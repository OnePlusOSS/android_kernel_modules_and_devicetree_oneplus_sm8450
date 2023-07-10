// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022 Oplus. All rights reserved.
 */
#ifdef CONFIG_LOCKING_PROTECT
#define pr_fmt(fmt) "dstate_opt: " fmt

#include <linux/sched.h>
#include <linux/mutex.h>
#include <linux/rwsem.h>
#include <linux/ww_mutex.h>
#include <linux/sched/signal.h>
#include <linux/sched/rt.h>
#include <linux/sched/wake_q.h>
#include <linux/sched/debug.h>
#include <linux/export.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/debug_locks.h>
#include <linux/osq_lock.h>
#include <linux/sched_clock.h>
#include <../kernel/sched/sched.h>
#include <trace/hooks/vendor_hooks.h>
#include <trace/hooks/sched.h>
#include <trace/hooks/dtask.h>
#include <trace/hooks/binder.h>
#include <trace/hooks/rwsem.h>
#include <trace/hooks/futex.h>
#include <trace/hooks/fpsimd.h>
#include <trace/hooks/topology.h>
#include <trace/hooks/debug.h>
#include <trace/hooks/wqlockup.h>
#include <trace/hooks/cgroup.h>
#include <trace/hooks/sys.h>
#include <trace/hooks/mm.h>
#include <linux/jiffies.h>
#include <../../sched/sched_assist/sa_common.h>

bool locking_protect_disable = false;

unsigned int locking_wakeup_preepmt_enable;

static DEFINE_PER_CPU(int, prev_locking_state);
static DEFINE_PER_CPU(int, prev_locking_depth);
void locking_state_systrace_c(unsigned int cpu, struct task_struct *p)
{
	struct oplus_task_struct *ots;
	int locking_state, locking_depth;

	ots = get_oplus_task_struct(p);
	/*
	 * 0: ots alloced but not locking, not be protected.
	 * 1: ots alloced and locking, preempt protected.
	 * 2: ots not alloc，not be protected.
	 */
	if (IS_ERR_OR_NULL(ots)) {
		locking_state = 2;
		locking_depth = 0;
	} else {
		locking_state = (ots->locking_start_time > 0 ? 1 : 0);
		locking_depth = ots->locking_depth;
	}

	if (per_cpu(prev_locking_state, cpu) != locking_state) {
		char buf[256];

		snprintf(buf, sizeof(buf), "C|9999|Cpu%d_locking_state|%d\n",
				cpu, locking_state);
		tracing_mark_write(buf);
		per_cpu(prev_locking_state, cpu) = locking_state;
	}

	if (per_cpu(prev_locking_depth, cpu) != locking_depth) {
		char buf[256];

		snprintf(buf, sizeof(buf), "C|9999|Cpu%d_locking_depth|%d\n",
				cpu, locking_depth);
		tracing_mark_write(buf);
		per_cpu(prev_locking_depth, cpu) = locking_depth;
	}
}

static inline struct task_struct *task_of(struct sched_entity *se)
{
	return container_of(se, struct task_struct, se);
}

bool task_skip_protect(struct task_struct *p)
{
	return test_task_ux(p);
}

#define MAX_PROTECT_NESTED_DEPTCH (8)
bool task_inlock(struct oplus_task_struct *ots)
{
	if (locking_protect_disable ==  true) {
		locking_wakeup_preepmt_enable = 0;
		return false;
	}

	if (unlikely(ots->locking_depth >= MAX_PROTECT_NESTED_DEPTCH))
		return false;

	return ots->locking_start_time > 0;
}

void enqueue_locking_thread(struct rq *rq, struct task_struct *p)
{
	struct oplus_task_struct *ots = NULL;
	struct oplus_rq *orq = NULL;
	struct list_head *pos, *n;

	if (!rq || !p || locking_protect_disable)
		return;

	ots = get_oplus_task_struct(p);
	orq = (struct oplus_rq *) rq->android_oem_data1;

	if (!oplus_list_empty(&ots->locking_entry))
		return;

	if (task_inlock(ots)) {
		bool exist = false;

		list_for_each_safe(pos, n, &orq->locking_thread_list) {
			if (pos == &ots->locking_entry) {
				exist = true;
				break;
			}
		}
		if (!exist) {
			list_add_tail(&ots->locking_entry, &orq->locking_thread_list);
			orq->rq_locking_task++;
			get_task_struct(p);
			if (unlikely(global_debug_enabled & DEBUG_FTRACE_LK))
				trace_printk("comm=%-12s pid=%d rq_nr=%d\n", p->comm, p->pid, orq->rq_locking_task);
		}
	}
}

void dequeue_locking_thread(struct rq *rq, struct task_struct *p)
{
	struct oplus_task_struct *ots = NULL;
	struct oplus_rq *orq = NULL;
	struct list_head *pos, *n;

	if (!rq || !p || locking_protect_disable)
		return;

	ots = get_oplus_task_struct(p);
	orq = (struct oplus_rq *) rq->android_oem_data1;

	if (!oplus_list_empty(&ots->locking_entry)) {
		list_for_each_safe(pos, n, &orq->locking_thread_list) {
			if (pos == &ots->locking_entry) {
				list_del_init(&ots->locking_entry);
				orq->rq_locking_task--;
				if (unlikely(global_debug_enabled & DEBUG_FTRACE_LK))
					trace_printk("comm=%-12s pid=%d rq_nr=%d\n", p->comm, p->pid, orq->rq_locking_task);
				put_task_struct(p);
				return;
			}
		}
		/* Task p is not in this rq? */
		pr_err("Can not find real rq, p->cpu=%d curr_rq=%d smpcpu=%d\n", task_cpu(p), cpu_of(rq), smp_processor_id());
	}
}

static inline bool orq_has_locking_tasks(struct oplus_rq *orq)
{
	bool ret = false;

	if (!orq)
		return false;
	ret = !oplus_list_empty(&orq->locking_thread_list);

	return ret;
}

void oplus_replace_locking_task_fair(struct rq *rq, struct task_struct **p,
					struct sched_entity **se, bool *repick)
{
	struct oplus_rq *orq = NULL;
	struct list_head *pos = NULL;
	struct list_head *n = NULL;
	struct sched_entity *key_se;
	struct task_struct *key_task;
	struct oplus_task_struct *key_ots;

	if (unlikely(!global_sched_assist_enabled))
		return;

	if (!rq || !p || !se || locking_protect_disable)
		return;

	orq = (struct oplus_rq *)rq->android_oem_data1;
	if (!orq_has_locking_tasks(orq))
		return;

	list_for_each_safe(pos, n, &orq->locking_thread_list) {
		key_ots = list_entry(pos, struct oplus_task_struct, locking_entry);

		if (IS_ERR_OR_NULL(key_ots))
			continue;

		key_task = ots_to_ts(key_ots);

		if (IS_ERR_OR_NULL(key_task)) {
			list_del_init(&key_ots->locking_entry);
			orq->rq_locking_task--;
			continue;
		}

		key_se = &key_task->se;

		if (!test_task_is_fair(key_task) || !task_inlock(key_ots)
			|| (key_task->flags & PF_EXITING) || unlikely(!key_se) || test_task_ux(key_task)) {
			list_del_init(&key_ots->locking_entry);
			orq->rq_locking_task--;
			put_task_struct(key_task);
			continue;
		}

		if (unlikely(task_cpu(key_task) != rq->cpu))
			continue;

		*p = key_task;
		*se = key_se;
		*repick = true;
		if (unlikely(global_debug_enabled & DEBUG_FTRACE_LK))
			trace_printk("pick locking task=%-12s pid=%d locking_depth=%d\n",
				key_task->comm, key_task->pid, key_ots->locking_depth);

		break;
	}
}

static inline void update_locking_time(unsigned long time, bool in_cs)
{
	struct oplus_task_struct *ots;

	/* Rt thread do not need our help. */
	if (test_task_is_rt(current))
		return;

	ots = get_oplus_task_struct(current);
	if (IS_ERR_OR_NULL(ots))
		return;

	if (!in_cs)
		goto set;

	/*
	 * Current has acquired the lock, increase it's locking depth.
	 * The depth over one means current hold more than one lock.
	 */
	if (time > 0) {
		ots->locking_depth++;
		goto set;
	}

	/*
	 * Current has released the lock, decrease it's locking depth.
	 * The depth become zero means current has leave all the critical section.
	 */
	if (unlikely(ots->locking_depth <= 0)) {
		ots->locking_depth = 0;
		goto set;
	}

	if (--(ots->locking_depth))
		return;

set:
	ots->locking_start_time = time;
}

static void android_vh_mutex_wait_start_handler(void *unused, struct mutex *lock)
{
	update_locking_time(jiffies, false);
}

static void android_vh_rtmutex_wait_start_handler(void *unused, struct rt_mutex *lock)
{
	update_locking_time(jiffies, false);
}

static void record_lock_starttime_handler(void *unused,
			struct task_struct *tsk, unsigned long settime)
{
	update_locking_time(settime, true);
}

static void android_vh_alter_rwsem_list_add_handler(void *unused, struct rwsem_waiter *waiter,
			struct rw_semaphore *sem, bool *already_on_list)
{
	update_locking_time(jiffies, false);
}

static void check_preempt_tick_handler(void *unused, struct task_struct *p,
			unsigned long *ideal_runtime, bool *skip_preempt,
			unsigned long delta_exec, struct cfs_rq *cfs_rq,
			struct sched_entity *curr, unsigned int granularity)
{
	struct task_struct *curr_task = entity_is_task(curr) ? task_of(curr) : NULL;
	struct oplus_task_struct *ots;

	if (NULL == curr_task)
		return;

	ots = get_oplus_task_struct(curr_task);
	if (likely(0 == ots->locking_start_time))
		return;

	if (time_after(jiffies, ots->locking_start_time))
		ots->locking_start_time = 0;
}

static int register_dstate_opt_vendor_hooks(void)
{
	int ret = 0;

	ret = register_trace_android_vh_record_mutex_lock_starttime(
					record_lock_starttime_handler, NULL);
	if (ret != 0) {
		pr_err("android_vh_record_mutex_lock_starttime failed! ret=%d\n", ret);
		goto out;
	}

	ret = register_trace_android_vh_record_rtmutex_lock_starttime(
					record_lock_starttime_handler, NULL);
	if (ret != 0) {
		pr_err("android_vh_record_rtmutex_lock_starttime failed! ret=%d\n", ret);
		goto out1;
	}

	ret = register_trace_android_vh_record_rwsem_lock_starttime(
					record_lock_starttime_handler, NULL);
	if (ret != 0) {
		pr_err("record_rwsem_lock_starttime failed! ret=%d\n", ret);
		goto out2;
	}

#ifdef CONFIG_PCPU_RWSEM_LOCKING_PROTECT
	ret = register_trace_android_vh_record_pcpu_rwsem_starttime(
					record_lock_starttime_handler, NULL);
	if (ret != 0) {
		pr_err("record_pcpu_rwsem_starttime failed! ret=%d\n", ret);
		goto out3;
	}
#endif

	ret = register_trace_android_rvh_check_preempt_tick(check_preempt_tick_handler,
								NULL);
	if (ret != 0) {
		pr_err("register_trace_android_rvh_check_preempt_tick failed! ret=%d\n", ret);
		goto out4;
	}

	ret = register_trace_android_vh_alter_rwsem_list_add(
			android_vh_alter_rwsem_list_add_handler, NULL);
	if (ret != 0) {
		pr_err("register_trace_android_vh_alter_rwsem_list_add failed! ret=%d\n", ret);
		goto out4;
	}

	ret = register_trace_android_vh_mutex_wait_start(android_vh_mutex_wait_start_handler, NULL);
	if (ret != 0) {
		pr_err("register_trace_android_vh_mutex_wait_start failed! ret=%d\n", ret);
		goto out4;
	}

	ret = register_trace_android_vh_rtmutex_wait_start(android_vh_rtmutex_wait_start_handler, NULL);
	if (ret != 0) {
		pr_err("register_trace_android_vh_rtmutex_wait_start failed! ret=%d\n", ret);
		goto out4;
	}

	return ret;

out4:
#ifdef CONFIG_PCPU_RWSEM_LOCKING_PROTECT
	unregister_trace_android_vh_record_pcpu_rwsem_starttime(
				record_lock_starttime_handler, NULL);
out3:
#endif
	unregister_trace_android_vh_record_rwsem_lock_starttime(
				record_lock_starttime_handler, NULL);
out2:
	unregister_trace_android_vh_record_rtmutex_lock_starttime(
				record_lock_starttime_handler, NULL);
out1:
	unregister_trace_android_vh_record_mutex_lock_starttime(
				record_lock_starttime_handler, NULL);
out:
	return ret;
}

static void unregister_dstate_opt_vendor_hooks(void)
{
	unregister_trace_android_vh_rtmutex_wait_start(
			android_vh_rtmutex_wait_start_handler, NULL);
	unregister_trace_android_vh_mutex_wait_start(
			android_vh_mutex_wait_start_handler, NULL);
	unregister_trace_android_vh_alter_rwsem_list_add(
			android_vh_alter_rwsem_list_add_handler, NULL);
#ifdef CONFIG_PCPU_RWSEM_LOCKING_PROTECT
	unregister_trace_android_vh_record_pcpu_rwsem_starttime(
			record_lock_starttime_handler, NULL);
#endif
	unregister_trace_android_vh_record_mutex_lock_starttime(
			record_lock_starttime_handler, NULL);
	unregister_trace_android_vh_record_rtmutex_lock_starttime(
			record_lock_starttime_handler, NULL);
	unregister_trace_android_vh_record_rwsem_lock_starttime(
			record_lock_starttime_handler, NULL);
}

int sched_assist_locking_init(void)
{
	int ret = 0;

	ret = register_dstate_opt_vendor_hooks();
	if (ret != 0)
		return ret;

	pr_info("%s succeed!\n", __func__);
	return 0;
}

void sched_assist_locking_exit(void)
{
	unregister_dstate_opt_vendor_hooks();
	pr_info("%s exit init succeed!\n", __func__);
}
#endif

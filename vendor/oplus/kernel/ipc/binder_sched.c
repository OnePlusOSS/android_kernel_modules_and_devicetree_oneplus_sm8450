// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020 Oplus. All rights reserved.
 */


#include <linux/seq_file.h>
#include <drivers/android/binder_internal.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/delay.h>
#include <trace/hooks/binder.h>
#include <linux/random.h>

#if IS_ENABLED(CONFIG_OPLUS_FEATURE_SCHED_ASSIST)
#include <../kernel/oplus_cpu/sched/sched_assist/sa_common.h>
#endif
#include "binder_sched.h"
#define CREATE_TRACE_POINTS
#include "binder_sched_trace.h"

struct kmem_cache *oplus_binder_struct_cachep = NULL;
unsigned int g_sched_enable = 1;
unsigned long long g_sched_debug = 0;

unsigned int g_async_ux_enable = 1;
unsigned int g_set_last_async_ux = 1;
unsigned int g_set_async_ux_after_pending = 1;
static unsigned int async_insert_queue = 1;
static unsigned int sync_insert_queue = 1;
static unsigned int async_ux_test = 0;

static int insert_limit[NUM_INSERT_MAX] = {0};

#define trace_binder_debug(x...) \
	do { \
		if (g_sched_debug) \
			trace_printk(x); \
	} while (0)

#define oplus_binder_debug(debug_mask, x...) \
	do { \
		if (g_sched_debug & debug_mask) \
			pr_info(x); \
	} while (0)

static inline struct oplus_binder_struct *alloc_oplus_binder_struct(void)
{
	if (!oplus_binder_struct_cachep) {
		return NULL;
	} else {
		return kmem_cache_alloc(oplus_binder_struct_cachep, GFP_ATOMIC);
	}
}

static inline void free_oplus_binder_struct(struct oplus_binder_struct *obs)
{
	if ((unsigned long long)obs == OBS_NOT_ASYNC_UX_VALUE) {
		obs = NULL;
		return;
	} else if (!oplus_binder_struct_cachep || IS_ERR_OR_NULL(obs)) {
		return;
	} else {
		kmem_cache_free(oplus_binder_struct_cachep, obs);
	}
}

static inline int is_obs_valid(struct oplus_binder_struct *obs)
{
	if ((unsigned long long)obs == OBS_NOT_ASYNC_UX_VALUE)
		return OBS_NOT_ASYNC_UX;
	else if (IS_ERR_OR_NULL(obs))
		return OBS_INVALID;
	else
		return OBS_VALID;
}

struct oplus_binder_struct *get_oplus_binder_struct(struct binder_transaction *t, bool alloc)
{
	struct oplus_binder_struct *obs = NULL;

	if (IS_ERR_OR_NULL(t)) {
		return NULL;
	}

	obs = (struct oplus_binder_struct *)(t->android_vendor_data1);
	if (!alloc) {
		trace_binder_get_obs(t, obs, alloc, "no alloc");
		return obs;
	}
	trace_binder_get_obs(t, obs, alloc, "before alloc");
	if (is_obs_valid(obs) == OBS_VALID) {
		return obs;
	} else if (alloc) {
		obs = alloc_oplus_binder_struct();
		t->android_vendor_data1 = (unsigned long long)obs;
		trace_binder_get_obs(t, obs, alloc, "after alloc");
		return obs;
	} else {
		return NULL;
	}
}

static void set_obs_not_async_ux(struct binder_transaction *t, struct oplus_binder_struct *obs)
{
	obs = (struct oplus_binder_struct *)OBS_NOT_ASYNC_UX_VALUE;
	if (!IS_ERR_OR_NULL(t)) {
		t->android_vendor_data1 = (unsigned long long)obs;
	}
}

static inline bool binder_is_sync_mode(u32 flags)
{
	return !(flags & TF_ONE_WAY);
}

static inline bool get_task_async_ux_sts(struct oplus_task_struct *ots)
{
	if (IS_ERR_OR_NULL(ots)) {
		return false;
	} else {
		return ots->binder_async_ux_sts;
	}
}

static inline void set_task_async_ux_sts(struct oplus_task_struct *ots, bool sts)
{
	if (IS_ERR_OR_NULL(ots)) {
		return;
	} else {
		ots->binder_async_ux_sts = sts;
	}
}

void set_task_async_ux_enable(pid_t pid, int enable)
{
	struct task_struct *task = NULL;
	struct oplus_task_struct *ots = NULL;

	if (unlikely(!g_async_ux_enable)) {
		return;
	}
	if (enable >= ASYNC_UX_ENABLE_MAX) {
		trace_binder_set_get_ux(task, pid, enable, "set, enable error");
		return;
	}

	if (pid == CURRENT_TASK_PID) {
		task = current;
	} else {
		if (pid < 0 || pid > PID_MAX_DEFAULT) {
			trace_binder_set_get_ux(task, pid, enable, "set, pid error");
			return;
		}
		task = find_task_by_vpid(pid);
		if (IS_ERR_OR_NULL(task)) {
			trace_binder_set_get_ux(NULL, pid, enable, "set, task null");
			return;
		}
	}
	ots = get_oplus_task_struct(task);
	if (IS_ERR_OR_NULL(ots)) {
		trace_binder_set_get_ux(task, pid, enable, "set, ots null");
		return;
	}
	ots->binder_async_ux_enable = enable;

	trace_binder_set_get_ux(task, pid, enable, "set enable end");
	oplus_binder_debug(LOG_SET_ASYNC_UX, "(set_pid=%d task_pid=%d comm=%s) enable=%d ux_sts=%d set enable end\n",
		pid, task->pid, task->comm, ots->binder_async_ux_enable, ots->binder_async_ux_sts);
}

bool get_task_async_ux_enable(pid_t pid)
{
	struct task_struct *task = NULL;
	struct oplus_task_struct *ots = NULL;
	int enable = 0;

	if (unlikely(!g_async_ux_enable)) {
		return false;
	}

	if (pid == CURRENT_TASK_PID) {
		task = current;
	} else {
		if (pid < 0 || pid > PID_MAX_DEFAULT) {
			trace_binder_set_get_ux(task, pid, enable, "get, pid error");
			return false;
		}
		task = find_task_by_vpid(pid);
		if (IS_ERR_OR_NULL(task)) {
			trace_binder_set_get_ux(NULL, pid, enable, "get, task null");
			return false;
		}
	}

	ots = get_oplus_task_struct(task);
	if (IS_ERR_OR_NULL(ots)) {
		trace_binder_set_get_ux(task, pid, enable, "get, ots null");
		return false;
	}
	enable = ots->binder_async_ux_enable;
	trace_binder_set_get_ux(task, pid, enable, "get end");
	return enable;
}

void get_all_tasks_async_ux_enable(void)
{
	struct task_struct *p = NULL;
	struct task_struct *t = NULL;
	struct oplus_task_struct *ots = NULL;
	bool async_ux_task = false;

	for_each_process_thread(p, t) {
		ots = get_oplus_task_struct(t);
		if (IS_ERR_OR_NULL(ots)) {
			pr_info("[async_ux_tasks] ots err, pid=%d tgid=%d comm=%s async_ux_enable=unknown\n",
				t->pid, t->tgid, t->comm);
			trace_binder_set_get_ux(t, INVALID_VALUE, INVALID_VALUE, "[async_ux_tasks] ots err");
		} else if (ots->binder_async_ux_enable) {
			async_ux_task = true;
			pr_info("[async_ux_tasks] pid=%d tgid=%d comm=%s async_ux_enable=%d\n",
				t->pid, t->tgid, t->comm, ots->binder_async_ux_enable);
			trace_binder_set_get_ux(t, INVALID_VALUE, ots->binder_async_ux_enable, "[async_ux_tasks]");
		}
	}
	if (!async_ux_task) {
		pr_info("[async_ux_tasks] no async_ux task\n");
		trace_binder_set_get_ux(NULL, INVALID_VALUE, INVALID_VALUE,
			"[async_ux_tasks] no task");
	}
}

noinline int tracing_mark_write(const char *buf)
{
	trace_printk(buf);
	return 0;
}

static void binder_ux_state_systrace(struct task_struct *from,
	struct task_struct *target, int ux_state, int systrace_lvl)
{
	bool lvl0_enable = false;
	bool lvl1_enable = false;
	int from_pid = 0;
	int target_pid = 0;

	if (g_sched_debug & LOG_BINDER_SYSTRACE_LVL0) {
		lvl0_enable = true;
	}
	if (g_sched_debug & LOG_BINDER_SYSTRACE_LVL1) {
		lvl1_enable = true;
	}
	if (!lvl0_enable && !lvl1_enable) {
		return;
	} else if ((systrace_lvl == LOG_BINDER_SYSTRACE_LVL1) && !lvl1_enable) {
		return;
	} else {
		char buf[128] = {0};
		if (!IS_ERR_OR_NULL(from)) {
			from_pid = from->pid;
		}
		if (!IS_ERR_OR_NULL(target)) {
			target_pid = target->pid;
		}
		snprintf(buf, sizeof(buf), "C|9999|z_binder_from|%d\n", from_pid);
		tracing_mark_write(buf);
		memset(buf, 0, sizeof(buf));
		snprintf(buf, sizeof(buf), "C|9999|z_binder_target|%d\n", target_pid);
		tracing_mark_write(buf);
		memset(buf, 0, sizeof(buf));
		snprintf(buf, sizeof(buf), "C|9999|z_binder_ux_state|%d\n", ux_state);
		tracing_mark_write(buf);
	}
}

#if IS_ENABLED(CONFIG_OPLUS_FEATURE_SCHED_ASSIST)

static inline void sync_binder_set_inherit_ux(struct task_struct *thread_task,
	struct task_struct *from_task, bool sync)
{
	int from_depth = oplus_get_ux_depth(from_task);
	int from_state = oplus_get_ux_state(from_task);
	int type = get_ux_state_type(thread_task);

	if (type != UX_STATE_NONE && type != UX_STATE_INHERIT) {
		trace_binder_inherit_ux(from_task, thread_task, from_depth, from_state,
			type, INVALID_VALUE, sync, "sync_set type not expected");
		binder_ux_state_systrace(current, thread_task, STATE_SYNC_TYPE_UNEXPECTED, LOG_BINDER_SYSTRACE_LVL1);
		return;
	}
	if (from_task && test_set_inherit_ux(from_task)) {
		if (!test_inherit_ux(thread_task, INHERIT_UX_BINDER)) {
			set_inherit_ux(thread_task, INHERIT_UX_BINDER, from_depth, from_state);
			trace_binder_inherit_ux(from_task, thread_task, from_depth, from_state,
				type, INVALID_VALUE, sync, "sync_set ux set");
			oplus_binder_debug(LOG_SET_SYNC_UX, "sync_set ux set, from(%d %d %s) set(%d %d %s)\n",
				from_task->pid, from_task->tgid, from_task->comm,
				thread_task->pid, thread_task->tgid, thread_task->comm);
			binder_ux_state_systrace(current, thread_task, STATE_SYNC_SET_UX, LOG_BINDER_SYSTRACE_LVL0);
		} else {
			reset_inherit_ux(thread_task, from_task, INHERIT_UX_BINDER);
			trace_binder_inherit_ux(from_task, thread_task, from_depth,
				from_state, type, INVALID_VALUE, sync, "sync_set ux reset");
			oplus_binder_debug(LOG_SET_SYNC_UX, "sync_set ux reset, from(%d %d %s) set(%d %d %s)\n",
				from_task->pid, from_task->tgid, from_task->comm,
				thread_task->pid, thread_task->tgid, thread_task->comm);
			binder_ux_state_systrace(current, thread_task, STATE_SYNC_RESET_UX, LOG_BINDER_SYSTRACE_LVL0);
		}
	}  else if (from_task && test_task_is_rt(from_task)) { /* rt trans can be set as ux if binder thread is cfs class */
		if (!test_inherit_ux(thread_task, INHERIT_UX_BINDER)) {
			int ux_value = SA_TYPE_LIGHT;
			set_inherit_ux(thread_task, INHERIT_UX_BINDER, from_depth, ux_value);
			trace_binder_inherit_ux(from_task, thread_task, from_depth,
				from_state, type, INVALID_VALUE, sync, "sync_set ux rt");
			oplus_binder_debug(LOG_SET_SYNC_UX, "sync_set ux rt, from(%d %d %s) set(%d %d %s)\n",
				from_task->pid, from_task->tgid, from_task->comm,
				thread_task->pid, thread_task->tgid, thread_task->comm);
			binder_ux_state_systrace(current, thread_task, STATE_SYNC_RT_SET_UX, LOG_BINDER_SYSTRACE_LVL0);
		} else {
			trace_binder_inherit_ux(from_task, thread_task, from_depth,
				from_state, type, INVALID_VALUE, sync, "sync_set rt none");
			binder_ux_state_systrace(current, thread_task, STATE_SYNC_RT_NOT_SET, LOG_BINDER_SYSTRACE_LVL1);
		}
	} else {
		trace_binder_inherit_ux(from_task, thread_task, from_depth, from_state,
			type, INVALID_VALUE, sync, "sync_set end do nothing");
		binder_ux_state_systrace(current, thread_task, STATE_SYNC_NOT_SET, LOG_BINDER_SYSTRACE_LVL1);
	}
}

static inline void async_binder_set_inherit_ux(struct task_struct *thread_task,
	struct task_struct *from_task, bool sync)
{
	struct oplus_task_struct *ots = NULL;
	int type = 0;
	int ux_value = 0;

	if (unlikely(!g_async_ux_enable)) {
		return;
	}

	if (!thread_task) {
		return;
	}

	type = get_ux_state_type(thread_task);
	if (type != UX_STATE_NONE && type != UX_STATE_INHERIT) {
		trace_binder_inherit_ux(from_task, thread_task, INVALID_VALUE, INVALID_VALUE,
			type, INVALID_VALUE, sync, "async_set type not expected");
		return;
	}

	ots = get_oplus_task_struct(thread_task);
	if (unlikely(IS_ERR_OR_NULL(ots))) {
		trace_binder_inherit_ux(from_task, thread_task, INVALID_VALUE, INVALID_VALUE,
			type, INVALID_VALUE, sync, "async_set ots null");
		return;
	}
	/*
	if (get_task_async_ux_sts(ots)) {
		trace_binder_inherit_ux(from_task, thread_task, INVALID_VALUE, INVALID_VALUE,
			type, INVALID_VALUE, sync, "async_set_inherit_ux ux_sts true");
		return;
	}
	*/
	trace_binder_inherit_ux(from_task, thread_task, ots->ux_depth, ots->ux_state,
		type, ots->binder_async_ux_sts, sync, "async_set before set");

	set_task_async_ux_sts(ots, true);
	ux_value = (ots->ux_state | SA_TYPE_HEAVY);
	set_inherit_ux(thread_task, INHERIT_UX_BINDER, ots->ux_depth, ux_value);

	trace_binder_inherit_ux(from_task, thread_task, ots->ux_depth, ots->ux_state,
		type, ots->binder_async_ux_sts, sync, "async_set after set");

	oplus_binder_debug(LOG_SET_ASYNC_UX, "async_set_ux after set, thread(pid=%d tgid=%d comm=%s) ux_sts=%d ux_state=%d ux_depth=%d inherit_ux=%lld\n",
		thread_task->pid, thread_task->tgid, thread_task->comm,
		ots->binder_async_ux_sts, ots->ux_state, ots->ux_depth, atomic64_read(&ots->inherit_ux));
	binder_ux_state_systrace(current, thread_task, STATE_ASYNC_SET_UX, LOG_BINDER_SYSTRACE_LVL0);
}

static inline void binder_set_inherit_ux(struct task_struct *thread_task,
	struct task_struct *from_task, bool sync)
{
	if (sync) {
		sync_binder_set_inherit_ux(thread_task, from_task, sync);
	} else {
		async_binder_set_inherit_ux(thread_task, from_task, sync);
	}
}

static inline void binder_unset_inherit_ux(struct task_struct *thread_task, bool sync)
{
	struct oplus_task_struct *ots = get_oplus_task_struct(thread_task);

	if (test_inherit_ux(thread_task, INHERIT_UX_BINDER)) {
		if (!IS_ERR_OR_NULL(ots)) {
			trace_binder_inherit_ux(NULL, thread_task, ots->ux_depth, ots->ux_state,
				INVALID_VALUE, ots->binder_async_ux_sts,
				sync, "unset_ux before unset");
		}
		unset_inherit_ux(thread_task, INHERIT_UX_BINDER);
		if (!IS_ERR_OR_NULL(ots)) {
			if (!sync) {
				set_task_async_ux_sts(ots, false);
			}
			trace_binder_inherit_ux(NULL, thread_task, ots->ux_depth, ots->ux_state,
				INVALID_VALUE, ots->binder_async_ux_sts, sync, "unset_ux after unset");
			if (!sync) {
				oplus_binder_debug(LOG_SET_ASYNC_UX, "async_unset_ux after unset, thread(pid = %d tgid = %d comm = %s) \
					 ots_enable = %d ux_sts = %d ux_state = %d ux_depth = %d inherit_ux = %lld\n",
					thread_task->pid, thread_task->tgid, thread_task->comm, ots->binder_async_ux_enable,
					ots->binder_async_ux_sts, ots->ux_state, ots->ux_depth, atomic64_read(&ots->inherit_ux));
			} else {
				oplus_binder_debug(LOG_SET_SYNC_UX, "sync_unset_ux after unset, thread(pid = %d tgid = %d comm = %s) \
					 ux_state = %d ux_depth = %d inherit_ux = %lld\n",
					thread_task->pid, thread_task->tgid, thread_task->comm,
					ots->ux_state, ots->ux_depth, atomic64_read(&ots->inherit_ux));
			}
		}
		if (sync)
			binder_ux_state_systrace(current, thread_task, STATE_SYNC_UNSET_UX, LOG_BINDER_SYSTRACE_LVL0);
		else
			binder_ux_state_systrace(current, thread_task, STATE_ASYNC_UNSET_UX, LOG_BINDER_SYSTRACE_LVL0);
	} else {
		trace_binder_inherit_ux(NULL, thread_task, INVALID_VALUE, INVALID_VALUE,
			INVALID_VALUE, INVALID_VALUE, sync, "unset_ux do nothing");
	}
}

#else /* CONFIG_OPLUS_FEATURE_SCHED_ASSIST */
static inline void binder_set_inherit_ux(struct task_struct *thread_task,
	struct task_struct *from_task, bool sync)
{
}

static inline void binder_unset_inherit_ux(struct task_struct *thread_task, bool sync)
{
}
#endif

/* implement vender hook in driver/android/binder.c */
void android_vh_binder_restore_priority_handler(void *unused,
	struct binder_transaction *t, struct task_struct *task)
{
	if (unlikely(!g_sched_enable))
		return;

	/* Google commit "d1367b5" caused this priority pass issue on our kernel-5.15 project */
#if (LINUX_VERSION_CODE > KERNEL_VERSION(5, 15, 0))
	if (t != NULL) {
		struct binder_priority *sp = &t->saved_priority;
		if (task->prio < MAX_RT_PRIO && !sp->prio && !sp->sched_policy) {
			sp->sched_policy = task->policy;
			sp->prio = task->prio;
		}
	}
#endif

	if (t != NULL) {
		trace_binder_ux_task(1, INVALID_VALUE, INVALID_VALUE, task,
			INVALID_VALUE, t, NULL, "sync_ux unset priority");
		binder_unset_inherit_ux(task, true);
	}
}

void android_vh_binder_wait_for_work_handler(void *unused,
			bool do_proc_work, struct binder_thread *tsk, struct binder_proc *proc)
{
	if (unlikely(!g_sched_enable))
		return;

	if (do_proc_work) {
		trace_binder_ux_task(1, INVALID_VALUE, INVALID_VALUE, tsk->task, INVALID_VALUE,
			NULL, NULL, "sync_ux unset wait_for_work");
		binder_unset_inherit_ux(tsk->task, true);
	}
}

void android_vh_sync_txn_recvd_handler(void *unused,
	struct task_struct *tsk, struct task_struct *from)
{
	if (unlikely(!g_sched_enable))
		return;

	trace_binder_ux_task(1, INVALID_VALUE, INVALID_VALUE, tsk, INVALID_VALUE,
		NULL, NULL, "sync_ux set txn_recvd");
	binder_set_inherit_ux(tsk, from, true);
}

#ifdef CONFIG_OPLUS_BINDER_PRIO_SKIP
static void android_vh_binder_priority_skip_handler(void *unused,
	struct task_struct *task, bool *skip)
{
	if (task->prio < MAX_RT_PRIO)
		*skip = true;
}
#endif

static int async_ux_test_debug(void)
{
	static unsigned int count = 0;
	unsigned int remainder = 0;
	int ret = 0;

	if (async_ux_test == ASYNC_UX_TEST_DISABLE) {
		return 0;
	}

	switch(async_ux_test) {
	case ASYNC_UX_RANDOM_LOW_INSERT_TEST:
		get_random_bytes(&count, sizeof(unsigned int));
		ret = (count % (2 * ASYNC_UX_ENABLE_MAX));
		break;
	case ASYNC_UX_RANDOM_HIGH_INSERT_TEST:
		get_random_bytes(&count, sizeof(unsigned int));
		ret = (count % ASYNC_UX_ENABLE_MAX);
		break;
	case ASYNC_UX_RANDOM_LOW_ENQUEUE_TEST:
		get_random_bytes(&count, sizeof(unsigned int));
		ret = (count % (2 * ASYNC_UX_ENABLE_MAX));
		if (ret > ASYNC_UX_ENABLE_ENQUEUE) {
			ret = 0;
		}
		break;
	case ASYNC_UX_RANDOM_HIGH_ENQUEUE_TEST:
		get_random_bytes(&count, sizeof(unsigned int));
		ret = (count % (ASYNC_UX_ENABLE_ENQUEUE + 1));
		break;
	case ASYNC_UX_INORDER_TEST:
		count++;
		remainder = count % 10;
		if (remainder == 1 || remainder == 5) {
			ret = ASYNC_UX_ENABLE_ENQUEUE;
		} else if (remainder == 2 || remainder == 6 || remainder == 8) {
			ret = ASYNC_UX_ENABLE_INSERT_QUEUE;
		} else {
			ret = ASYNC_UX_DISABLE;
		}
		break;
	default:
		ret = 0;
		break;
	}
	if (ret >= ASYNC_UX_ENABLE_MAX) {
		ret = 0;
	}
	return ret;
}

static void android_vh_alloc_oem_binder_struct_handler(void *unused,
	struct binder_transaction_data *tr, struct binder_transaction *t, struct binder_proc *target_proc)
{
	struct oplus_binder_struct *obs = NULL;
	struct oplus_task_struct *ots = NULL;
	int async_ux_enable = 0, test_debug = 0;

	if (unlikely(!g_sched_enable) || unlikely(!g_async_ux_enable)) {
		return;
	}
	if (IS_ERR_OR_NULL(tr) || IS_ERR_OR_NULL(t)) {
		trace_binder_ux_enable(current, async_ux_enable, t,
			obs, "tr_buf t/tr err");
		return;
	}

	if (binder_is_sync_mode(tr->flags)) {
		return;
	}

	obs = get_oplus_binder_struct(t, false);
	if (is_obs_valid(obs) == OBS_NOT_ASYNC_UX) {
		trace_binder_ux_enable(current, async_ux_enable, t,
			obs, "tr NOT_ASYNC_UX");
		return;
	}

	if (is_obs_valid(obs) == OBS_VALID && obs->async_ux_enable) {
		trace_binder_ux_enable(current, obs->async_ux_enable, t,
			obs, "tr async_ux has enable");
		return;
	}

	ots = get_oplus_task_struct(current);
	if (IS_ERR_OR_NULL(ots)) {
		trace_binder_ux_enable(current, INVALID_VALUE, t,
			obs, "ots null");
		return;
	}

	async_ux_enable = ots->binder_async_ux_enable;
	test_debug = async_ux_test_debug();
	if (async_ux_enable || test_debug) {
		obs = get_oplus_binder_struct(t, true);
		if (is_obs_valid(obs) == OBS_VALID) {
			obs->async_ux_enable = async_ux_enable ? async_ux_enable : test_debug;
			trace_binder_ux_enable(current, obs->async_ux_enable, t,
				obs, "tr async_ux enable");
		}
	} else {
		trace_binder_ux_enable(current, async_ux_enable, t,
				obs, "tr async_ux not enable");
		set_obs_not_async_ux(t, obs);
	}
}

/* sync mode unset_ux: pls refer to android_vh_sync_txn_recvd_handler / android_vh_binder_priority_skip_handler / android_vh_binder_wait_for_work_handler  */
static void sync_mode_unset_ux(struct binder_transaction *t,
				struct binder_proc *proc, struct binder_thread *thread, bool finished)
{
}

static void async_mode_unset_ux(struct binder_transaction *t,
				struct binder_proc *proc, struct binder_thread *thread, bool finished)
{
	struct oplus_binder_struct *obs = NULL;
	struct oplus_task_struct *ots = NULL;
	struct task_struct *task = NULL;

	if (unlikely(!g_sched_enable) || unlikely(!g_async_ux_enable)) {
		return;
	}

	if (IS_ERR_OR_NULL(thread) || IS_ERR_OR_NULL(thread->task)) {
		return;
	}

	task = thread->task;
	ots = get_oplus_task_struct(task);
	if (IS_ERR_OR_NULL(ots)) {
		trace_binder_ux_task(false, INVALID_VALUE, INVALID_VALUE, task,
			INVALID_VALUE, t, NULL, "async_ux ots null, go");
	} else if (!get_task_async_ux_sts(ots)) {
		trace_binder_ux_task(false, INVALID_VALUE, INVALID_VALUE, task,
			INVALID_VALUE, t, NULL, "async_ux unset sts false");
		return;
	} else {
		trace_binder_ux_task(false, INVALID_VALUE, INVALID_VALUE, task,
			INVALID_VALUE, t, NULL, "async_ux unset sts true");
	}

	if (finished) {	/* t has been freed */
		binder_unset_inherit_ux(task, false);
		trace_binder_ux_task(false, INVALID_VALUE, INVALID_VALUE, task,
			INVALID_VALUE, t, NULL, "async_ux unset [finished]");
		return;
	}
	if (IS_ERR_OR_NULL(t)) {
		return;
	}
	obs = get_oplus_binder_struct(t, false);
	if (is_obs_valid(obs) != OBS_VALID) {
		trace_binder_ux_task(0, INVALID_VALUE, INVALID_VALUE, NULL, INVALID_VALUE,
			t, obs, "async_ux obs invalid");
		return;
	}
	if (obs->async_ux_enable == ASYNC_UX_DISABLE) {
		return;
	}
	obs->async_ux_enable = ASYNC_UX_DISABLE;
	binder_unset_inherit_ux(task, false);
	trace_binder_ux_task(false, INVALID_VALUE, INVALID_VALUE, task,
		obs->async_ux_enable, t, obs, "async_ux unset not-finished");
}

static void set_binder_thread_node(struct binder_transaction *t,
	struct task_struct *task, struct binder_buffer *buffer, bool sync, bool reset)
{
	struct oplus_task_struct *ots = NULL;
	struct binder_node *node = NULL;
	bool set_node = false;

	if (unlikely(!g_sched_enable) || unlikely(!g_async_ux_enable) || (!g_set_last_async_ux)) {
		return;
	}
	if (sync) {	/* don't use t->flags here because t maybe NULL */
		return;
	}
	if (IS_ERR_OR_NULL(task)) {
		return;
	}

	if (t && !IS_ERR_OR_NULL(t->buffer)) {
		node = t->buffer->target_node;
	}
	ots = get_oplus_task_struct(task);
	if (!IS_ERR_OR_NULL(ots)) {
		oplus_binder_debug(LOG_TRACK_LAST_ASYNC, "before set, thread(pid=%d tgid=%d comm=%s) sync: %d, reset: %d, ots_node: 0x%llx, node: 0x%llx\n",
			task->pid, task->tgid, task->comm, sync, reset, (unsigned long long)ots->binder_thread_node, (unsigned long long)node);
		if (reset) {
			ots->binder_thread_node = NULL;
			set_node = true;
			trace_set_thread_node(task, NULL, sync, "async reset");
		} else if (ots->binder_thread_node != node) {
			ots->binder_thread_node = node;
			set_node = true;
			trace_set_thread_node(task, node, sync, "async set");
		}
		oplus_binder_debug(LOG_TRACK_LAST_ASYNC, "after set, thread(pid=%d tgid=%d comm=%s) sync: %d, reset: %d, ots_node: 0x%llx, node: 0x%llx, set_node: %d\n",
			task->pid, task->tgid, task->comm, sync, reset, (unsigned long long)ots->binder_thread_node, (unsigned long long)node, set_node);
	} else {
		trace_set_thread_node(task, NULL, sync, "ots null");
	}
}

static void set_thread_node_when_br_received(struct binder_transaction *t, struct binder_thread *thread)
{
	struct task_struct *task = NULL;

	if (!g_set_last_async_ux) {
		return;
	}
	if (IS_ERR_OR_NULL(t) || IS_ERR_OR_NULL(thread) || IS_ERR_OR_NULL(thread->task)) {
		return;
	}
	task = thread->task;
	trace_set_thread_node(task, NULL, INVALID_VALUE, "set when br_received");
	oplus_binder_debug(LOG_TRACK_LAST_ASYNC, "set node when transaction_received\n");
	set_binder_thread_node(t, task, NULL, false, false);
}

static void set_async_ux_after_pending(struct binder_transaction *t, struct binder_thread *thread)
{
	struct oplus_binder_struct *obs = NULL;
	struct oplus_task_struct *ots = NULL;
	struct task_struct *task = NULL;
	int ux_enable = 0;
	if (!g_set_async_ux_after_pending) {
		return;
	}
	if (IS_ERR_OR_NULL(t) || IS_ERR_OR_NULL(thread) || IS_ERR_OR_NULL(thread->task)) {
		return;
	}
	obs = get_oplus_binder_struct(t, false);
	if (is_obs_valid(obs) != OBS_VALID) {
		trace_binder_set_async_afterpending(t, NULL, NULL, obs, INVALID_VALUE, "obs invalid");
		return;
	}
	ux_enable = obs->async_ux_enable;

	if (!obs->pending_async) {
		trace_binder_set_async_afterpending(t, task, NULL, obs, ux_enable, "not pending_async");
		return;
	}

	task = thread->task;
	ots = get_oplus_task_struct(task);
	if (IS_ERR_OR_NULL(ots)) {
		return;
	}
	if (get_task_async_ux_sts(ots)) {
		trace_binder_set_async_afterpending(t, task, ots, obs, ux_enable, "ux_sts true");
		return;
	}

	trace_binder_set_async_afterpending(t, task, ots, obs, ux_enable, "set when br_received");
	oplus_binder_debug(LOG_SET_ASYNC_AFTER_PENDING, "thread(pid=%d tgid=%d comm=%s) ux_en=%d pending_async=%d set_async_after_pending\n",
		task->pid, task->tgid, task->comm, ux_enable, obs->pending_async);
	binder_ux_state_systrace(current, task, STATE_ASYNC_SET_UX_AFTER_PENDING, LOG_BINDER_SYSTRACE_LVL0);

	binder_set_inherit_ux(task, NULL, false);
	obs->pending_async = false;
}
static void android_vh_binder_transaction_received_handler(void *unused,
	struct binder_transaction *t, struct binder_proc *proc, struct binder_thread *thread, uint32_t cmd)
{
	if (unlikely(!g_sched_enable) || unlikely(!g_async_ux_enable)) {
		return;
	}
	if(!strncmp(proc->tsk->comm, SYSTEM_SERVER_NAME, TASK_COMM_LEN)) {
		if(t->debug_id == insert_limit[NUM_INSERT_ID1]) {
			insert_limit[NUM_INSERT_ID1] = 0;
		} else if (t->debug_id == insert_limit[NUM_INSERT_ID2]) {
			insert_limit[NUM_INSERT_ID2] = 0;
		}
	}
	if (binder_is_sync_mode(t->flags)) {
		return;
	}
	set_thread_node_when_br_received(t, thread);
	set_async_ux_after_pending(t, thread);
}

static void android_vh_binder_free_buf_handler(void *unused,
	struct binder_proc *proc, struct binder_thread *thread, struct binder_buffer *buffer)
{
	if (unlikely(!g_sched_enable) || unlikely(!g_async_ux_enable)) {
		return;
	}

	if (IS_ERR_OR_NULL(thread) || IS_ERR_OR_NULL(thread->task)) {
		return;
	}
	if (buffer->async_transaction) {
		async_mode_unset_ux(buffer->transaction, proc, thread, true);
		set_binder_thread_node(NULL, thread->task, buffer, false, true);
		trace_binder_free_buf(proc, thread, buffer, "async mode");
	} else {
		sync_mode_unset_ux(buffer->transaction, proc, thread, true);
		set_binder_thread_node(NULL, thread->task, buffer, true, true);
		trace_binder_free_buf(proc, thread, buffer, "sync mode");
	}
}

static void android_vh_free_oplus_binder_struct_handler(void *unused, struct binder_transaction *t)
{
	struct oplus_binder_struct *obs = (struct oplus_binder_struct *)(t->android_vendor_data1);

	if (unlikely(!g_sched_enable) || unlikely(!g_async_ux_enable)) {
		return;
	}
	if (binder_is_sync_mode(t->flags)) {
		return;
	}
	trace_binder_t_obs(t, obs, "free_obs");
	free_oplus_binder_struct(obs);
}

static bool binder_dynamic_enqueue_work_ilocked(struct binder_work *work,
		struct list_head *target_list, bool sync_insert)
{
	struct binder_work *w = NULL;
	struct binder_transaction *t = NULL;
	struct oplus_binder_struct *obs = NULL;
	bool insert = false;
	int i = 0;

	if (unlikely(!g_sched_enable) || unlikely(!g_async_ux_enable)) {
		return false;
	}

	trace_binder_ux_work(work, target_list, NULL, insert, i, "dynamic begin");
	if(insert_limit[NUM_INSERT_ID1] && insert_limit[NUM_INSERT_ID2]) {
		trace_binder_ux_work(work, target_list, NULL, insert, i, "dynamic break");
		return false;
	}
	BUG_ON(target_list == NULL);
	BUG_ON(work->entry.next && !list_empty(&work->entry));

	list_for_each_entry(w, target_list, entry) {
		i++;
		if (i > MAX_UX_IN_LIST) {
			insert = false;
			break;
		}
		if (IS_ERR_OR_NULL(w)) {
			break;
		}

		if (w->type != BINDER_WORK_TRANSACTION) {
			continue;
		}

		t = container_of(w, struct binder_transaction, work);
		if (IS_ERR_OR_NULL(t)) {
			break;
		}
		if (sync_insert) {
			if (!binder_is_sync_mode(t->flags)) {
				continue;
			}
			if (!t->from) {
				continue;
			}
			if ((test_task_ux(t->from->task) || test_task_is_rt(t->from->task))) {
				continue;
			}
			binder_ux_state_systrace(current, NULL, STATE_SYNC_INSERT_QUEUE, LOG_BINDER_SYSTRACE_LVL0);
		} else {
			if (binder_is_sync_mode(t->flags)) {
				continue;
			}
			obs = get_oplus_binder_struct(t, false);
			if (is_obs_valid(obs) != OBS_VALID) {
				insert = true;
				break;
			}
			if (obs->async_ux_enable) {
				continue;
			}
			binder_ux_state_systrace(current, NULL, STATE_ASYNC_INSERT_QUEUE, LOG_BINDER_SYSTRACE_LVL0);
		}

		insert = true;
		break;
	}

	if (insert && !IS_ERR_OR_NULL(w) && !IS_ERR_OR_NULL(&w->entry)) {
		list_add(&work->entry, &w->entry);
		if(!insert_limit[NUM_INSERT_ID1] && (t->debug_id != insert_limit[NUM_INSERT_ID2])) {
			insert_limit[NUM_INSERT_ID1] = t->debug_id;
		} else if (!insert_limit[NUM_INSERT_ID2] && (t->debug_id != insert_limit[NUM_INSERT_ID1])) {
			insert_limit[NUM_INSERT_ID2] = t->debug_id;
		}
	} else {
		list_add_tail(&work->entry, target_list);
	}
	trace_binder_ux_work(work, target_list, IS_ERR_OR_NULL(w) ? NULL : &w->entry, insert, i, "dynamic end");
	return true;
}

static void android_vh_binder_special_task_handler(void *unused, struct binder_transaction *t,
	struct binder_proc *proc, struct binder_thread *thread, struct binder_work *w,
	struct list_head *target_list, bool sync, bool *enqueue_task)
{
	struct oplus_binder_struct *obs = NULL;
	bool allow_sync_insert = false;

	if (unlikely(!g_sched_enable) || unlikely(!g_async_ux_enable)
		|| unlikely(!async_insert_queue)) {
		return;
	}

	if (sync) {
		/* called by binder_proc_transaction() when no binder_thread selected */
		if (sync_insert_queue && t && proc && (&proc->todo == target_list)) {
			if(proc->tsk && t->from
				&& (test_set_inherit_ux(t->from->task) || test_task_is_rt(t->from->task))
				&& !strncmp(proc->tsk->comm, SYSTEM_SERVER_NAME, TASK_COMM_LEN)) {
				allow_sync_insert = true;
				goto dynamic_enqueue;
			}
		}
		return;
	}

	if (unlikely(!async_insert_queue)) {
		return;
	}

	if (!w || !target_list) {
		return;
	}

	if (!t && w) {
		t = container_of(w, struct binder_transaction, work);
	}
	obs = get_oplus_binder_struct(t, false);
	if (is_obs_valid(obs) != OBS_VALID) {
		return;
	}
dynamic_enqueue:
	if ((obs && obs->async_ux_enable == ASYNC_UX_ENABLE_INSERT_QUEUE) || allow_sync_insert) {
		if (binder_dynamic_enqueue_work_ilocked(w, target_list, allow_sync_insert)) {
			/*
			if enqueue_task == false, binder_dynamic_enqueue_work_ilocked list_add_xxx is called,
			don't call binder.c binder_enqueue_work_ilocked() again.
			*/
			*enqueue_task = false;
		}
	}
}

#if IS_ENABLED(CONFIG_OPLUS_FEATURE_SCHED_ASSIST)

static bool sync_mode_check_ux(struct binder_proc *proc,
		struct binder_transaction *t, struct task_struct *binder_th_task, bool sync)
{
	struct task_struct *binder_proc_task = proc->tsk;
	bool set_ux = true;

	if (unlikely(!g_sched_enable)) {
		return false;
	}

	if (!strncmp(binder_proc_task->comm, "servicemanager", TASK_COMM_LEN)
				|| !strncmp(binder_proc_task->comm, "hwservicemanage", TASK_COMM_LEN)) {
		binder_set_inherit_ux(binder_proc_task, current, sync);
		trace_binder_proc_thread(binder_proc_task, binder_th_task, sync, INVALID_VALUE, t, proc,
			"sync_ux set servicemg");
	}

	if (!binder_th_task)
		return false;

	trace_binder_proc_thread(binder_proc_task, binder_th_task, sync, INVALID_VALUE, t, proc,
		"sync_ux set ux");

	return set_ux;
}

#define CHECK_MAX_NODE_FOR_ASYNC_THREAD		400
static struct task_struct *get_current_async_thread(struct binder_transaction *t, struct binder_proc *proc)
{
	struct rb_node *n = NULL;
	struct binder_node *node = NULL;
	struct binder_thread *thread = NULL;
	struct oplus_task_struct *ots = NULL;
	ktime_t time = 0;
	int count = 0;
	bool has_async = true;
	static int get_null_count = 0;
	static int get_thread_count = 0;

	if (unlikely(!g_set_last_async_ux)) {
		return NULL;
	}
	if (proc->max_threads <= 0) {
		return NULL;
	}
	if (t && t->buffer) {
		node = t->buffer->target_node;
	}
	if (!node) {
		return NULL;
	}
	if (g_sched_debug & LOG_GET_LAST_ASYNC) {
		time = ktime_get();
	}
	for (n = rb_first(&proc->threads); n != NULL; n = rb_next(n)) {
		thread = rb_entry(n, struct binder_thread, rb_node);
		if (thread->task) {
			ots = get_oplus_task_struct(thread->task);
			if (!IS_ERR_OR_NULL(ots) && (ots->binder_thread_node == node)) {
				if (g_sched_debug & LOG_GET_LAST_ASYNC) {
					time = ktime_get() - time;
					get_thread_count++;
				}
				trace_get_async_thread(proc, thread, count, NULL, node, has_async, time, "async_thread got");
				oplus_binder_debug(LOG_GET_LAST_ASYNC, "proc(pid:%d tgid:%d comm:%s) thread(pid:%d tgid:%d comm:%s) \
					max_threads:%d request:%d started:%d count:%d node:0x%llx time:%lldns got get_thread: %d get_null: %d\n",
					proc ? proc->tsk->pid : 0,
					proc ? proc->tsk->tgid : 0,
					proc ? proc->tsk->comm : "null",
					thread ? thread->task->pid : 0,
					thread ? thread->task->tgid : 0,
					thread ? thread->task->comm : "null",
					proc ? proc->max_threads : 0,
					proc ? proc->requested_threads : 0,
					proc ? proc->requested_threads_started : 0,
					count, (unsigned long long)node, time, get_thread_count, get_null_count);
				return thread->task;
			}
		}
		if (node->has_async_transaction == false) {
			has_async = false;
			break;
		}
		count++;
		if (count > CHECK_MAX_NODE_FOR_ASYNC_THREAD) {
			break;
		}
		if ((g_sched_debug & LOG_GET_LAST_ASYNC) && !IS_ERR_OR_NULL(ots) && thread->task) {
			trace_get_async_thread(proc, thread, count, ots->binder_thread_node, node,
				has_async, time, "async_thread search");
			oplus_binder_debug(LOG_TRACK_LAST_ASYNC, "proc(pid:%d tgid:%d comm:%s) thread(pid:%d tgid:%d comm:%s) \
				max_threads:%d request:%d started:%d count:%d ots_node:0x%llx node:0x%llx time:%lldns get_null: %d\n",
				proc ? proc->tsk->pid : 0,
				proc ? proc->tsk->tgid : 0,
				proc ? proc->tsk->comm : "null",
				thread ? thread->task->pid : 0,
				thread ? thread->task->tgid : 0,
				thread ? thread->task->comm : "null",
				proc ? proc->max_threads : 0,
				proc ? proc->requested_threads : 0,
				proc ? proc->requested_threads_started : 0,
				count, (unsigned long long)(ots->binder_thread_node),
				(unsigned long long)node, time, get_null_count);
		}
	}
	if (g_sched_debug & LOG_GET_LAST_ASYNC) {
		time = ktime_get() - time;
		get_null_count++;
	}
	trace_get_async_thread(proc, thread, count, NULL, node, has_async, time, "async_thread get null");
	oplus_binder_debug(LOG_GET_LAST_ASYNC, "proc(pid:%d tgid:%d comm:%s) max_threads:%d request:%d \
		started:%d count:%d node:0x%llx has_async:%d time:%lldns end get_null: %d, get_thread: %d\n",
		proc ? proc->tsk->pid : 0,
		proc ? proc->tsk->tgid : 0,
		proc ? proc->tsk->comm : "null",
		proc ? proc->max_threads : 0,
		proc ? proc->requested_threads : 0,
		proc ? proc->requested_threads_started : 0,
		count, (unsigned long long)node, has_async, time,
		get_null_count, get_thread_count);
	return NULL;
}

/* need to double check whether set the same task ux twice is ok or not */
static bool check_sf_thread_async_ux(struct task_struct *task,
	struct binder_transaction *t, struct oplus_binder_struct *obs)
{
	struct oplus_task_struct *ots = NULL;
	struct task_struct *grp_leader = NULL;
	static int sf_async_ux = 0, sf_sync_ux = 0;
	bool set_sync_ux = false;

	if (IS_ERR_OR_NULL(task)) {
		return false;
	}

	/* old begin */
		ots = get_oplus_task_struct(current);
	grp_leader = task->group_leader;
		if (grp_leader && !IS_ERR_OR_NULL(ots)) {
			if ((ots->im_flag == IM_FLAG_SURFACEFLINGER) && test_task_ux(grp_leader)) {
			if (is_obs_valid(obs) == OBS_VALID &&
				(obs->async_ux_enable > ASYNC_UX_DISABLE && obs->async_ux_enable < ASYNC_UX_ENABLE_MAX)) {
				set_sync_ux = false;
				if (g_sched_debug & LOG_SET_SF_UX) {
					sf_async_ux++;
				}
				trace_binder_ux_task(false, INVALID_VALUE, set_sync_ux, task, INVALID_VALUE,
					t, NULL, "async_ux set SF async");
				oplus_binder_debug(LOG_SET_SF_UX, "async current(%d) task(pid:%d tgid:%d comm:%s) sf_async:%d sf_sync: %d\n",
					current->pid, task->pid, task->tgid, task->comm, sf_async_ux, sf_sync_ux);
			} else {
				set_sync_ux = true;
				if (g_sched_debug & LOG_SET_SF_UX) {
					sf_sync_ux++;
				}
				trace_binder_ux_task(false, INVALID_VALUE, set_sync_ux, task, INVALID_VALUE,
					t, NULL, "async_ux set SF sync");
				oplus_binder_debug(LOG_SET_SF_UX, "sync current(%d) ux_task(pid:%d tgid:%d comm:%s) sf_async:%d sf_sync: %d\n",
					current->pid, task->pid, task->tgid, task->comm, sf_async_ux, sf_sync_ux);
			}
		}
	}
	/* old end */

	return set_sync_ux;
}
static bool async_mode_check_ux(struct binder_proc *proc, struct binder_transaction *t,
		struct task_struct *binder_th_task, bool sync, bool pending_async,
		struct task_struct **last_task, bool *force_sync)
{
	struct oplus_binder_struct *obs = NULL;
	struct task_struct *ux_task = binder_th_task;
	bool set_ux = false;
	if (unlikely(!g_sched_enable)) {
		return false;
	}

	if (unlikely(!g_async_ux_enable)) {
		if (check_sf_thread_async_ux(ux_task, t, NULL)) {
			set_ux = true;
			*force_sync = true;
		}
		return set_ux;
	}
	obs = get_oplus_binder_struct(t, false);
	if (check_sf_thread_async_ux(ux_task, t, obs)) {
		set_ux = true;
		*force_sync = true;
		goto end;
	}
	if (is_obs_valid(obs) != OBS_VALID) {
		set_ux = false;
		trace_binder_ux_task(sync, pending_async, set_ux, ux_task, INVALID_VALUE,
			t, obs, "async_ux obs invalid");
		goto end;
	}

	if (obs->async_ux_enable == ASYNC_UX_DISABLE) {
		set_ux = false;
		trace_binder_ux_task(sync, pending_async, set_ux, ux_task, obs->async_ux_enable,
			t, obs, "async_ux not enable");
		goto end;
	}

	if (ux_task) {
		set_ux = true;
		trace_binder_ux_task(sync, pending_async, set_ux, ux_task, obs->async_ux_enable,
			t, obs, "async_ux set ux");
		goto end;
	}

	/* pending_async, no binder_th_task */
	if (pending_async) {
		ux_task = get_current_async_thread(t, proc);
		if (ux_task) {
			*last_task = ux_task;
			set_ux = true;
			binder_ux_state_systrace(current, ux_task, STATE_ASYNC_SET_LAST_UX, LOG_BINDER_SYSTRACE_LVL0);
		} else {
			set_ux = false;
			binder_ux_state_systrace(current, NULL, STATE_ASYNC_NOT_SET_LAST_UX, LOG_BINDER_SYSTRACE_LVL0);
		}
		obs->pending_async = true;
		trace_binder_ux_task(sync, pending_async, set_ux, ux_task, obs->async_ux_enable,
			t, obs, "async_ux set last");
		goto end;
	} else {
		binder_ux_state_systrace(current, NULL, STATE_ASYNC_NO_THREAD_NO_PENDING, LOG_BINDER_SYSTRACE_LVL0);
	}
end:
	trace_binder_ux_task(sync, pending_async, set_ux, ux_task, INVALID_VALUE,
			t, obs, "async_ux end");
	return set_ux;
}


#else /* CONFIG_OPLUS_FEATURE_SCHED_ASSIST */

static bool sync_mode_check_ux(struct binder_proc *proc,
		struct binder_transaction *t, struct task_struct *binder_th_task, bool sync)
{
	return false;
}

static bool async_mode_check_ux(struct binder_proc *proc, struct binder_transaction *t,
	struct task_struct *binder_th_task, bool sync, bool pending_async,
	struct task_struct **last_task, bool *force_sync)
{
	return false;
}

#endif

static void android_vh_binder_proc_transaction_finish_handler(void *unused, struct binder_proc *proc,
		struct binder_transaction *t, struct task_struct *binder_th_task, bool pending_async, bool sync)
{
	struct task_struct *last_task = NULL;
	bool set_ux = false;
	bool force_sync = false;

	if (unlikely(!g_sched_enable))
		return;

	if (!binder_th_task) {
		binder_ux_state_systrace(current, NULL, STATE_NO_BINDER_THREAD, LOG_BINDER_SYSTRACE_LVL0);
	}

	set_binder_thread_node(t, binder_th_task, NULL, sync, false);

	if (sync) {
		set_ux = sync_mode_check_ux(proc, t, binder_th_task, sync);
	} else {
		set_ux = async_mode_check_ux(proc, t, binder_th_task, sync,
			pending_async, &last_task, &force_sync);
	}
	if (set_ux) {
		if (force_sync) {
			binder_set_inherit_ux(binder_th_task, current, true);
		} else if (last_task) {
			binder_set_inherit_ux(last_task, current, sync);
		} else {
			binder_set_inherit_ux(binder_th_task, current, sync);
		}
	}

	if (last_task) {
		trace_binder_ux_task(sync, pending_async, set_ux, last_task,
			INVALID_VALUE, t, NULL, "ux t_finish last");
	} else {
		trace_binder_ux_task(sync, pending_async, set_ux, binder_th_task,
			INVALID_VALUE, t, NULL, "ux t_finish");
	}
}

void register_binder_sched_vendor_hooks(void)
{
	register_trace_android_vh_binder_restore_priority(
		android_vh_binder_restore_priority_handler, NULL);
	register_trace_android_vh_binder_wait_for_work(
		android_vh_binder_wait_for_work_handler, NULL);
	register_trace_android_vh_sync_txn_recvd(
		android_vh_sync_txn_recvd_handler, NULL);
#ifdef CONFIG_OPLUS_BINDER_PRIO_SKIP
	register_trace_android_vh_binder_priority_skip(
		android_vh_binder_priority_skip_handler, NULL);
#endif
	register_trace_android_vh_binder_proc_transaction_finish(
		android_vh_binder_proc_transaction_finish_handler, NULL);
	register_trace_android_vh_binder_special_task(
		android_vh_binder_special_task_handler, NULL);
	register_trace_android_vh_alloc_oem_binder_struct(
		android_vh_alloc_oem_binder_struct_handler, NULL);
	register_trace_android_vh_binder_transaction_received(
		android_vh_binder_transaction_received_handler, NULL);
	register_trace_android_vh_free_oem_binder_struct(
		android_vh_free_oplus_binder_struct_handler, NULL);
	register_trace_android_vh_binder_free_buf(
		android_vh_binder_free_buf_handler, NULL);
}

static void init_oplus_binder_struct(void *ptr)
{
	struct oplus_binder_struct *obs = ptr;

	memset(obs, 0, sizeof(struct oplus_binder_struct));
}

void oplus_binder_sched_init(void)
{
	oplus_binder_struct_cachep = kmem_cache_create("oplus_binder_struct",
		sizeof(struct oplus_binder_struct), 0, SLAB_PANIC|SLAB_ACCOUNT, init_oplus_binder_struct);

	register_binder_sched_vendor_hooks();
}

module_param_named(binder_sched_enable, g_sched_enable, uint, 0660);
module_param_named(binder_sched_debug, g_sched_debug, ullong, 0660);
module_param_named(binder_async_ux_test, async_ux_test, uint, 0660);
module_param_named(binder_ux_enable, g_async_ux_enable, int, 0664);
module_param_named(binder_async_insert_queue, async_insert_queue, int, 0664);
module_param_named(binder_sync_insert_queue, sync_insert_queue, uint, 0664);
module_param_named(binder_set_last_async_ux, g_set_last_async_ux, int, 0664);
module_param_named(binder_set_async_ux_after_pending, g_set_async_ux_after_pending, int, 0664);


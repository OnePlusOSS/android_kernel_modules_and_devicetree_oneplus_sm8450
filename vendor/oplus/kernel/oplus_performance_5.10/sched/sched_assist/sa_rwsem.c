// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020 Oplus. All rights reserved.
 */

#include "sched_assist.h"
#include "sa_common.h"
#include "sa_rwsem.h"

#include <linux/sched.h>
#include <linux/list.h>
#include <linux/rwsem.h>
#include <kernel/sched/sched.h>

#define RWSEM_READER_OWNED	(1UL << 0)
#define RWSEM_RD_NONSPINNABLE	(1UL << 1)
#define RWSEM_WR_NONSPINNABLE	(1UL << 2)
#define RWSEM_NONSPINNABLE	(RWSEM_RD_NONSPINNABLE | RWSEM_WR_NONSPINNABLE)
#define RWSEM_OWNER_FLAGS_MASK	(RWSEM_READER_OWNED | RWSEM_NONSPINNABLE)

#define RWSEM_WRITER_LOCKED	(1UL << 0)
#define RWSEM_WRITER_MASK	RWSEM_WRITER_LOCKED

static inline struct task_struct *rwsem_owner(struct rw_semaphore *sem)
{
	return (struct task_struct *)
		(atomic_long_read(&sem->owner) & ~RWSEM_OWNER_FLAGS_MASK);
}

static inline bool rwsem_test_oflags(struct rw_semaphore *sem, long flags)
{
	return atomic_long_read(&sem->owner) & flags;
}

static inline bool is_rwsem_reader_owned(struct rw_semaphore *sem)
{
#if IS_ENABLED(CONFIG_DEBUG_RWSEMS)
	/*
	 * Check the count to see if it is write-locked.
	 */
	long count = atomic_long_read(&sem->count);

	if (count & RWSEM_WRITER_MASK)
		return false;
#endif
	return rwsem_test_oflags(sem, RWSEM_READER_OWNED);
}

static void rwsem_list_add_ux(struct list_head *entry, struct list_head *head)
{
	struct list_head *pos = NULL;
	struct list_head *n = NULL;
	struct rwsem_waiter *waiter = NULL;

	list_for_each_safe(pos, n, head) {
		waiter = list_entry(pos, struct rwsem_waiter, list);
#ifdef CONFIG_OPLUS_SYSTEM_KERNEL_QCOM
		if (waiter->task->prio > MAX_RT_PRIO && !test_task_ux(waiter->task)) {
#else
		if (!test_task_ux(waiter->task)) {
#endif
			list_add(entry, waiter->list.prev);
			return;
		}
	}

	if (pos == head)
		list_add_tail(entry, head);
}

bool oplus_rwsem_list_add(struct task_struct *tsk, struct list_head *entry, struct list_head *head)
{
	bool is_ux = false;

	if (unlikely(!global_sched_assist_enabled))
		return false;

	if (!entry || !head)
		return false;

	is_ux = test_task_ux(tsk);
	if (is_ux) {
		rwsem_list_add_ux(entry, head);
		return true;
	}

	return false;
}
EXPORT_SYMBOL(oplus_rwsem_list_add);

int rwsem_max_block_time_ms = 5000;
module_param_named(rwsem_max_block_time_ms, rwsem_max_block_time_ms, int, 0660);
bool waiter_block_too_long(struct rwsem_waiter *waiter)
{
	struct task_struct *tsk = waiter->task;
	struct sched_entity *se;
	struct rq *rq;
	struct rq_flags rf;
	u64 block_start;
	u64 sleep_start;

	if (!tsk)
		return false;
	se = &tsk->se;
	if (!se)
		return false;
	rq = task_rq_lock(tsk, &rf);

	block_start = schedstat_val(se->statistics.block_start);
	sleep_start = schedstat_val(se->statistics.sleep_start);

	if (block_start) {
		u64 delta = (rq_clock(rq) - block_start);
		if ((s64)delta < 0)
			delta = 0;
		delta >>= 20;
		if (delta > rwsem_max_block_time_ms)
			goto stop_boost;
	}

	if (sleep_start) {
		u64 delta = (rq_clock(rq) - sleep_start);
		if ((s64)delta < 0)
                        delta = 0;
		delta >>= 20;
		if (delta > rwsem_max_block_time_ms)
			goto stop_boost;
	}
	task_rq_unlock(rq, tsk, &rf);
	return false;
stop_boost:
	task_rq_unlock(rq, tsk, &rf);
	return true;
}

bool rwsem_list_unux_hang_check(struct rw_semaphore *sem)
{
	struct list_head *pos = NULL;
	struct list_head *n = NULL;
	struct rwsem_waiter *waiter = NULL;

	list_for_each_safe(pos, n, &sem->wait_list) {
		waiter = list_entry(pos, struct rwsem_waiter, list);
		if (test_task_ux(waiter->task))
			continue;
		if (waiter_block_too_long(waiter))
			return true;
		else
			return false;
	}
	return false;
}

bool rwsem_list_add_skip_ux(struct task_struct *in_tsk, struct task_struct *tsk)
{
	if (unlikely(!global_sched_assist_enabled))
		return false;

	if (in_tsk->prio > MAX_RT_PRIO && test_task_ux(tsk))
		return true;

	return false;
}
EXPORT_SYMBOL(rwsem_list_add_skip_ux);

static void rwsem_set_inherit_ux(struct rw_semaphore *sem)
{
	bool is_ux = test_set_inherit_ux(current);
	struct task_struct *owner = rwsem_owner(sem);

	/* set writer as ux task */
	if (is_ux && !is_rwsem_reader_owned(sem) && !test_inherit_ux(owner, INHERIT_UX_RWSEM)) {
		int type = get_ux_state_type(owner);

		if ((type == UX_STATE_NONE) || (type == UX_STATE_INHERIT))
			set_inherit_ux(owner, INHERIT_UX_RWSEM, oplus_get_ux_depth(current), oplus_get_ux_state(current));
	}
}

static void rwsem_unset_inherit_ux(struct rw_semaphore *sem)
{
	if (test_inherit_ux(current, INHERIT_UX_RWSEM))
		unset_inherit_ux(current, INHERIT_UX_RWSEM);
}

/* implement vender hook in kernel/locking/rwsem.c */
void android_vh_alter_rwsem_list_add_handler(void *unused, struct rwsem_waiter *waiter,
			struct rw_semaphore *sem, bool *already_on_list)
{
	/* prevent unux starve */
	if (rwsem_list_unux_hang_check(sem))
		return;
	if (oplus_rwsem_list_add(waiter->task, &waiter->list, &sem->wait_list))
		*already_on_list = true;
}

void android_vh_rwsem_wake_handler(void *unused, struct rw_semaphore *sem)
{
	if (unlikely(!global_sched_assist_enabled))
		return;

	rwsem_set_inherit_ux(sem);
}

void android_vh_rwsem_wake_finish_handler(void *unused, struct rw_semaphore *sem)
{
	if (unlikely(!global_sched_assist_enabled))
		return;

	rwsem_unset_inherit_ux(sem);
}

// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020 Oplus. All rights reserved.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/version.h>
#include <kernel/sched/sched.h>
#include <trace/hooks/cgroup.h>
#include <trace/hooks/signal.h>

#include "sched_assist.h"
#include "sa_common.h"
#include "sa_sysfs.h"
#include "sa_exec.h"
#include "sa_fair.h"
#include "sa_oemdata.h"
#include "sa_priority.h"

#if IS_ENABLED(CONFIG_OPLUS_FEATURE_LOADBALANCE)
#include "sa_balance.h"
#endif

#ifdef CONFIG_LOCKING_PROTECT
#include "sched_assist_locking.h"
#endif

#ifndef CONFIG_OPLUS_SYSTEM_KERNEL_QCOM
/* Ported from walt.c BEGIN */
static void android_rvh_update_cpus_allowed_handler(void *unused, struct task_struct *p,
						cpumask_var_t cpus_requested,
						const struct cpumask *new_mask, int *ret)
{
	struct oplus_task_struct *ots = get_oplus_task_struct(p);
	if (IS_ERR_OR_NULL(ots))
		return;

	if (!cpumask_empty(&ots->cpus_requested) && cpumask_subset(&ots->cpus_requested, cpus_requested)) {
		*ret = set_cpus_allowed_ptr(p, &ots->cpus_requested);
	}
}

static void android_rvh_sched_setaffinity_handler(void *unused, struct task_struct *p,
					  const struct cpumask *in_mask,
					  int *retval)
{
	struct oplus_task_struct *ots;
	/* nothing to do if the affinity call failed */
	if (*retval)
		return;

	ots = get_oplus_task_struct(p);
	if (IS_ERR_OR_NULL(ots))
		return;
	/*
	 * cache the affinity for user space tasks so that they
	 * can be restored during cpuset cgroup change.
	 */
	if (!(p->flags & PF_KTHREAD))
		cpumask_and(&ots->cpus_requested, in_mask, cpu_possible_mask);
}

static int cpu_stop_handler(void *data)
{
	int cpu;
	struct task_struct *g, *p;
	int level = 0;

	read_lock(&tasklist_lock);
	for_each_possible_cpu(cpu) {
		if (level == 0)
			raw_spin_lock(&cpu_rq(cpu)->lock);
		else
			raw_spin_lock_nested(&cpu_rq(cpu)->lock, level);
		level++;
	}

	do_each_thread(g, p) {
		struct oplus_task_struct *ots = get_oplus_task_struct(p);
		if (IS_ERR_OR_NULL(ots))
			continue;
		cpumask_copy(&ots->cpus_requested, &p->cpus_mask);
	} while_each_thread(g, p);

	for_each_possible_cpu(cpu) {
		raw_spin_unlock(&cpu_rq(cpu)->lock);
	}
	read_unlock(&tasklist_lock);
	return 0;
}
/* Ported from walt.c END */
#endif

static int register_scheduler_vendor_hooks(void)
{
	int ret;

	/* register vender hook in kernel/sched/fair.c */
	REGISTER_TRACE_RVH(android_rvh_place_entity, android_rvh_place_entity_handler);
	REGISTER_TRACE_RVH(android_rvh_check_preempt_tick, android_rvh_check_preempt_tick_handler);
	REGISTER_TRACE_RVH(android_rvh_can_migrate_task, android_rvh_can_migrate_task_handler);
#ifndef CONFIG_OPLUS_SYSTEM_KERNEL_QCOM
	REGISTER_TRACE_RVH(android_rvh_post_init_entity_util_avg, android_rvh_post_init_entity_util_avg_handler);
#endif
	/* REGISTER_TRACE_RVH(android_rvh_select_task_rq_fair, android_rvh_select_task_rq_fair_handler); */
	/* REGISTER_TRACE_RVH(android_rvh_find_energy_efficient_cpu, android_rvh_find_energy_efficient_cpu_handler); */
	REGISTER_TRACE_RVH(android_rvh_enqueue_entity, android_rvh_enqueue_entity_handler);
	REGISTER_TRACE_RVH(android_rvh_dequeue_entity, android_rvh_dequeue_entity_handler);

#ifndef CONFIG_OPLUS_SYSTEM_KERNEL_QCOM
	REGISTER_TRACE_RVH(android_rvh_check_preempt_wakeup, android_rvh_check_preempt_wakeup_handler);
	REGISTER_TRACE_RVH(android_rvh_replace_next_task_fair, android_rvh_replace_next_task_fair_handler);
#endif

	/* register vender hook in kernel/sched/topology.c */
	REGISTER_TRACE_VH(android_vh_build_sched_domains, android_vh_build_sched_domains_handler);

	/* register vender hook in  kernel/sched/rt.c */
#ifndef CONFIG_OPLUS_SYSTEM_KERNEL_QCOM
	/* REGISTER_TRACE_RVH(android_rvh_select_task_rq_rt, android_rvh_select_task_rq_rt_handler); */
	REGISTER_TRACE_RVH(android_rvh_find_lowest_rq, android_rvh_find_lowest_rq_handler);
#endif

	/* register vender hook in kernel/sched/core.c */
	REGISTER_TRACE_RVH(android_rvh_sched_fork, android_rvh_sched_fork_handler);
	REGISTER_TRACE_RVH(android_rvh_schedule, android_rvh_schedule_handler);
	REGISTER_TRACE_RVH(android_vh_scheduler_tick, android_vh_scheduler_tick_handler);
#ifdef CONFIG_OPLUS_SYSTEM_KERNEL_QCOM
	REGISTER_TRACE_RVH(android_rvh_enqueue_task, android_rvh_enqueue_task_handler);
	REGISTER_TRACE_RVH(android_rvh_dequeue_task, android_rvh_dequeue_task_handler);
#endif

	/* register vender hook in fs/exec.c */
	REGISTER_TRACE_VH(task_rename, task_rename_handler);

	/* register vendor hook in kernel/cgroup/cgroup-v1.c */
	REGISTER_TRACE_VH(android_vh_cgroup_set_task, android_vh_cgroup_set_task_handler);
	/* register vendor hook in kernel/signal.c  */
	REGISTER_TRACE_VH(android_vh_do_send_sig_info, android_vh_do_send_sig_handler);

	REGISTER_TRACE_VH(sched_stat_runtime, android_vh_sched_stat_runtime_handler);
#ifndef CONFIG_OPLUS_SYSTEM_KERNEL_QCOM
	REGISTER_TRACE_RVH(android_rvh_update_cpus_allowed, android_rvh_update_cpus_allowed_handler);
	REGISTER_TRACE_RVH(android_rvh_sched_setaffinity, android_rvh_sched_setaffinity_handler);
#endif
#ifdef CONFIG_LOCKING_PROTECT
	sched_assist_locking_init();
#endif

#if IS_ENABLED(CONFIG_OPLUS_FEATURE_BAN_APP_SET_AFFINITY)
	/* register vendor hook in kernel/core.c */
	REGISTER_TRACE_VH(android_vh_sched_setaffinity_early, android_vh_sched_setaffinity_early_handler);
#endif

#if IS_ENABLED(CONFIG_OPLUS_FEATURE_LOADBALANCE)
	oplus_loadbalance_init();
#endif

	return 0;
}

#define OPLUS_OEM_DATA_SIZE_TEST(ostruct, kstruct)		\
	BUILD_BUG_ON(sizeof(ostruct) > (sizeof(u64) *		\
		ARRAY_SIZE(((kstruct *)0)->android_oem_data1)))

static int __init oplus_sched_assist_init(void)
{
	int ret;

	/* compile time checks for vendor data size */
	OPLUS_OEM_DATA_SIZE_TEST(struct oplus_rq, struct rq);

	ret = sa_oemdata_init();
	if (ret != 0)
		return ret;

	global_sched_assist_enabled |= FEATURE_COMMON;
#ifdef CONFIG_OPLUS_FEATURE_SCHED_SPREAD
	global_sched_assist_enabled |= FEATURE_SPREAD;
#endif /* CONFIG_OPLUS_FEATURE_SCHED_SPREAD */

	sched_assist_init_oplus_rq();
	update_ux_sched_cputopo();

	ret = oplus_sched_assist_proc_init();
	if (ret != 0)
		return ret;

	ret = register_scheduler_vendor_hooks();
	if (ret != 0)
		return ret;

#ifndef CONFIG_OPLUS_SYSTEM_KERNEL_QCOM
	stop_machine(cpu_stop_handler, NULL, NULL);
#endif
	register_profile_event();
	ux_debug("sched assist init succeed!\n");
	return 0;
}

module_init(oplus_sched_assist_init);

MODULE_DESCRIPTION("Oplus Sched Assist Vender Hooks Driver");
MODULE_LICENSE("GPL v2");

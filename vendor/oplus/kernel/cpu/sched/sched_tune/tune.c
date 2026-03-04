#include <linux/cgroup.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/percpu.h>
#include <linux/printk.h>
#include <linux/rcupdate.h>
#include <linux/slab.h>
#include <linux/sched/clock.h>
#include <linux/sched/cputime.h>
#include <kernel/sched/sched.h>
#include <linux/reciprocal_div.h>
#include <linux/sched/clock.h>

#include "tune.h"

#define ENQUEUE_TASK  1
#define DEQUEUE_TASK -1

static int schedtune_debug_enable;
module_param_named(schedtune_debug_enable, schedtune_debug_enable, uint, 0644);

static struct schedtune root_schedtune = {
	.boost  = 0,
};

/* Array of configured boostgroups */
static struct schedtune *allocated_group[BOOSTGROUPS_COUNT] = {
	&root_schedtune,
	NULL,
};

/* Boost groups affecting each CPU in the system */
DEFINE_PER_CPU(struct boost_groups, cpu_boost_groups);

static bool schedtune_initialized = false;

static inline unsigned long task_util(struct task_struct *p)
{
	return READ_ONCE(p->se.avg.util_avg);
}

static inline struct task_group *css_tg(struct cgroup_subsys_state *css)
{
	return css ? container_of(css, struct task_group, css) : NULL;
}

static inline int oplus_get_stune_idx(struct task_struct *t)
{
	/*android_oem_data1[0] used for oplus_task_struct*/
	return t->android_oem_data1[2];
}
static inline void  oplus_set_stune_idx(struct task_struct *t, int stune_idx)
{
	t->android_oem_data1[2] = stune_idx;
}

static inline bool schedtune_boost_timeout(u64 now, u64 ts)
{
	return ((now - ts) > SCHEDTUNE_BOOST_HOLD_NS);
}

static inline struct schedtune *task_schedtune(struct task_struct *tsk)
{
	struct cgroup_subsys_state *css;
	struct task_group *tg;

	css = task_css(tsk, cpu_cgrp_id);
	tg = css_tg(css);
	return (struct schedtune *)tg->android_kabi_reserved1;
}

int schedtune_task_boost(struct task_struct *p)
{
	struct schedtune *st;
	int task_boost;

	if (unlikely(!schedtune_initialized))
		return 0;

	/* Get task boost value */
	rcu_read_lock();
	st = task_schedtune(p);
	if (!st) {
		rcu_read_unlock();
		return 0;
	}
	task_boost = st->boost;
	rcu_read_unlock();

	return task_boost;
}
EXPORT_SYMBOL_GPL(schedtune_task_boost);

static inline bool schedtune_boost_group_active(int idx,
		struct boost_groups *bg, u64 now)
{
	if (bg->group[idx].tasks)
		return true;

	return !schedtune_boost_timeout(now, bg->group[idx].ts);
}

static void schedtune_cpu_update(int cpu, u64 now)
{
	struct boost_groups *bg = &per_cpu(cpu_boost_groups, cpu);
	int boost_max;
	u64 boost_ts;
	int idx;

	/* The root boost group is always active */
	boost_max = bg->group[0].boost;
	boost_ts = now;
	for (idx = 1; idx < BOOSTGROUPS_COUNT; ++idx) {

		/* Ignore non boostgroups not mapping a cgroup */
		if (!bg->group[idx].valid)
			continue;

		/*
		 * A boost group affects a CPU only if it has
		 * RUNNABLE tasks on that CPU or it has hold
		 * in effect from a previous task.
		 */
		if (!schedtune_boost_group_active(idx, bg, now))
			continue;

		/* This boost group is active */
		if (boost_max > bg->group[idx].boost)
			continue;

		boost_max = bg->group[idx].boost;
		boost_ts =  bg->group[idx].ts;
	}

	/* Ensures boost_max is non-negative when all cgroup boost values
	 * are neagtive. Avoids under-accounting of cpu capacity which may cause
	 * task stacking and frequency spikes.*/
	/*boost_max = max(boost_max, 0);*/
	bg->boost_max = boost_max;
	bg->boost_ts = boost_ts;
}

static int schedtune_boostgroup_update(int idx, int boost)
{
	struct boost_groups *bg;
	int cur_boost_max;
	int old_boost;
	int cpu;
	u64 now;

	/* Update per CPU boost groups */
	for_each_possible_cpu(cpu) {
		bg = &per_cpu(cpu_boost_groups, cpu);

		/* CGroups are never associated to non active cgroups */
		WARN_ON(!bg->group[idx].valid);

		/*
		 * Keep track of current boost values to compute the per CPU
		 * maximum only when it has been affected by the new value of
		 * the updated boost group
		 */
		cur_boost_max = bg->boost_max;
		old_boost = bg->group[idx].boost;

		/* Update the boost value of this boost group */
		bg->group[idx].boost = boost;

		/* Check if this update increase current max */
		now = sched_clock();
		if (boost > cur_boost_max &&
			schedtune_boost_group_active(idx, bg, now)) {
			bg->boost_max = boost;
			bg->boost_ts = bg->group[idx].ts;
			if (unlikely(schedtune_debug_enable))
				trace_printk("cpu:[%d,1],boost_max:%d\n", cpu, bg->boost_max);
			continue;
		}

		/* Check if this update has decreased current max */
		if (cur_boost_max == old_boost && old_boost > boost) {
			schedtune_cpu_update(cpu, now);
			if (unlikely(schedtune_debug_enable))
				trace_printk("cpu:[%d,1],boost_max:%d\n", cpu, bg->boost_max);
			continue;
		}
		if (unlikely(schedtune_debug_enable))
			trace_printk("cpu:[%d,0],boost_max:%d\n", cpu, bg->boost_max);
	}

	return 0;
}

s64 schedtune_boost_read(struct cgroup_subsys_state *css, struct cftype *cft)
{
	struct task_group *tg = css_tg(css);
	struct schedtune *st = (struct schedtune *)tg->android_kabi_reserved1;

	if (!st)
		return -ENOENT;
	return st->boost;
}

int schedtune_boost_write(struct cgroup_subsys_state *css, struct cftype *cft, s64 boost)
{
	struct task_group *tg = css_tg(css);
	struct schedtune *st = (struct schedtune *)tg->android_kabi_reserved1;

	if (!st)
		return -ENOENT;

	if (QOS_SCHED_TUNE_DEFAULT >= boost || boost > 100) {
		pr_err("invalid or default stune boost value %lld\n", boost);
		return -EINVAL;
	}

	/*stune qos reset tag is 0, it's the same with reset boost value, so we don't need
	to handle it specifically*/

	st->boost = boost;
	/* Update CPU boost */
	schedtune_boostgroup_update(st->idx, st->boost);

	return 0;
}

static struct reciprocal_value schedtune_spc_rdiv;

static long schedtune_margin(unsigned long signal, long boost)
{
	long long margin = 0;

	/*
	 * Signal proportional compensation (SPC)
	 *
	 * The Boost (B) value is used to compute a Margin (M) which is
	 * proportional to the complement of the original Signal (S):
	 *   M = B * (SCHED_CAPACITY_SCALE - S)
	 * The obtained M could be used by the caller to "boost" S.
	 */
	if (boost >= 0) {
		margin  = SCHED_CAPACITY_SCALE - signal;
		margin *= boost;
	} else
		margin = -signal * boost;

	margin  = reciprocal_divide(margin, schedtune_spc_rdiv);
	if (boost < 0)
		margin *= -1;
	return margin;
}

static int schedtune_cpu_boost_with(int cpu, struct task_struct *p)
{
	struct boost_groups *bg;
	u64 now;
	int task_boost = p ? schedtune_task_boost(p) : 0;

	bg = &per_cpu(cpu_boost_groups, cpu);
	now = sched_clock();

	/* Check to see if we have a hold in effect */
	if (schedtune_boost_timeout(now, bg->boost_ts))
		schedtune_cpu_update(cpu, now);
	return task_boost;
	//return max(bg->boost_max, task_boost);
}

static inline long schedtune_cpu_margin_with(unsigned long util, int cpu,
		struct task_struct *p)
{
	int boost = schedtune_cpu_boost_with(cpu, p);
	long margin;

	if (boost == 0)
		margin = 0;
	else
		margin = schedtune_margin(util, boost);

	return margin;
}

unsigned long schedtune_task_util(struct task_struct *p)
{
	unsigned long util = task_util(p);
	long margin = schedtune_cpu_margin_with(util, task_cpu(p), p);

	return util + margin;
}
EXPORT_SYMBOL(schedtune_task_util);

noinline unsigned long stune_util(int cpu, unsigned long other_util,
		 unsigned long util)
{
	unsigned long u_util = min_t(unsigned long, SCHED_CAPACITY_SCALE,
				   util + other_util);
	long margin = schedtune_cpu_margin_with(u_util, cpu, cpu_curr(cpu));
	if (unlikely(schedtune_debug_enable))
		trace_printk("cpu:%d, util:%ld, margin:%ld\n", cpu, u_util, margin);

	return u_util + margin;
}
EXPORT_SYMBOL(stune_util);

static inline bool schedtune_update_timestamp(struct task_struct *p)
{
	return task_has_rt_policy(p);
}

static inline void schedtune_tasks_update(struct task_struct *p, int cpu,
			int idx, int task_count)
{
	struct boost_groups *bg = &per_cpu(cpu_boost_groups, cpu);
	int tasks = bg->group[idx].tasks + task_count;

	/* Update boosted tasks count while avoiding to make it negative */
	bg->group[idx].tasks = max(0, tasks);

	/* Update timeout on enqueue */
	if (task_count > 0) {
		u64 now = sched_clock();

		if (schedtune_update_timestamp(p))
			bg->group[idx].ts = now;

		/* Boost group activation or deactivation on that RQ */
		if (bg->group[idx].tasks == 1)
			schedtune_cpu_update(cpu, now);
	}
	if (unlikely(schedtune_debug_enable))
		trace_printk("task:[%d,%s,%d],idx:%d,boost:%d, boost_max:%d,ts:%lld\n",
			p->pid, p->comm, cpu, idx, bg->group[idx].boost, bg->boost_max, bg->group[idx].ts);
}

/*
 * NOTE: This function must be called while holding the lock on the CPU RQ
 */
void schedtune_enqueue_task(struct task_struct *p, int cpu)
{
	struct boost_groups *bg = &per_cpu(cpu_boost_groups, cpu);
	unsigned long irq_flags;
	int idx;

	if (unlikely(!schedtune_initialized))
		return;

	/*
	 * Boost group accouting is protected by a per-cpu lock and requires
	 * interrupt to be disabled to avoid race conditions for example on
	 * do_exit()::cgroup_exit() and task migration.
	 */
	raw_spin_lock_irqsave(&bg->lock, irq_flags);

	idx = oplus_get_stune_idx(p);
	if (idx >= BOOSTGROUPS_COUNT)
		goto done;

	schedtune_tasks_update(p, cpu, idx, ENQUEUE_TASK);

done:
	raw_spin_unlock_irqrestore(&bg->lock, irq_flags);
}

/*
 * NOTE: This function must be called while holding the lock on the CPU RQ
 */
void schedtune_dequeue_task(struct task_struct *p, int cpu)
{
	struct boost_groups *bg = &per_cpu(cpu_boost_groups, cpu);
	unsigned long irq_flags;
	int idx;

	if (unlikely(!schedtune_initialized))
		return;

	/*
	 * Boost group accouting is protected by a per-cpu lock and requires
	 * interrupt to be disabled to avoid race conditions on...
	 */
	raw_spin_lock_irqsave(&bg->lock, irq_flags);

	idx = oplus_get_stune_idx(p);
	if (idx >= BOOSTGROUPS_COUNT)
		goto done;

	schedtune_tasks_update(p, cpu, idx, DEQUEUE_TASK);

done:
	raw_spin_unlock_irqrestore(&bg->lock, irq_flags);
}

static void schedtune_boostgroup_init(struct schedtune *st, int idx)
{
	struct boost_groups *bg;
	int cpu;

	/* Initialize per CPUs boost group support */
	for_each_possible_cpu(cpu) {
		bg = &per_cpu(cpu_boost_groups, cpu);
		bg->group[idx].boost = 0;
		bg->group[idx].valid = true;
		bg->group[idx].ts = 0;
	}

	/* Keep track of allocated boost groups */
	allocated_group[idx] = st;
	st->idx = idx;
}

static void schedtune_boostgroup_release(struct schedtune *st)
{
	struct boost_groups *bg;
	int cpu;

	/* Reset per CPUs boost group support */
	for_each_possible_cpu(cpu) {
		bg = &per_cpu(cpu_boost_groups, cpu);
		bg->group[st->idx].valid = false;
		bg->group[st->idx].boost = 0;
	}

	/* Keep track of allocated boost groups */
	allocated_group[st->idx] = NULL;
}

void schedtune_root_alloc(void)
{
	root_task_group.android_kabi_reserved1 = (u64)&root_schedtune;
}

int schedtune_alloc(struct task_group *tg, struct cgroup_subsys_state *parent_css)
{
	struct schedtune *st;
	int idx;

	/* Allow only single level hierachies */
	if (parent_css != &root_task_group.css) {
		pr_err("Nested SchedTune boosting groups not allowed\n");
		return -EFAULT;
	}

	/* Allow only a limited number of boosting groups */
	for (idx = 1; idx < BOOSTGROUPS_COUNT; idx++) {
		if (!allocated_group[idx])
			break;
	}
	if (idx == BOOSTGROUPS_COUNT) {
		pr_err("Trying to create more than %d SchedTune boosting groups\n",
				   BOOSTGROUPS_COUNT);
		return -EFAULT;
	}
	st = kzalloc(sizeof(*st), GFP_ATOMIC);
	if (!st)
			return -ENOMEM;
	tg->android_kabi_reserved1 = (u64)st;
	schedtune_boostgroup_init(st, idx);
	return 0;
}

void schedtune_free(struct cgroup_subsys_state *css)
{
	struct task_group *tg = css_tg(css);
	struct schedtune *st;

	st = (struct schedtune *)tg->android_kabi_reserved1;
	if (!st)
		return;
	/* Release per CPUs boost group support */
	schedtune_boostgroup_release(st);
	kfree(st);
}

void schedtune_attach(struct task_struct *task)
{
	struct rq *rq;
	struct rq_flags rq_flags;
	struct boost_groups *bg;
	unsigned int cpu;
	u64 now;
	struct schedtune *st;
	int tasks;
	int src_idx; /* Source boost group index */
	int dst_idx; /* Destination boost group index */

	rq = task_rq_lock(task, &rq_flags);
	cpu = cpu_of(rq);
	bg = &per_cpu(cpu_boost_groups, cpu);
	raw_spin_lock(&bg->lock);
	st = task_schedtune(task);
	if (st)
		dst_idx = st->idx;
	else
		dst_idx = 0;

	src_idx = oplus_get_stune_idx(task);
	if (unlikely(dst_idx == src_idx)) {
		goto done;
	}
	oplus_set_stune_idx(task, dst_idx);
	tasks = bg->group[src_idx].tasks - 1;
	bg->group[src_idx].tasks = max(0, tasks);
	bg->group[dst_idx].tasks += 1;
	/* Update boost hold start for this group */
	now = sched_clock();
	bg->group[dst_idx].ts = now;
	/* Force boost group re-evaluation at next boost check */
	bg->boost_ts = now - SCHEDTUNE_BOOST_HOLD_NS;
done:
	raw_spin_unlock(&bg->lock);
	task_rq_unlock(rq, task, &rq_flags);
}

static inline  void schedtune_init_cgroups(void)
{
	struct boost_groups *bg;
	int cpu;

	/* Initialize the per CPU boost groups */
	for_each_possible_cpu(cpu) {
		bg = &per_cpu(cpu_boost_groups, cpu);
		memset(bg, 0, sizeof(struct boost_groups));
		bg->group[0].valid = true;
		raw_spin_lock_init(&bg->lock);
	}
	pr_info("schedtune: configured to support %d boost groups",
		BOOSTGROUPS_COUNT);
	schedtune_initialized = true;
}

/*
 * Initialize the cgroup structures
 */
static int schedtune_init(void)
{
	schedtune_spc_rdiv = reciprocal_value(100);
	schedtune_init_cgroups();

	return 0;
}

postcore_initcall(schedtune_init);

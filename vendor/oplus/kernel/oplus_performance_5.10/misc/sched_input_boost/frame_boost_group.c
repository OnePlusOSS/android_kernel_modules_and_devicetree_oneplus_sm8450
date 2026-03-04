#include <linux/sched.h>
#include <linux/sched/walt.h>
#include <linux/seq_file.h>
#include <linux/sched/cpufreq.h>
#include <trace/events/sched.h>
#include <linux/reciprocal_div.h>
#include <trace/hooks/cgroup.h>
#include <../../../drivers/android/binder_internal.h>
#include "walt.h"
#include "frame_boost_group.h"
#include "frame_info.h"
#include "trace.h"
#include "frame_boost_binder.h"

#if IS_ENABLED(CONFIG_OPLUS_FEATURE_SCHED_ASSIST)
#include <../kernel/oplus_perf_sched/sched_assist/sa_common.h>
#endif

struct task_struct *ui = NULL;
struct task_struct *render = NULL;
struct task_struct *hwtask1 = NULL;
struct task_struct *hwtask2 = NULL;
struct frame_boost_group default_frame_boost_group;
atomic_t start_frame = ATOMIC_INIT(1);
int stune_boost = 20;
int need_spb = 0;
DEFINE_RAW_SPINLOCK(fbg_lock);
int min_clusters = 2;
char lc[] = "ndroid.launcher";
char dy[] = "droid.ugc.aweme";
char ks[] = ".smile.gifmaker";
char douyu[] = "v.douyu.android";

extern bool ishighfps;
extern bool use_vload;
extern unsigned long cpu_util_without(int cpu, struct task_struct *p);
extern int sysctl_slide_boost_enabled;
extern int sysctl_input_boost_enabled;
extern bool up_migrate;
extern u64 last_migrate_time;

#define DEFAULT_ROLL_OVER_INTERVAL 4000000 /* ns */
#define DEFAULT_FREQ_UPDATE_INTERVAL 2000000  /* ns */
#define DEFAULT_UTIL_INVALID_INTERVAL 48000000 /* ns */
#define DEFAULT_UTIL_UPDATE_TIMEOUT 20000000  /* ns */
#define DEFAULT_GROUP_RATE 60 /* 60FPS */
#define ACTIVE_TIME  5000000000/* ns */

#define DEFAULT_TRANS_DEPTH (2)
#define DEFAULT_MAX_THREADS (6)
#define STATIC_FBG_DEPTH (-1)

bool isUi(struct task_struct *p)
{
	if (ui == NULL)
		return false;
	return p == ui;
}

struct frame_boost_group * frame_grp(void)
{
	return &default_frame_boost_group;
}

/*
* frame boost group load tracking
*/
void sched_set_group_window_size(unsigned int window)
{
	struct frame_boost_group *grp = &default_frame_boost_group;
	unsigned long flag;

	if (!grp) {
		pr_err("set window size for group %d fail\n");
		return;
	}

	raw_spin_lock_irqsave(&grp->lock, flag);

	grp->window_size = window;

	raw_spin_unlock_irqrestore(&grp->lock, flag);
}

struct frame_boost_group *task_frame_boost_group(struct task_struct *p)
{
	struct walt_task_struct *wts = (struct walt_task_struct *) p->android_vendor_data1;
	return rcu_dereference(wts->fbg);
}

#define DIV64_U64_ROUNDUP(X, Y) div64_u64((X) + (Y - 1), Y)

static inline u64 scale_exec_time(u64 delta, struct rq *rq)
{
	struct walt_rq *wrq = (struct walt_rq *) rq->android_vendor_data1;
	u64 task_exec_scale;
	int cpu = cpu_of(rq);
	unsigned long curr_cap = arch_scale_freq_capacity(cpu);
	unsigned int curr_freq = (curr_cap * (u64) wrq->cluster->max_possible_freq) >>
		SCHED_CAPACITY_SHIFT;

	task_exec_scale = DIV64_U64_ROUNDUP(curr_freq *
				arch_scale_cpu_capacity(cpu),
				wrq->cluster->max_possible_freq);

	trace_task_exec_scale(cpu, task_exec_scale, curr_freq, arch_scale_cpu_capacity(cpu), wrq->cluster->max_possible_freq);

	return (delta * task_exec_scale) >> 10;
}

static int account_busy_for_group_demand(struct task_struct *p, int event, struct rq *rq, struct frame_boost_group *grp)
{
	/* No need to bother updating task demand for the idle task. */
	if (is_idle_task(p))
		return 0;

	if (event == TASK_WAKE && (grp->nr_running == 0))
		return 0;

	/*
	 * The idle exit time is not accounted for the first task _picked_ up to
	 * run on the idle CPU.
	 */
	if (event == PICK_NEXT_TASK && rq->curr == rq->idle)
		return 0;

	/*
	 * TASK_UPDATE can be called on sleeping task, when its moved between
	 * related groups
	 */
	if (event == TASK_UPDATE) {
		if (rq->curr == p)
			return 1;

		return p->on_rq;
	}

	return 1;
}

void update_group_nr_running(struct task_struct *p, int event, u64 wallclock)
{
	struct frame_boost_group *grp = NULL;

	if (event == IRQ_UPDATE)
		return;

	rcu_read_lock();

	grp = task_frame_boost_group(p);
	if (!grp) {
		rcu_read_unlock();
		return;
	}

	raw_spin_lock(&grp->lock);

	if (event == PICK_NEXT_TASK)
		grp->nr_running++;
	else if (event == PUT_PREV_TASK)
		grp->nr_running--;

	raw_spin_unlock(&grp->lock);
	rcu_read_unlock();
}

static void add_to_group_time(struct frame_boost_group *grp, struct rq *rq, u64 wallclock)
{
	u64 delta_exec, delta_scale;
	u64 mark_start = grp->mark_start;
	u64 window_start = grp->window_start;
	struct walt_rq *wrq = (struct walt_rq *) rq->android_vendor_data1;

	if (unlikely(wallclock <= mark_start))
		return;

	/* per group load tracking in FBG */
	if (likely(mark_start >= window_start)) {
		/*
		*    ws   ms   wc
		*    |    |    |
		*    V    V    V
		*  --|----|----|--
		*/
		delta_exec = wallclock - mark_start;
		grp->time.curr_window_exec += delta_exec;

		delta_scale = scale_exec_time(delta_exec, rq);
		grp->time.curr_window_scale += delta_scale;
	} else {
		/*
		*    ms   ws   wc
		*    |    |    |
		*    V    V    V
		*  --|----|----|--
		*/

		/* prev window group statistic */
		delta_exec = window_start - mark_start;
		grp->time.prev_window_exec += delta_exec;

		delta_scale = scale_exec_time(delta_exec, rq);
		grp->time.prev_window_scale += delta_scale;

		/* curr window group statistic */
		delta_exec = wallclock - window_start;
		grp->time.curr_window_exec += delta_exec;

		delta_scale = scale_exec_time(delta_exec, rq);
		grp->time.curr_window_scale += delta_scale;
	}
	trace_add_group_delta(wallclock, mark_start, window_start, delta_exec, delta_scale, wrq->task_exec_scale);
}

static inline void add_to_group_demand(struct frame_boost_group *grp, struct rq *rq, struct task_struct *p, u64 wallclock)
{
	if (unlikely(wallclock <= grp->window_start))
		return;

	add_to_group_time(grp, rq, wallclock);
}

void update_group_demand(struct task_struct *p, struct rq *rq, int event, u64 wallclock)
{
	struct frame_boost_group *grp = NULL;
	struct walt_task_struct *wts = (struct walt_task_struct *) p->android_vendor_data1;
	struct walt_rq *wrq = (struct walt_rq *) rq->android_vendor_data1;

	if (event == IRQ_UPDATE)
		return;


	rcu_read_lock();

	grp = task_frame_boost_group(p);
	if (!grp) {
		rcu_read_unlock();
		return;
	}

	raw_spin_lock(&grp->lock);

	/* we must update mark_start whether group demand is busy or not */
	if (!account_busy_for_group_demand(p, event, rq, grp)) {
		goto done;
	}


	if (grp->nr_running == 1)
		grp->mark_start = max(grp->mark_start, wts->mark_start);

	add_to_group_demand(grp, rq, p, wallclock);

done:
	grp->mark_start = wallclock;
	raw_spin_unlock(&grp->lock);
	rcu_read_unlock();
	trace_add_group_demand(p, event, grp->time.curr_window_scale, wrq->task_exec_scale, grp->nr_running);
}

void group_time_rollover(struct group_time *time)
{
	time->prev_window_scale = time->curr_window_scale;
	time->curr_window_scale = 0;
	time->prev_window_exec = time->curr_window_exec;
	time->curr_window_exec = 0;
}

int sched_set_group_window_rollover(void)
{
	u64 wallclock;
	unsigned long flag;
	u64 window_size = 0;
	struct frame_boost_group *grp = &default_frame_boost_group;

	raw_spin_lock_irqsave(&grp->lock, flag);

	wallclock =  walt_ktime_get_ns();
	window_size = wallclock - grp->window_start;
	if (window_size < DEFAULT_ROLL_OVER_INTERVAL) {
		raw_spin_unlock_irqrestore(&grp->lock, flag);
		return 0;
	}
	grp->prev_window_size = window_size;
	grp->window_start = wallclock;

	group_time_rollover(&grp->time);
	raw_spin_unlock_irqrestore(&grp->lock, flag);
	if (use_vload) {
		use_vload = false;
	}
	if (up_migrate) {
		up_migrate = false;
	}

	return 0;
}

static unsigned long capacity_spare_without(int cpu, struct task_struct *p)
{
	return max_t(long, capacity_of(cpu) - cpu_util_without(cpu, p), 0);
}

static inline unsigned long boosted_task_util(struct task_struct *task)
{
	unsigned long util = task_util_est(task);
	long margin = schedtune_grp_margin(util);

	trace_sched_boost_task(task, util, margin);
	return util + margin;
}

static inline bool task_demand_fits(struct task_struct *p, int cpu)
{
	unsigned long capacity = capacity_orig_of(cpu);
	unsigned long boosted_util = boosted_task_util(p);
	unsigned int margin = 1024;

	return (capacity << SCHED_CAPACITY_SHIFT) >=
				(boosted_util * margin);
}

static inline bool frame_task_fits_max(struct task_struct *p, int cpu)
{
	unsigned long capacity = capacity_orig_of(cpu);
	unsigned long max_capacity = cpu_rq(cpu)->rd->max_cpu_capacity;
	unsigned long task_boost = per_task_boost(p);
	cpumask_t allowed_cpus;
	int allowed_cpu;

	if (capacity == max_capacity)
		return true;

	if (task_boost > TASK_BOOST_ON_MID)
		return false;

	if (task_demand_fits(p, cpu)) {
		return true;
	}

	/* Now task does not fit. Check if there's a better one. */
	cpumask_and(&allowed_cpus, &p->cpus_mask, cpu_online_mask);
	for_each_cpu(allowed_cpu, &allowed_cpus) {
		if (capacity_orig_of(allowed_cpu) > capacity)
			return false; /* Misfit */
	}

	/* Already largest capacity in allowed cpus. */
	return true;
}

struct cpumask *find_rtg_target(struct task_struct *p)
{
	struct frame_boost_group *grp = NULL;
	struct walt_sched_cluster *preferred_cluster = NULL;
	struct cpumask *rtg_target = NULL;
	struct walt_sched_cluster *prime_cluster = NULL;
	int prime_cpu = -1;

	if (num_sched_clusters < min_clusters)
		return NULL;

	rcu_read_lock();
	grp = task_frame_boost_group(p);
	rcu_read_unlock();

	if (!grp)
		return NULL;

	preferred_cluster = grp->preferred_cluster;
	if (!preferred_cluster)
		return NULL;

	rtg_target = &preferred_cluster->cpus;
	if (!frame_task_fits_max(p, cpumask_first(rtg_target))) {
		rtg_target = NULL;
	}

	prime_cluster = sched_cluster[num_sched_clusters -1];
	if(!prime_cluster)
		return NULL;

	prime_cpu = cpumask_first(&prime_cluster->cpus);
	if (!rtg_target && (preferred_cluster->id == 1)) {
		if (frame_task_fits_max(p, prime_cpu))
			rtg_target = &prime_cluster->cpus;
		else
			rtg_target = &preferred_cluster->cpus;
	}

	return rtg_target;
}

#if IS_ENABLED(CONFIG_OPLUS_FEATURE_SCHED_ASSIST)
/* optimize fingerprint scenario */
static bool skip_finger_cpu(struct rq *rq)
{
	struct oplus_rq *orq = NULL;
	struct oplus_task_struct *ots = NULL;
	bool need_skip = false;
	struct list_head *pos;


	raw_spin_lock(&rq->lock);
	orq = (struct oplus_rq *) rq->android_oem_data1;
	if (!list_empty(&orq->ux_list)) {
		list_for_each(pos, &orq->ux_list) {
			ots = container_of(pos, struct oplus_task_struct, ux_entry);
			if (ots->ux_state == SA_TYPE_LISTPICK) {
				need_skip = true;
				break;
			}
		}
	}
	raw_spin_unlock(&rq->lock);

	return need_skip;
}
#endif

bool is_prime_fits(int cpu)
{
	struct task_struct *curr = NULL;
	struct walt_task_struct *wts = NULL;
	struct rq *rq = NULL;
	bool need_skip = false;

	if (cpu < 0 || !cpu_active(cpu))
		return false;

	rq = cpu_rq(cpu);
	curr = rq->curr;
	if (!curr)
		return false;

	wts = (struct walt_task_struct *) curr->android_vendor_data1;

	/* skip this cpu to improve scheduler lantency */
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_SCHED_ASSIST)
	if (oplus_get_im_flag(curr) == IM_FLAG_SURFACEFLINGER ||
		oplus_get_im_flag(curr) == IM_FLAG_RENDERENGINE) {
		need_skip = true;
	} else {
		need_skip = skip_finger_cpu(rq);
	}
#endif
	return ((wts->fbg_depth == 0) && !need_skip);
}

bool need_frame_boost(struct task_struct *p)
{
	struct walt_task_struct *wts = NULL;
	bool is_launch = false;
	if (!p)
		return false;
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_SCHED_ASSIST)
	is_launch = sched_assist_scene(SA_LAUNCH);
#endif

	wts = (struct walt_task_struct *) p->android_vendor_data1;
	return (wts->fbg_depth != 0) &&
	(!is_launch || wts->active_time > ACTIVE_TIME);
}

int find_fbg_cpu(struct task_struct *p)
{
	int max_spare_cap_cpu = -1;
	int active_cpu = -1;
	struct frame_boost_group *grp = NULL;
	struct walt_sched_cluster *preferred_cluster = NULL;
	struct task_struct *curr = NULL;
	struct walt_task_struct *wts = NULL;
	struct rq *rq = NULL;
	int loop = 0;

	rcu_read_lock();

	grp = task_frame_boost_group(p);
	if (grp) {
		int i;
		unsigned long spare_cap = 0, max_spare_cap = 0;
		struct cpumask *preferred_cpus = NULL;
		cpumask_t search_cpus = CPU_MASK_NONE;
		preferred_cluster = grp->preferred_cluster;

		/* full boost, if prime cluster has too many frame task,
		we will step in the function, so select gold cluster */
		if (is_full_throttle_boost() && num_sched_clusters >= min_clusters)
			preferred_cluster = sched_cluster[num_sched_clusters -2];

		do {
			if (!preferred_cluster) {
				rcu_read_unlock();
				return -1;
			}
			preferred_cpus = &preferred_cluster->cpus;

			cpumask_and(&search_cpus, &p->cpus_mask, cpu_active_mask);

			for_each_cpu_and(i, &search_cpus, preferred_cpus) {
				rq = cpu_rq(i);
				curr = rq->curr;
				if (curr) {
					wts = (struct walt_task_struct *) curr->android_vendor_data1;
					if (wts->fbg_depth != 0)
						continue;

					/* skip this cpu to improve scheduler lantency*/
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_SCHED_ASSIST)
					if (oplus_get_im_flag(curr) == IM_FLAG_SURFACEFLINGER ||
						oplus_get_im_flag(curr) == IM_FLAG_RENDERENGINE)
						continue;
					if (oplus_get_im_flag(curr) == IM_FLAG_CAMERA_HAL)
						continue;
					if (test_task_is_rt(curr) &&
						oplus_get_im_flag(curr) == IM_FLAG_HWBINDER)
						continue;
#endif
				}
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_SCHED_ASSIST)
				/* optimize fingerprint scenario */
				if (skip_finger_cpu(rq))
					continue;
#endif

				if (active_cpu == -1)
					active_cpu = i;

				if (is_reserved(i))
					continue;

				if (available_idle_cpu(i) || (i == task_cpu(p) && p->state == TASK_RUNNING)) {
					rcu_read_unlock();
					return i;
				}

				spare_cap = capacity_spare_without(i, p);
				if (spare_cap > max_spare_cap) {
					max_spare_cap = spare_cap;
					max_spare_cap_cpu = i;
				}
			}

			if (num_sched_clusters >= 3 &&
				preferred_cluster->id == num_sched_clusters - 1) {
				loop = 1;
				preferred_cluster = sched_cluster[num_sched_clusters - 2];
			} else {
				loop = 0;
			}
		} while (loop);
	}
	rcu_read_unlock();

	if (max_spare_cap_cpu == -1)
		max_spare_cap_cpu = active_cpu;

	return max_spare_cap_cpu;
}

extern struct reciprocal_value reciprocal_value(u32 d);
struct reciprocal_value schedtune_spc_rdiv;
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

int schedtune_grp_margin(unsigned long util)
{
	if (need_spb)
		goto done;

	if (stune_boost == 0)
		return 0;

	if ((!ishighfps) && !(sysctl_slide_boost_enabled || sysctl_input_boost_enabled))
		return 0;

done:
	return schedtune_margin(util, stune_boost);
}

static struct walt_sched_cluster *best_cluster(struct frame_boost_group *grp)
{
	int cpu;
	unsigned long max_cap = 0, cap = 0;
	struct walt_sched_cluster *cluster = NULL, *max_cluster = NULL;
	unsigned long util = grp->time.normalized_util;
	unsigned long boosted_grp_util = util + schedtune_grp_margin(util);

	if (num_sched_clusters < min_clusters)
		return NULL;

	/* if sched_boost == 1, select prime cluster*/
	if (is_full_throttle_boost()) {
		return sched_cluster[num_sched_clusters -1];
	}

	for_each_sched_cluster(cluster) {
		cpu = cpumask_first(&cluster->cpus);
		cap = capacity_orig_of(cpu);
		if (cap > max_cap) {
			max_cap = cap;
			max_cluster = cluster;
		}
		if (boosted_grp_util <= cap)
			return cluster;
	}

	return max_cluster;
}

static inline bool
group_should_invalid_util(struct frame_boost_group *grp, u64 now)
{
	return (now - grp->last_util_update_time >= grp->util_invalid_interval);
}

bool valid_normalized_util(struct frame_boost_group *grp)
{
	struct task_struct *p = NULL;
	cpumask_t fbg_cpus = CPU_MASK_NONE;
	int cpu;
	struct rq *rq;
	struct walt_task_struct *wts;

	if (grp->nr_running) {
		list_for_each_entry(wts, &grp->tasks, fbg_list) {
			p = wts_to_ts(wts);

			cpu = task_cpu(p);
			rq = cpu_rq(cpu);
			if (task_running(rq, p))
				cpumask_set_cpu(task_cpu(p), &fbg_cpus);
		}
	}

	if (use_vload && up_migrate) {
		return false;
	}

	return cpumask_intersects(&fbg_cpus, &grp->preferred_cluster->cpus);
}

unsigned long sched_get_group_util(const struct cpumask *query_cpus)
{
	unsigned long flag;
	unsigned long grp_util = 0;
	struct frame_boost_group *grp = &default_frame_boost_group;
	u64 now =  walt_ktime_get_ns();

	raw_spin_lock_irqsave(&grp->lock, flag);

	if (!list_empty(&grp->tasks) && grp->preferred_cluster &&
		cpumask_intersects(query_cpus, &grp->preferred_cluster->cpus) &&
		(!group_should_invalid_util(grp, now)) &&
		valid_normalized_util(grp)) {
		grp_util = grp->time.normalized_util;
	}

	raw_spin_unlock_irqrestore(&grp->lock, flag);

	return grp_util;
}

static bool group_should_update_freq(struct frame_boost_group *grp,
				     int cpu, unsigned int flag, u64 now)
{
	if (flag & FRAME_FORCE_UPDATE) {
		return true;
	} else if (flag & FRAME_NORMAL_UPDATE) {
		if (now - grp->last_freq_update_time >= grp->freq_update_interval) {
			return true;
		}
	}

	return false;
}

void sched_set_group_normalized_util(unsigned long util, unsigned int flag)
{
	u64 now;
	int prev_cpu, next_cpu;
	unsigned long flags;
	bool need_update_prev_freq = false;
	bool need_update_next_freq = false;
	struct walt_sched_cluster *preferred_cluster = NULL;
	struct walt_sched_cluster *prev_cluster = NULL;
	struct frame_boost_group *grp = &default_frame_boost_group;

	raw_spin_lock_irqsave(&grp->lock, flags);

	if (list_empty(&grp->tasks)) {
		raw_spin_unlock_irqrestore(&grp->lock, flags);
		return;
	}

	grp->time.normalized_util = util;
	preferred_cluster = best_cluster(grp);

	if (!grp->preferred_cluster)
		grp->preferred_cluster = preferred_cluster;
	else if (grp->preferred_cluster != preferred_cluster) {
		prev_cluster = grp->preferred_cluster;
		prev_cpu = cpumask_first(&prev_cluster->cpus);
		need_update_prev_freq = true;

		grp->preferred_cluster = preferred_cluster;
	}

	if (grp->preferred_cluster)
		next_cpu = cpumask_first(&grp->preferred_cluster->cpus);
	else
		next_cpu = 0;

	now =  walt_ktime_get_ns();
	if (up_migrate && (now - last_migrate_time >= DEFAULT_UTIL_UPDATE_TIMEOUT))
		up_migrate = false;

	grp->last_util_update_time = now;

	need_update_next_freq = group_should_update_freq(grp, next_cpu, flag, now);
	if (need_update_next_freq) {
		grp->last_freq_update_time = now;
	}

	raw_spin_unlock_irqrestore(&grp->lock, flags);

	if (need_update_prev_freq) {
		waltgov_run_callback(cpu_rq(prev_cpu), WALT_CPUFREQ_FORCE_UPDATE);
	}

	if (need_update_next_freq) {
		waltgov_run_callback(cpu_rq(next_cpu), WALT_CPUFREQ_FORCE_UPDATE);
	}
}

static void remove_from_frame_boost_group(struct task_struct *p)
{
	struct walt_task_struct *wts = (struct walt_task_struct *) p->android_vendor_data1;
	struct frame_boost_group *grp = wts->fbg;
	struct rq* rq;
	struct rq_flags flag;
	unsigned long irqflag;

	rq = __task_rq_lock(p, &flag);
	raw_spin_lock_irqsave(&grp->lock, irqflag);

	list_del_init(&wts->fbg_list);
	rcu_assign_pointer(wts->fbg, NULL);

	if (p->on_cpu)
		grp->nr_running--;

	if (grp->nr_running < 0) {
		WARN_ON(1);
		grp->nr_running = 0;
	}

	if (list_empty(&grp->tasks)) {
		grp->preferred_cluster = NULL;
		grp->time.normalized_util = 0;
	}

	raw_spin_unlock_irqrestore(&grp->lock, irqflag);
	__task_rq_unlock(rq, &flag);
}

static void add_to_frame_boost_group(struct task_struct *p, struct frame_boost_group *grp)
{
	struct rq *rq;
	struct rq_flags flag;
	unsigned long irqflag;
	struct walt_task_struct *wts = (struct walt_task_struct *) p->android_vendor_data1;

	rq = __task_rq_lock(p, &flag);
	raw_spin_lock_irqsave(&grp->lock, irqflag);

	list_add(&wts->fbg_list, &grp->tasks);
	rcu_assign_pointer(wts->fbg, grp);

	if (p->on_cpu) {
		grp->nr_running++;
		if (grp->nr_running == 1)
			grp->mark_start = max(grp->mark_start,  walt_ktime_get_ns());
	}

	raw_spin_unlock_irqrestore(&grp->lock, irqflag);
	__task_rq_unlock(rq, &flag);
}

int sched_set_frame_boost_group(struct task_struct *p, bool is_add)
{
	int rc = 0;
	unsigned long flags;
	struct frame_boost_group *grp = NULL;
	struct walt_task_struct *wts = (struct walt_task_struct *) p->android_vendor_data1;

	raw_spin_lock_irqsave(&p->pi_lock, flags);

	if((!wts->fbg && !is_add) || (wts->fbg && is_add) ||  (is_add && (p->flags & PF_EXITING)))
		goto done;

	if (!is_add) {
		remove_from_frame_boost_group(p);
		goto done;
	}

	grp = &default_frame_boost_group;

	add_to_frame_boost_group(p, grp);

done:
	raw_spin_unlock_irqrestore(&p->pi_lock, flags);

	return rc;
}

int set_fbg_sched(struct task_struct *task, bool is_add)
{
	int err = -1;

	if (!task)
		return err;

	if (in_interrupt()) {
		pr_err("[FRAME_BOOST]: %s is in interrupt\n", __func__);
		return err;
	}

	err = sched_set_frame_boost_group(task, is_add);

	return err;
}

void set_fbg_thread(struct task_struct *task, bool is_add)
{
	int err = 0;
	struct walt_task_struct *wts = NULL;

	if (!task)
		return;

	err = set_fbg_sched(task, is_add);
	if (err < 0) {
		pr_err("[FRAME_BOOST]: %s task:%d set_group err:%d\n",
		__func__, task->pid, err);
		return;
	}

	wts = (struct walt_task_struct *) task->android_vendor_data1;
	if (is_add) {
		wts->fbg_depth = STATIC_FBG_DEPTH;
	} else {
		wts->fbg_depth = 0;
	}
}

struct task_struct *do_update_thread(int pid, struct task_struct *old)
{
	struct task_struct *new = NULL;

	if (pid > 0) {
		if (old && (pid == old->pid))
			return old;

		rcu_read_lock();
		new = find_task_by_vpid(pid);
		if (new) {
			get_task_struct(new);
		}
		rcu_read_unlock();
	}

	if (atomic_read(&start_frame) == 1) {
		set_fbg_thread(old, false);
		set_fbg_thread(new, true);
	}

	if (old) {
		put_task_struct(old);
	}

	return new;
}

void update_hwui_tasks(int hwtid1, int hwtid2) {
	hwtask1 = do_update_thread(hwtid1, hwtask1);
	hwtask2 = do_update_thread(hwtid2, hwtask2);
}

void update_frame_thread(int pid, int tid)
{
	unsigned long flags;
	raw_spin_lock_irqsave(&fbg_lock, flags);

	ui = do_update_thread(pid, ui);
	render = do_update_thread(tid, render);
	if (ui) {
		if (strstr(ui->comm, lc)
			|| strstr(ui->comm, dy)
			|| strstr(ui->comm, ks)
			|| strstr(ui->comm, douyu)) {
			stune_boost = 30;
			need_spb = 1;
		} else {
			stune_boost = 20;
			need_spb = 0;
		}
	}

	raw_spin_unlock_irqrestore(&fbg_lock, flags);
}

/* binder thread (!oneway) for boost group*/
static int max_trans_depth = DEFAULT_TRANS_DEPTH;
static int max_trans_num = DEFAULT_MAX_THREADS;
static atomic_t fbg_trans_num = ATOMIC_INIT(0);

bool is_frame_task(struct task_struct *task)
{
	struct frame_boost_group *grp = NULL;

	if (task == NULL) {
		return false;
	}

	rcu_read_lock();
	grp = task_frame_boost_group(task);
	rcu_read_unlock();

	return grp;
}

void add_trans_thread(struct task_struct *target, struct task_struct *from)
{
	int ret;
	struct walt_task_struct *wts1 = NULL;
	struct walt_task_struct *wts2 = NULL;

	if (target == NULL || from == NULL) {
		return;
	}

	if (is_frame_task(target) || !is_frame_task(from)) {
		return;
	}

	if (atomic_read(&fbg_trans_num) >= max_trans_num) {
		return;
	}

	wts1 =(struct walt_task_struct *) from->android_vendor_data1;
	wts2 =(struct walt_task_struct *) target->android_vendor_data1;

	if (wts1->fbg_depth != STATIC_FBG_DEPTH &&
		wts1->fbg_depth >= max_trans_depth) {
		return;
	}

	get_task_struct(target);
	ret = set_fbg_sched(target, true);
	if (ret < 0) {
		put_task_struct(target);
		return;
	}

	if (wts1->fbg_depth == STATIC_FBG_DEPTH)
		wts2->fbg_depth = 1;
	else
		wts2->fbg_depth = wts1->fbg_depth + 1;

	atomic_inc(&fbg_trans_num);
	put_task_struct(target);
}

void remove_trans_thread(struct task_struct *target)
{
	int ret;
	struct walt_task_struct *wts = NULL;
	if (target == NULL)
		return;

	if (!is_frame_task(target))
		return;

	get_task_struct(target);

	wts =(struct walt_task_struct *) target->android_vendor_data1;
	if (wts->fbg_depth == STATIC_FBG_DEPTH) {
		put_task_struct(target);
		return;
	}

	ret = set_fbg_sched(target, false);
	if (ret < 0) {
		put_task_struct(target);
		return;
	}

	wts->fbg_depth = 0;
	if (atomic_read(&fbg_trans_num) > 0) {
		atomic_dec(&fbg_trans_num);
	}
	put_task_struct(target);
}

void binder_thread_set_fbg(struct task_struct *thread, struct task_struct *from, bool oneway)
{
	if (!oneway && from && thread) {
		add_trans_thread(thread, from);
	}
}

void binder_thread_remove_fbg(struct task_struct *thread, bool oneway)
{
	if (!oneway && thread) {
		remove_trans_thread(thread);
	}
}

/* implement vender hook in driver/android/binder.c */
void fbg_binder_wakeup_hook(void *unused, struct task_struct *task, bool sync, struct binder_proc *proc)
{
	if (unlikely(walt_disabled))
		return;

	binder_thread_set_fbg(task, current, !sync);
}

void fbg_binder_restore_priority_hook(void *unused, struct binder_transaction *t, struct task_struct *task)
{
	if (unlikely(walt_disabled))
		return;

	if (t != NULL) {
		binder_thread_remove_fbg(task, false);
	}
}

void fbg_binder_wait_for_work_hook(void *unused,
                       bool do_proc_work, struct binder_thread *tsk, struct binder_proc *proc)
{
	if (unlikely(walt_disabled))
		return;

	if (do_proc_work) {
		binder_thread_remove_fbg(tsk->task, false);
	}
}

void fbg_sync_txn_recvd_hook(void *unused, struct task_struct *tsk, struct task_struct *from)
{
	if (unlikely(walt_disabled))
		return;
	binder_thread_set_fbg(tsk, from, false);
}

#define TOPAPP 4
#define VIPAPP 8
static bool get_grp(struct task_struct *p)
{
        struct cgroup_subsys_state *css;
        if (p == NULL)
                return false;

        rcu_read_lock();
        css = task_css(p, cpu_cgrp_id);
        if (!css) {
                rcu_read_unlock();
                return false;
        }
        rcu_read_unlock();

        return ((css->id == VIPAPP) || (css->id == TOPAPP));
}

static void fbg_cgroup_set_task_hook(void *unused, int ret, struct task_struct *p)
{
        bool is_top = false;
        bool is_webview = (oplus_get_im_flag(p) == IM_FLAG_WEBVIEW);

        if (ret || !is_webview)
                return;

        is_top = get_grp(p);

        /* Clear all frame tasks if the group ui thread move to un-top-app cgroup */
        set_fbg_thread(p, is_top);
}

int frame_boost_group_init(void)
{
	struct frame_boost_group *grp = &default_frame_boost_group;

	grp->freq_update_interval = DEFAULT_FREQ_UPDATE_INTERVAL;
	grp->util_invalid_interval = DEFAULT_UTIL_INVALID_INTERVAL;
	grp->util_update_timeout = DEFAULT_UTIL_UPDATE_TIMEOUT;
	grp->window_size = NSEC_PER_SEC / DEFAULT_GROUP_RATE;
	grp->preferred_cluster = NULL;

	INIT_LIST_HEAD(&grp->tasks);
	raw_spin_lock_init(&grp->lock);
	schedtune_spc_rdiv = reciprocal_value(100);
	/* Register vender hook in kernel/cgroup/cgroup-v1.c */
        register_trace_android_vh_cgroup_set_task(fbg_cgroup_set_task_hook, NULL);
	return 0;
}

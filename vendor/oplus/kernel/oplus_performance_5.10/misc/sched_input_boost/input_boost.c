#include "walt.h"

#define UX_LOAD_WINDOW 8000000

#define FPS90 (90)
#define FPS120 (120)

#define HALF1 (5)
#define HALF2 (4)
#define HALF3 (3)
#define SA_TYPE_HEAVY (1 << 1)
#define DEFAULT_INPUT_BOOST_DURATION 1000
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_SCHED_ASSIST)
#include <../kernel/oplus_perf_sched/sched_assist/sa_common.h>
#endif

u64 ux_task_load[NR_CPUS] = {0};
u64 ux_load_ts[NR_CPUS] = {0};
int sysctl_slide_boost_enabled = 0;
int sysctl_boost_task_threshold = 51;
int sysctl_frame_rate = 60;
int sysctl_input_boost_enabled = 0;
u64 oplus_input_boost_duration = 0;
extern u64 walt_ktime_get_ns(void);

static inline unsigned long  task_sum_demand(struct task_struct *p)
{
	struct walt_task_struct *wts = (struct walt_task_struct *) p->android_vendor_data1;
	return scale_demand(wts->sum);
}

void adjust_sched_assist_input_ctrl(void) {
	if (!sysctl_input_boost_enabled)
		return;
	if(sysctl_slide_boost_enabled) {
		sysctl_input_boost_enabled = 0;
		oplus_input_boost_duration = 0;
		return;
	}
	if(jiffies_to_msecs(jiffies) < oplus_input_boost_duration) {
		return;
	}
	else {
		oplus_input_boost_duration = 0;
		sysctl_input_boost_enabled = 0;
	}
}

void sched_assist_adjust_slide_param(unsigned int *maxtime) {
	 /*give each scene with default boost value*/
	if (sysctl_slide_boost_enabled) {
		 if (sysctl_frame_rate <= 90)
			 *maxtime = HALF1;
		 else if (sysctl_frame_rate <= 120)
			 *maxtime = HALF2;
		 else
			 *maxtime = HALF3;
	 } else if (sysctl_input_boost_enabled) {
		 if (sysctl_frame_rate <= 90)
			 *maxtime = 8;
		 else if (sysctl_frame_rate <= 120)
			 *maxtime = 7;
		 else
			 *maxtime = 6;
	 }
}

u64 calc_ux_load(struct task_struct *p, u64 wallclock)
{
	unsigned int maxtime = 0;
	u64 timeline = 0, exec_load = 0, ravg_load = 0;
	struct walt_task_struct *wts = (struct walt_task_struct *) p->android_vendor_data1;

	u64 wakeclock = wts->last_wake_ts;
	u64 maxload = (sched_ravg_window >> 1) + (sched_ravg_window >> 4);

	if (wallclock < wakeclock)
		return 0;

	sched_assist_adjust_slide_param(&maxtime);

	maxtime = maxtime * NSEC_PER_MSEC;
	timeline = wallclock - wakeclock;
	if (timeline >= maxtime) {
		exec_load = maxload;
	}

	ravg_load = (wts->prev_window + wts->curr_window) << 1;
	if (ravg_load > maxload)
		ravg_load = maxload;

	return max(exec_load, ravg_load);
}

bool slide_boost_scene(void)
{
	return sysctl_slide_boost_enabled || sysctl_input_boost_enabled
		|| sched_assist_scene(SA_ANIM) || sched_assist_scene(SA_GPU_COMPOSITION);
}

bool slide_boost_target(struct task_struct *p)
{
	return p && (is_heavy_ux_task(p) || test_task_is_rt(p) || test_inherit_ux(p, INHERIT_UX_BINDER));
}

bool task_fit_slide_boost(struct task_struct *task, int cpu)
{
	return slide_boost_target(task) && !oplus_task_misfit(task, cpu);
}

bool oplus_task_misfit(struct task_struct *p, int cpu)
{
	int num_mincpu = cpumask_weight(topology_core_cpumask(0));

	if ((task_sum_demand(p) >= sysctl_boost_task_threshold ||
	     task_util(p) >= sysctl_boost_task_threshold) && cpu < num_mincpu)
		return true;

	return false;
}

bool slide_task_misfit(struct task_struct *p, int cpu)
{
	return slide_boost_scene() && slide_boost_target(p) && oplus_task_misfit(p, cpu);
}

bool slide_rt_boost(struct task_struct *p)
{
	struct oplus_task_struct *ots = get_oplus_task_struct(p);
	unsigned long tutil = task_util(p);

	if (ots->im_flag == IM_FLAG_SURFACEFLINGER || ots->im_flag == IM_FLAG_RENDERENGINE) {
		if (slide_boost_scene() || tutil > 90)
			return true;
	}
	return false;
}

void find_demand_fits_cpu(struct task_struct *p, int *target)
{
	int i = 0;
	struct rq *rq = NULL;
	int nr_min = cpumask_weight(topology_core_cpumask(0));

	for (i = nr_min; i < nr_cpu_ids; i++) {
		if (i < 0) {
			break;
		}

		rq = cpu_rq(i);
		if (!rq || rq->curr->prio <= MAX_RT_PRIO)
			continue;

		if (!is_heavy_ux_task(rq->curr) && cpumask_test_cpu(i, cpu_active_mask)
			&& cpumask_test_cpu(i, p->cpus_ptr)) {
			*target = i;
			break;
		}
	}
}

void cpufreq_update_by_slide(struct rq *rq)
{
	unsigned int flag = 0;
	int cpu = cpu_of(rq);
	u64 wallclock = walt_ktime_get_ns();
	adjust_sched_assist_input_ctrl();
	if (slide_boost_scene()) {
		if (task_fit_slide_boost(rq->curr, cpu)) {
			ux_task_load[cpu] = calc_ux_load(rq->curr, wallclock);
			ux_load_ts[cpu] = wallclock;
			flag |= WALT_CPUFREQ_FORCE_UPDATE;
			waltgov_run_callback(rq, flag);
		} else if (ux_task_load[cpu] != 0) {
			ux_task_load[cpu] = 0;
			flag |= WALT_CPUFREQ_FORCE_UPDATE;
			waltgov_run_callback(rq, flag);
		}
	}
}

unsigned long oplus_get_boost_util(int cpu)
{
	u64 wallclock = walt_ktime_get_ns();
	u64 timeline = 0;
	unsigned long util = 0;

	if (slide_boost_scene() && ux_task_load[cpu]) {
		timeline = wallclock - ux_load_ts[cpu];
		if  (timeline >= UX_LOAD_WINDOW) {
			ux_task_load[cpu] = 0;
			return util;
		}
		util = div_u64(ux_task_load[cpu] << SCHED_CAPACITY_SHIFT, sched_ravg_window);
	}
	return util;
}
EXPORT_SYMBOL_GPL(oplus_get_boost_util);

#define INPUT_BOOST_DURATION 1500000000 /* ns*/
static struct hrtimer ibtimer;
static int intput_boost_duration;
static ktime_t ib_last_time;

void enable_input_boost_timer(void)
{
	ktime_t ktime;
	ib_last_time = ktime_get();

	ktime = ktime_set(0, intput_boost_duration);
	hrtimer_start(&ibtimer, ktime, HRTIMER_MODE_REL);
}

void disable_input_boost_timer(void)
{
	hrtimer_cancel(&ibtimer);
}

enum hrtimer_restart input_boost_timeout(struct hrtimer *timer)
{
	ktime_t now;
	now = ktime_get();

	ib_last_time = now;
	sysctl_input_boost_enabled = 0;
	return HRTIMER_NORESTART;
}

int oplus_slide_boost_ctrl_handler(struct ctl_table * table, int write, void __user * buffer, size_t * lenp, loff_t * ppos)
{
	int result;

	result = proc_dointvec(table, write, buffer, lenp, ppos);
	if (!write)
		goto out;
	if (sysctl_input_boost_enabled && sysctl_slide_boost_enabled) {
		disable_input_boost_timer();
		sysctl_input_boost_enabled = 0;
	}
out:
	return result;
}

int oplus_input_boost_ctrl_handler(struct ctl_table * table, int write, void __user * buffer, size_t * lenp, loff_t * ppos)
{
	int result;
	result = proc_dointvec(table, write, buffer, lenp, ppos);
	if (!write)
		goto out;

	disable_input_boost_timer();
	enable_input_boost_timer();
out:
	return result;
}

int oplus_input_boost_init(void)
{
	int ret = 0;

	ib_last_time = ktime_get();
	intput_boost_duration = INPUT_BOOST_DURATION;

	hrtimer_init(&ibtimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	ibtimer.function = &input_boost_timeout;

	return ret;
}
EXPORT_SYMBOL_GPL(oplus_input_boost_init);

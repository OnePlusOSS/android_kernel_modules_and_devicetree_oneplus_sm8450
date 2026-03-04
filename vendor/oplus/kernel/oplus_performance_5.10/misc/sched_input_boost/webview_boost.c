#include <linux/sched.h>
#include "walt.h"

#if IS_ENABLED(CONFIG_OPLUS_FEATURE_SCHED_ASSIST)
#include <../kernel/oplus_perf_sched/sched_assist/sa_common.h>
#endif
#include "webview_boost.h"
#include "frame_boost_group.h"

#define TOPAPP 4
#define VIPAPP 8
#define MIN_BOOST_VLAUE 15
#define DEBUG 0
#define WEB_ID_MAX 10
#define CHILD_MAX 3

static int webview_debug = 0;
module_param_named(debug, webview_debug, uint, 0644);

static struct web_target {
	char val[16];
	char *desc[CHILD_MAX];
} web_target[WEB_ID_MAX] = {
	{"cent.mm:toolsmp", {"Chrome_InProc", "Compositor"}},
	{"m.taobao.taobao", {"Chrome_IOThread", "Chrome_ChildIOT", "Chrome_InProc"}},
	{"vilege_process0", {"Chrome_ChildIOT", "Compositor"}},
	{"ocessService0:0", {"CrRendererMain"}},
	{"ileged_process0", {"Chrome_ChildIOT", "VizCompositorTh", "CrGpuMain"}},
	{"taobao.idlefish", {"JNISurfaceTextu", "1.ui"}},
	{"nt.mm:appbrand0", {"Chrome_InProcRe", "Compositor", "VizCompositorTh"}},
	{"v.douyu.android", {"Chrome_IOThread"}},
	{"ieyou.train.ark", {"1.ui", "JNISurfaceTextu"}},
};

bool is_top(struct task_struct *p)
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

void task_rename_hook(void *unused, struct task_struct *p, const char *buf)
{
	struct task_struct *leader = p->group_leader;
	int im_flag = IM_FLAG_WEBVIEW;
	int i = 0, j = 0;
	size_t tlen = 0;

	if (unlikely(walt_disabled))
		return;

	for (i = 0; i < WEB_ID_MAX; ++i) {
		tlen = strlen(web_target[i].val);
		if (tlen == 0)
			break;

		if (strstr(leader->comm, web_target[i].val)) {
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_SCHED_ASSIST)
			for (j = 0; j < CHILD_MAX; ++j) {
				char *temp = web_target[i].desc[j];
				if (!temp)
					break;

				if ((p->prio <= DEFAULT_PRIO) && strstr(buf, temp)) {
					oplus_set_im_flag(p, im_flag);
					oplus_set_ux_state(p, SA_TYPE_LIGHT);
					set_fbg_thread(p, true);
					if(webview_debug) {
						pr_debug("record webview: pid=%d comm=%s prio=%d leader_pid=%d leader_comm=%s\n",
						p->pid, buf, p->prio, leader->pid, leader->comm);
					}
					break;
				} else if (oplus_get_im_flag(p) == IM_FLAG_WEBVIEW) {
					oplus_set_im_flag(p, 0);
					oplus_set_ux_state(p, 0);
					set_fbg_thread(p, false);
				}
			}
#endif
			break;
		}
	}
}

static bool is_webview(struct task_struct *p)
{
	if (!is_top(p))
		return false;

#if IS_ENABLED(CONFIG_OPLUS_FEATURE_SCHED_ASSIST)
	if (oplus_get_im_flag(p) == IM_FLAG_WEBVIEW) {
		return true;
	}
#endif

	return false;
}

bool is_webview_boost(struct task_struct *p)
{
	if (is_webview(p) && task_util(p) >= MIN_BOOST_VLAUE)
		return true;

	return false;
}

extern unsigned long cpu_util_without(int cpu, struct task_struct *p);
static unsigned long capacity_spare_without(int cpu, struct task_struct *p)
{
	return max_t(long, capacity_of(cpu) - cpu_util_without(cpu, p), 0);
}

int find_webview_cpu(struct task_struct *p)
{
	int i;
	int max_spare_cap_cpu = -1;
	int active_cpu = -1;
	unsigned long spare_cap = 0, max_spare_cap = 0;
	int index = (num_sched_clusters > 1) ? 1 : 0;
	struct walt_sched_cluster *preferred_cluster = sched_cluster[index];

	struct cpumask *preferred_cpus = &preferred_cluster->cpus;
	struct task_struct *curr = NULL;
	struct rq * rq = NULL;

	cpumask_t search_cpus = CPU_MASK_NONE;
	cpumask_and(&search_cpus, &p->cpus_mask, cpu_active_mask);

	for_each_cpu_and(i, &search_cpus, preferred_cpus) {
		if (active_cpu == -1)
			active_cpu = i;

		if (is_reserved(i))
			continue;

		rq = cpu_rq(i);
		curr = rq->curr;
		if ((curr->prio < MAX_RT_PRIO) || is_webview(curr))
			continue;

		if (available_idle_cpu(i) || (i == task_cpu(p) && p->state == TASK_RUNNING)) {
			return i;
		}

		spare_cap = capacity_spare_without(i, p);
		if (spare_cap > max_spare_cap) {
			max_spare_cap = spare_cap;
			max_spare_cap_cpu = i;
		}
	}
	if (max_spare_cap_cpu == -1)
		max_spare_cap_cpu = active_cpu;

	return max_spare_cap_cpu;
}

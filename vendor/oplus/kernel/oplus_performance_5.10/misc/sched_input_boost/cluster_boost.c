#include <linux/sched.h>
#include "walt.h"
#include "sched_assist_ofb.h"

static bool cb_debug = false;

static void trace_set_task_preferred_cluster(struct task_struct *p, long cpus_mask,
			long active_mask, int preferred_cluster_id)
{
	char buf[256];

	snprintf(buf, sizeof(buf), "comm:%s pid:%d tgid:%d, cpus_mask=%lx, active_mask=%lx,"
		" preferred_cluster_id=%d\n", p->comm, p->pid, p->tgid,
		cpus_mask, active_mask, preferred_cluster_id);
	trace_printk(buf);
}

static void trace_cluster_boost(struct task_struct *p, long cpus_mask,
			long active_mask, int preferred_cluster_id, int prev_cpu, int best_cpu)
{
	char buf[256];

	snprintf(buf, sizeof(buf), "comm:%s pid:%d tgid:%d, cpus_mask=%lx, active_mask=%lx,"
		" preferred_cluster_id=%d, pre_cpu=%d, best_cpu=%d\n", p->comm,
		p->pid, p->tgid, cpus_mask, active_mask, preferred_cluster_id, prev_cpu, best_cpu);
	trace_printk(buf);
}

static void _set_task_preferred_cluster(pid_t tid, int cluster_id)
{
	struct task_struct * task = NULL;
	struct walt_task_struct *wts = NULL;

	rcu_read_lock();
	task = find_task_by_vpid(tid);
	if (task) {
		wts = (struct walt_task_struct *)task->android_vendor_data1;
		if ((cluster_id >= 0) && (cluster_id < num_sched_clusters)) {
			wts->preferred_cluster_id = cluster_id;
		} else {
			wts->preferred_cluster_id = -1;
		}

		if (cb_debug) {
			trace_set_task_preferred_cluster(task, cpumask_bits(task->cpus_ptr)[0],
				cpumask_bits(cpu_active_mask)[0], wts->preferred_cluster_id);
		}
	}
	rcu_read_unlock();
}

int fbg_set_task_preferred_cluster(void __user *uarg)
{
	struct ofb_ctrl_cluster data;

	if (uarg == NULL)
		return -EINVAL;

	if (copy_from_user(&data, uarg, sizeof(data)))
		return -EFAULT;

	_set_task_preferred_cluster(data.tid, data.cluster_id);

	return 0;
}

extern unsigned long cpu_util_without(int cpu, struct task_struct *p);
static unsigned long capacity_spare_without(int cpu, struct task_struct *p)
{
	return max_t(long, capacity_of(cpu) - cpu_util_without(cpu, p), 0);
}

void fbg_cluster_boost(struct task_struct *p, int *fbg_best_cpu)
{
	struct walt_task_struct *wts = (struct walt_task_struct *)p->android_vendor_data1;
	int preferred_cluster_id = wts->preferred_cluster_id;
	struct walt_sched_cluster *preferred_cluster;
	struct cpumask *preferred_cpus;
	cpumask_t search_cpus = CPU_MASK_NONE;
	int i;
	int active_cpu = -1;
	int max_spare_cap_cpu = -1;
	unsigned long spare_cap = 0, max_spare_cap = 0;

	bool valid_cluster_id = (preferred_cluster_id >= 0)
		&& (preferred_cluster_id < num_sched_clusters);
	if (!valid_cluster_id)
		return;

	preferred_cluster = sched_cluster[preferred_cluster_id];
	preferred_cpus = &preferred_cluster->cpus;
	cpumask_and(&search_cpus, &p->cpus_mask, cpu_active_mask);
	cpumask_and(&search_cpus, &search_cpus, preferred_cpus);

	for_each_cpu(i, &search_cpus) {
		if (active_cpu == -1)
			active_cpu = i;

		if (is_reserved(i))
			continue;

		if (available_idle_cpu(i) || (i == task_cpu(p) && p->state == TASK_RUNNING)) {
			max_spare_cap_cpu = i;
			break;
		}

		spare_cap = capacity_spare_without(i, p);
		if (spare_cap > max_spare_cap) {
			max_spare_cap = spare_cap;
			max_spare_cap_cpu = i;
		}
	}

	if (max_spare_cap_cpu == -1)
		max_spare_cap_cpu = active_cpu;

	if ((max_spare_cap_cpu == -1)
		|| ((cpumask_weight(&search_cpus) == 1) && (!available_idle_cpu(max_spare_cap_cpu))))
		goto debug_info;

	*fbg_best_cpu = max_spare_cap_cpu;

debug_info:
	if (cb_debug) {
		trace_cluster_boost(p, cpumask_bits(p->cpus_ptr)[0],
			cpumask_bits(cpu_active_mask)[0], preferred_cluster_id, task_cpu(p),
			*fbg_best_cpu);
	}
}

static ssize_t cluster_boost_proc_write(struct file *file,
			const char __user *buf, size_t count, loff_t *ppos)
{
	char page[64] = {0};
	int ret, tid, cluster_id;

	ret = simple_write_to_buffer(page, sizeof(page), ppos, buf, count);
	if (ret <= 0)
		return ret;

	ret = sscanf(page, "%d %d", &tid, &cluster_id);
	if (ret != 2) {
		ret = -EINVAL;
		return ret;
	}

	_set_task_preferred_cluster(tid, cluster_id);

	return count;
}

static const struct proc_ops cluster_boost_proc_ops = {
	.proc_write		= cluster_boost_proc_write,
};

void fbg_cluster_boost_init(struct proc_dir_entry *parent)
{
	if (cb_debug) {
		proc_create("cluster_boost", 0220, parent, &cluster_boost_proc_ops);
	}
}

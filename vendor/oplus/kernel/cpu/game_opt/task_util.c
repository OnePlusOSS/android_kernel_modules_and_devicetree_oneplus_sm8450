// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022 Oplus. All rights reserved.
 */

#include <linux/atomic.h>
#include <linux/kernel.h>
#include <linux/sort.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/syscore_ops.h>
#include <linux/seq_file.h>
#include <linux/proc_fs.h>
#include <linux/cpufreq.h>
#include <linux/sched/cpufreq.h>
#include <trace/hooks/sched.h>

#include "game_ctrl.h"

struct task_runtime_info {
	pid_t pid;
	struct task_struct *task;
	u64 sum_exec_scale;
} child_threads[MAX_TID_COUNT];

/* a small value */
#define MAX_UI_ASSIST_NUM 20

struct thread_wake_info {
	pid_t pid;
	struct task_struct *task;
	u32 wake_count;
	bool ui_wakeup_assit;
} ui_assist_threads[MAX_UI_ASSIST_NUM];

static int ui_assist_num = 0;

static struct task_struct *game_leader = NULL;
static pid_t game_pid = -1;
static int child_num;
static u64 window_start;

static DEFINE_RAW_SPINLOCK(g_lock);
atomic_t have_valid_game_pid = ATOMIC_INIT(0);

static ssize_t game_pid_proc_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
	char page[32] = {0};
	int ret, pid;
	struct task_struct *leader = NULL;

	ret = simple_write_to_buffer(page, sizeof(page) - 1, ppos, buf, count);
	if (ret <= 0)
		return ret;

	ret = sscanf(page, "%d", &pid);
	if (ret != 1)
		return -EINVAL;

	atomic_set(&have_valid_game_pid, 0);

	raw_spin_lock(&g_lock);

	if (game_leader) {
		put_task_struct(game_leader);
		game_leader = NULL;
		game_pid = -1;
	}

	ui_assist_num = 0;

	/* release */
	if (pid <= 0) {
		ret = count;
		goto unlock;
	}

	/* acquire */
	rcu_read_lock();
	leader = find_task_by_vpid(pid);
	if (!leader || leader->pid != leader->tgid) { /* must be process id */
		rcu_read_unlock();
		ret = -EINVAL;
		goto unlock;
	} else {
		get_task_struct(leader);
		rcu_read_unlock();
	}

	game_leader = leader;
	game_pid = pid;
	window_start = ktime_get_raw_ns();
	atomic_set(&have_valid_game_pid, 1);

	ret = count;

unlock:
	child_num = 0;
	raw_spin_unlock(&g_lock);
	return ret;
}

static ssize_t game_pid_proc_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
	char page[64] = {0};
	int len;

	raw_spin_lock(&g_lock);
	len = sprintf(page, "game_pid=%d child_num=%d\n", game_pid, child_num);
	raw_spin_unlock(&g_lock);

	return simple_read_from_buffer(buf, count, ppos, page, len);
}

static const struct proc_ops game_pid_proc_ops = {
	.proc_write		= game_pid_proc_write,
	.proc_read		= game_pid_proc_read,
	.proc_lseek		= default_llseek,
};

static struct thread_wake_info *find_ui_assist_wake_info(struct task_struct *task)
{
	int i;

	for (i = 0; i < ui_assist_num; i++) {
		if ((ui_assist_threads[i].task == task) && (ui_assist_threads[i].pid == task->pid))
			return &ui_assist_threads[i];
	}

	return NULL;
}

static bool threads_satify_rule(struct task_struct *task)
{
	if ((task->tgid != game_pid) || (current->tgid != game_pid))
		return false;

	if ((task->pid != game_pid) && (current->pid != game_pid))
		return false;

	return true;
}

void ui_assist_threads_wake_stat(struct task_struct *task)
{
	struct thread_wake_info *ui_assist;

	if (atomic_read(&have_valid_game_pid) == 0)
		return;

	if (!threads_satify_rule(task))
		return;

	/* ui assist thread name-limited "Thread-" */
	if ((current->pid == game_pid) && !strnstr(task->comm, "Thread-", 7))
		return;
	if ((task->pid == game_pid) && !strnstr(current->comm, "Thread-", 7))
		return;

	/*
	 * only update wake stat when lock is available,
	 * if not available, skip.
	 */
	if (raw_spin_trylock(&g_lock)) {
		if (!threads_satify_rule(task))
			goto unlock;

		/* assit wakeup ui */
		if (current->pid == game_pid) {
			ui_assist = find_ui_assist_wake_info(task);
			if (!ui_assist) {
				if (ui_assist_num < MAX_UI_ASSIST_NUM) {
					ui_assist = &ui_assist_threads[ui_assist_num];
					ui_assist->pid = task->pid;
					ui_assist->task = task;
					ui_assist->wake_count = 0;
					ui_assist->ui_wakeup_assit = false;
					ui_assist_num++;
				}
			} else {
				if (ui_assist->ui_wakeup_assit) {
					ui_assist->wake_count++;
					ui_assist->ui_wakeup_assit = false;
				}
			}
		} else { /* ui wakeup assit */
			ui_assist = find_ui_assist_wake_info(current);
			if (ui_assist)
				ui_assist->ui_wakeup_assit = true;
		}

unlock:
		raw_spin_unlock(&g_lock);
	}
}

/*
 * Ascending order by wake_count
 */
static int cmp_task_wake_count(const void *a, const void *b)
{
	struct thread_wake_info *prev, *next;

	prev = (struct thread_wake_info *)a;
	next = (struct thread_wake_info *)b;
	if (unlikely(!prev || !next))
		return 0;

	if (prev->wake_count > next->wake_count)
		return -1;
	else if (prev->wake_count < next->wake_count)
		return 1;
	else
		return 0;
}

static struct thread_wake_info ui_results[MAX_UI_ASSIST_NUM];
static char ui_page[512] = {0};
#define MAX_UA_RESULT_NUM 5
static int ui_assist_thread_show(struct seq_file *m, void *v)
{
	int i, num, result_num = 0;
	char task_name[TASK_COMM_LEN];
	ssize_t len = 0;

	if (atomic_read(&have_valid_game_pid) == 0)
		return -ESRCH;

	raw_spin_lock(&g_lock);
	for (i = 0; i < ui_assist_num; i++) {
		if (ui_assist_threads[i].wake_count > 0) {
			ui_results[result_num].pid = ui_assist_threads[i].pid;
			ui_results[result_num].task = ui_assist_threads[i].task;
			ui_results[result_num].wake_count = ui_assist_threads[i].wake_count;
			result_num++;
		}
	}
	raw_spin_unlock(&g_lock);

	if (result_num > 1) {
		sort(&ui_results[0], result_num,
			sizeof(struct thread_wake_info), &cmp_task_wake_count, NULL);
	}

	memset(ui_page, 0, sizeof(ui_page));

	num = 0;
	for (i = 0; i < result_num; i++) {
		if (get_task_name(ui_results[i].pid, ui_results[i].task, task_name)) {
			len += snprintf(ui_page + len, RESULT_PAGE_SIZE - len, "%d;%s;%u\n",
				ui_results[i].pid, task_name, ui_results[i].wake_count);
			if (++num >= MAX_UA_RESULT_NUM)
				break;
		}
	}

	if (len > 0)
		seq_puts(m, ui_page);

	return 0;
}

static int ui_assist_thread_proc_open(struct inode *inode, struct file *filp)
{
	return single_open(filp, ui_assist_thread_show, inode);
}

static const struct proc_ops ui_assist_thread_proc_ops = {
	.proc_open		= ui_assist_thread_proc_open,
	.proc_read		= seq_read,
	.proc_lseek		= seq_lseek,
	.proc_release	= single_release,
};

/*
 * Ascending order by sum_exec_scale
 */
static int cmp_task_sum_exec_scale(const void *a, const void *b)
{
	struct task_runtime_info *prev, *next;

	prev = (struct task_runtime_info *)a;
	next = (struct task_runtime_info *)b;
	if (unlikely(!prev || !next))
		return 0;

	if (prev->sum_exec_scale > next->sum_exec_scale)
		return -1;
	else if (prev->sum_exec_scale < next->sum_exec_scale)
		return 1;
	else
		return 0;
}

static inline int cal_util(u64 sum_exec_scale, u64 window_size)
{
	int util;

	if (unlikely(window_size <= 0))
		return 0;

	util = sum_exec_scale / (window_size >> 10);
	if (util > 1024)
		util = 1024;

	return util;
}

bool get_task_name(pid_t pid, struct task_struct *in_task, char *name)
{
	struct task_struct * task = NULL;
	bool ret = false;

	rcu_read_lock();
	task = find_task_by_vpid(pid);
	if (task && (task == in_task)) {
		strncpy(name, task->comm, TASK_COMM_LEN);
		ret = true;
	}
	rcu_read_unlock();

	return ret;
}

static int heavy_task_info_show(struct seq_file *m, void *v)
{
	char *page;
	struct task_runtime_info *results;
	int i, num, util, result_num;
	char task_name[TASK_COMM_LEN];
	ssize_t len = 0;
	u64 now, window_size;

	if (atomic_read(&have_valid_game_pid) == 0)
		return -ESRCH;

	page = kzalloc(RESULT_PAGE_SIZE, GFP_KERNEL);
	if (!page)
		return -ENOMEM;
	results = kmalloc(sizeof(struct task_runtime_info) * MAX_TID_COUNT, GFP_KERNEL);
	if (!results) {
		kfree(page);
		return -ENOMEM;
	}

	raw_spin_lock(&g_lock);
	for (i = 0; i < child_num; i++) {
		results[i].pid = child_threads[i].pid;
		results[i].task = child_threads[i].task;
		results[i].sum_exec_scale = child_threads[i].sum_exec_scale;
	}

	result_num = child_num;
	child_num = 0;
	now = ktime_get_raw_ns();
	window_size = now - window_start;
	window_start = now;
	raw_spin_unlock(&g_lock);

	/* ascending order by sum_exec_scale */
	sort(results, result_num, sizeof(struct task_runtime_info),
		&cmp_task_sum_exec_scale, NULL);

	num = 0;
	for (i = 0; i < result_num; i++) {
		util = cal_util(results[i].sum_exec_scale, window_size);
		if (util <= 0)
			break;
		if (get_task_name(results[i].pid, results[i].task, task_name)) {
			len += snprintf(page + len, RESULT_PAGE_SIZE - len, "%d;%s;%d\n",
				results[i].pid, task_name, util);
			if (++num >= MAX_TASK_NR)
				break;
		}
	}

	if (len > 0)
		seq_puts(m, page);

	kfree(results);
	kfree(page);

	return 0;
}

static int heavy_task_info_proc_open(struct inode *inode, struct file *filp)
{
	return single_open(filp, heavy_task_info_show, inode);
}

static const struct proc_ops heavy_task_info_proc_ops = {
	.proc_open		= heavy_task_info_proc_open,
	.proc_read		= seq_read,
	.proc_lseek		= seq_lseek,
	.proc_release	= single_release,
};

static inline unsigned int get_cur_freq(unsigned int cpu)
{
	struct cpufreq_policy *policy = cpufreq_cpu_get_raw(cpu);

	return (policy == NULL) ? 0 : policy->cur;
}

static inline unsigned int get_max_freq(unsigned int cpu)
{
	struct cpufreq_policy *policy = cpufreq_cpu_get_raw(cpu);

	return (policy == NULL) ? 0 : policy->cpuinfo.max_freq;
}

#define DIV64_U64_ROUNDUP(X, Y) div64_u64((X) + (Y - 1), Y)
static inline u64 scale_exec_time(u64 delta, struct rq *rq)
{
	u64 task_exec_scale;
	unsigned int cur_freq, max_freq;
	int cpu = cpu_of(rq);

	cur_freq = get_cur_freq(cpu);
	max_freq = get_max_freq(cpu);

	if (unlikely(cur_freq <= 0) || unlikely(max_freq <= 0) || unlikely(cur_freq > max_freq))
		return delta;

	task_exec_scale = DIV64_U64_ROUNDUP(cur_freq *
				arch_scale_cpu_capacity(cpu),
				max_freq);

	return (delta * task_exec_scale) >> 10;
}

static struct task_runtime_info *find_child_thread(struct task_struct *task)
{
	int i;

	for (i = 0; i < child_num; i++) {
		if ((child_threads[i].task == task) && (child_threads[i].pid == task->pid))
			return &child_threads[i];
	}

	return NULL;
}

static inline void update_task_runtime(struct task_struct *task, u64 runtime)
{
	u64 exec_scale;
	struct rq *rq = task_rq(task);
	struct task_runtime_info *child_thread;

	if (atomic_read(&have_valid_game_pid) == 0)
		return;

	if (task->tgid != game_pid)
		return;

	/*
	 * only stat runtime when lock is available,
	 * if not available, skip.
	 */
	if (raw_spin_trylock(&g_lock)) {
		if (task->tgid != game_pid)
			goto unlock;

		exec_scale = scale_exec_time(runtime, rq);

		child_thread = find_child_thread(task);
		if (!child_thread) {
			if (child_num >= MAX_TID_COUNT)
				goto unlock;
			child_thread = &child_threads[child_num];
			child_thread->pid = task->pid;
			child_thread->task = task;
			child_thread->sum_exec_scale = exec_scale;
			child_num++;
		} else {
			child_thread->sum_exec_scale += exec_scale;
		}

unlock:
		raw_spin_unlock(&g_lock);
	}
}

static void sched_stat_runtime_hook(void *unused, struct task_struct *p, u64 runtime, u64 vruntime)
{
	update_task_runtime(p, runtime);
}

static void sched_stat_runtime_rt_hook(void *unused, struct task_struct *p, u64 runtime)
{
	update_task_runtime(p, runtime);
}

static void register_task_util_vendor_hooks(void)
{
	/* Register vender hook in kernel/sched/fair.c */
	register_trace_sched_stat_runtime(sched_stat_runtime_hook, NULL);

	/* Register vender hook in kernel/sched/rt.c */
	register_trace_android_vh_sched_stat_runtime_rt(sched_stat_runtime_rt_hook, NULL);
}

int task_util_init(void)
{
	if (unlikely(!game_opt_dir))
		return -ENOTDIR;

	register_task_util_vendor_hooks();

	proc_create_data("game_pid", 0664, game_opt_dir, &game_pid_proc_ops, NULL);
	proc_create_data("heavy_task_info", 0444, game_opt_dir, &heavy_task_info_proc_ops, NULL);
	proc_create_data("ui_assist_thread", 0444, game_opt_dir, &ui_assist_thread_proc_ops, NULL);

	return 0;
}

// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022 Oplus. All rights reserved.
 */

#include <trace/events/sched.h>
#include <trace/hooks/sched.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/sort.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/spinlock.h>

#include "game_ctrl.h"

/*
 * render related thread wake information
 */
struct render_related_thread {
	pid_t pid;
	struct task_struct *task;
	u32 wake_count;
} related_threads[MAX_TID_COUNT];

static int rt_num = 0;
static int total_num = 0;
static pid_t game_tgid = -1;

static DEFINE_RWLOCK(rt_info_rwlock);
atomic_t have_valid_render_pid = ATOMIC_INIT(0);

static inline bool same_rt_thread_group(struct task_struct *waker,
	struct task_struct *wakee)
{
	return (waker->tgid == game_tgid) && (wakee->tgid == game_tgid);
}

static struct render_related_thread *find_related_thread(struct task_struct *task)
{
	int i;

	for (i = 0; i < total_num; i++) {
		if ((related_threads[i].task == task) && (related_threads[i].pid == task->pid))
			return &related_threads[i];
	}

	return NULL;
}

static bool is_render_thread(struct render_related_thread * wakee)
{
	int i;

	for (i = 0; i < rt_num; i++) {
		if (related_threads[i].pid == wakee->pid)
			return true;
	}

	return false;
}

static void try_to_wake_up_success_hook(void *unused, struct task_struct *task)
{
	struct render_related_thread *wakee;
	struct render_related_thread *waker;

	ui_assist_threads_wake_stat(task);

	if (atomic_read(&have_valid_render_pid) == 0)
		return;

	/*
	 * ignore wakeup event if waker or wakee
	 * not belong to render related thread group
	 */
	if (!same_rt_thread_group(current, task))
		return;

	/*
	 * only update wake stat when lock is available,
	 * if not available, skip.
	 */
	if (write_trylock(&rt_info_rwlock)) {
		if (!same_rt_thread_group(current, task))
			goto unlock;

		/* wakee must be a render related thread */
		wakee = find_related_thread(task);
		if (wakee) {
			waker = find_related_thread(current);
			if (!waker) {
				if (total_num >= MAX_TID_COUNT)
					goto unlock;
				waker = &related_threads[total_num];
				waker->pid = current->pid;
				waker->task = current;
				waker->wake_count = 1;
				total_num++;
			} else {
				waker->wake_count++;
			}

			if (is_render_thread(wakee))
				wakee->wake_count++;
		}

unlock:
		write_unlock(&rt_info_rwlock);
	}
}

/*
 * Ascending order by wake_count
 */
static int cmp_task_wake_count(const void *a, const void *b)
{
	struct render_related_thread *prev, *next;

	prev = (struct render_related_thread *)a;
	next = (struct render_related_thread *)b;
	if (unlikely(!prev || !next))
		return 0;

	if (prev->wake_count > next->wake_count)
		return -1;
	else if (prev->wake_count < next->wake_count)
		return 1;
	else
		return 0;
}

static int rt_info_show(struct seq_file *m, void *v)
{
	int i, result_num, gl_num;
	struct render_related_thread *results;
	char *page;
	char task_name[TASK_COMM_LEN];
	ssize_t len = 0;

	if (atomic_read(&have_valid_render_pid) == 0)
		return -ESRCH;

	page = kzalloc(RESULT_PAGE_SIZE, GFP_KERNEL);
	if (!page)
		return -ENOMEM;
	results = kmalloc(sizeof(struct render_related_thread) * MAX_TID_COUNT, GFP_KERNEL);
	if (!results) {
		kfree(page);
		return -ENOMEM;
	}

	read_lock(&rt_info_rwlock);
	for (i = 0; i < total_num; i++) {
		results[i].pid = related_threads[i].pid;
		results[i].task = related_threads[i].task;
		results[i].wake_count = related_threads[i].wake_count;
	}

	for (i = 0; i < rt_num; i++)
		related_threads[i].wake_count = 0;
	result_num = total_num;
	gl_num = rt_num;
	total_num = rt_num;
	read_unlock(&rt_info_rwlock);

	if (unlikely(gl_num > 1)) {
		sort(&results[0], gl_num,
			sizeof(struct render_related_thread), &cmp_task_wake_count, NULL);
	}

	if (result_num > gl_num) {
		sort(&results[gl_num], result_num - gl_num,
			sizeof(struct render_related_thread), &cmp_task_wake_count, NULL);
	}

	for (i = 0; i < result_num && i < MAX_TASK_NR; i++) {
		if (get_task_name(results[i].pid, results[i].task, task_name)) {
			len += snprintf(page + len, RESULT_PAGE_SIZE - len, "%d;%s;%u\n",
				results[i].pid, task_name, results[i].wake_count);
		}
	}

	if (len > 0)
		seq_puts(m, page);

	kfree(results);
	kfree(page);

	return 0;
}

static int rt_info_proc_open(struct inode *inode, struct file *filp)
{
	return single_open(filp, rt_info_show, inode);
}

static inline bool is_repetitive_pid(pid_t pid)
{
	int i;

	for (i = 0; i < rt_num; i++) {
		if (pid == related_threads[i].pid)
			return true;
	}

	return false;
}

static ssize_t rt_info_proc_write(struct file *file, const char __user *buf,
	size_t count, loff_t *ppos)
{
	int i, ret;
	char page[128] = {0};
	char *iter = page;
	struct task_struct *task;
	pid_t pid;

	ret = simple_write_to_buffer(page, sizeof(page) - 1, ppos, buf, count);
	if (ret <= 0)
		return ret;

	atomic_set(&have_valid_render_pid, 0);

	write_lock(&rt_info_rwlock);

	for (i = 0; i < rt_num; i++) {
		if (related_threads[i].task)
			put_task_struct(related_threads[i].task);
	}

	rt_num = 0;
	total_num = 0;
	game_tgid = -1;

	while (iter != NULL) {
		/* input should be "123 234" */
		ret = sscanf(iter, "%d", &pid);
		if (ret != 1)
			break;

		iter = strchr(iter + 1, ' ');

		/* skip repetitive pid */
		if (is_repetitive_pid(pid))
			continue;

		rcu_read_lock();
		task = find_task_by_vpid(pid);
		if (task)
			get_task_struct(task);
		rcu_read_unlock();

		if (task) {
			if (game_tgid == -1) {
				game_tgid = task->tgid;
			} else {
				/* all rt threads should belong to a group */
				if (game_tgid != task->tgid) {
					put_task_struct(task);
					continue;
				}
			}

			related_threads[rt_num].pid = pid;
			related_threads[rt_num].task = task;
			related_threads[rt_num].wake_count = 0;

			rt_num++;
		}
	}

	if (rt_num) {
		total_num = rt_num;
		atomic_set(&have_valid_render_pid, 1);
	}

	write_unlock(&rt_info_rwlock);

	return count;
}

static const struct proc_ops rt_info_proc_ops = {
	.proc_open		= rt_info_proc_open,
	.proc_write		= rt_info_proc_write,
	.proc_read		= seq_read,
	.proc_lseek		= seq_lseek,
	.proc_release	= single_release,
};

static int rt_num_show(struct seq_file *m, void *v)
{
	char page[256] = {0};
	ssize_t len = 0;
	int i;

	read_lock(&rt_info_rwlock);
	len += snprintf(page + len, sizeof(page) - len, "rt_num=%d total_num=%d\n",
		rt_num, total_num);
	for (i = 0; i < rt_num; i++) {
		len += snprintf(page + len, sizeof(page) - len, "tgid:%d pid:%d comm:%s\n",
			related_threads[i].task->tgid, related_threads[i].task->pid,
			related_threads[i].task->comm);
	}
	read_unlock(&rt_info_rwlock);

	seq_puts(m, page);

	return 0;
}

static int rt_num_proc_open(struct inode *inode, struct file *filp)
{
	return single_open(filp, rt_num_show, inode);
}

static const struct proc_ops rt_num_proc_ops = {
	.proc_open		= rt_num_proc_open,
	.proc_read		= seq_read,
	.proc_lseek		= seq_lseek,
	.proc_release	= single_release,
};

static void register_rt_info_vendor_hooks(void)
{
	/* Register vender hook in kernel/sched/core.c */
	register_trace_android_rvh_try_to_wake_up_success(try_to_wake_up_success_hook, NULL);
}

int rt_info_init(void)
{
	if (unlikely(!game_opt_dir))
		return -ENOTDIR;

	register_rt_info_vendor_hooks();

	proc_create_data("rt_info", 0664, game_opt_dir, &rt_info_proc_ops, NULL);
	proc_create_data("rt_num", 0444, game_opt_dir, &rt_num_proc_ops, NULL);

	return 0;
}

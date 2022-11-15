// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022-2030 Oplus. All rights reserved.
 */
#include <linux/types.h>
#include <linux/string.h>      /* for strncmp */
#include <linux/workqueue.h>   /* for struct work_struct, INIT_WORK, schedule_work, cancel_work_sync etc */
#include <linux/timer.h>       /* for struct timer_list, timer_setup, mod_timer, del_timer etc */
#include <linux/signal_types.h> /* for struct kernel_siginfo */
#include <linux/sched/signal.h> /* for send_sig_info */
#include <linux/sched.h>       /* for struct task_struct, find_task_by_vpid, PF_FROZEN, TASK_UNINTERRUPTIBLE etc */
#include <linux/err.h>         /* for IS_ERR_OR_NULL */
#include <linux/rcupdate.h>    /* for rcu_read_lock, rcu_read_unlock */
#include <linux/printk.h>      /* for pr_err, pr_info etc */
#include <linux/sched/debug.h> /* for sched_show_task */
#include <linux/uaccess.h>     /* for copy_from_user */
#include <linux/errno.h>       /* for EINVAL, EFAULT */
#include <linux/compiler.h>    /* for likely, unlikely */
#include <linux/proc_fs.h>     /* for struct proc_ops, proc_create, proc_remove etc */
#include <linux/seq_file.h>    /* for single_open, seq_lseek, seq_release etc */
#include <linux/slab.h>        /* for kzalloc, kfree */
#include <linux/module.h>      /* for late_initcall, module_exit etc */
#include <linux/jiffies.h>     /* for jiffies, include asm/param.h for HZ */
#include <linux/kernel.h>      /* for current ,snprintf */
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_THEIA)
#include "../../include/theia_send_event.h" /* for theia_send_event etc */
#endif

#define INIT_WATCHDOG_LOG_TAG "[init_watchdog]"
#define BUF_STR_LEN 10
#define INIT_WATCHDOG_CHECK_INTERVAL 30
#define SIG_WAKEUP_INIT (SIGRTMIN + 0x16)
#define EXTRA_INFO_BUF_LEN 128
#define PROC_INIT_WATCHDOG "init_watchdog"

typedef enum {
	STATE_DISABLE,
	STATE_ENABLE,
	STATE_CHECKED,
} INIT_WATCHDOG_STATE;

typedef enum  {
	STATE_NOT_KICK,
	STATE_KICKED,
} INIT_WATCHDOG_KICK_STATE;

struct init_watchdog_data {
	struct work_struct work;
	struct timer_list timer;
	INIT_WATCHDOG_STATE state;
	INIT_WATCHDOG_KICK_STATE kick_state;
	unsigned int block_count;
};

static struct init_watchdog_data *g_init_watchdog_data;

static void init_check_work_handler(struct work_struct *work)
{
	struct task_struct *taskp = NULL;
	char extra_info[EXTRA_INFO_BUF_LEN] = {0};

	/* init not ready for check, don't check this loop */
	if (g_init_watchdog_data->state != STATE_CHECKED) {
		pr_info(INIT_WATCHDOG_LOG_TAG "init check is not ready\n");
		return;
	}

	/* if kicked, init process work normal, clear block count, exit to next loop */
	if (g_init_watchdog_data->kick_state == STATE_KICKED) {
		g_init_watchdog_data->kick_state = STATE_NOT_KICK;
		g_init_watchdog_data->block_count = 0;
		pr_info(INIT_WATCHDOG_LOG_TAG "init process is alive\n");
		return;
	}

	rcu_read_lock();
	taskp = find_task_by_vpid(1);
	rcu_read_unlock();

	if (IS_ERR_OR_NULL(taskp)) {
		pr_err(INIT_WATCHDOG_LOG_TAG "can't find init task_struct\n");
		return;
	}

	if (taskp->flags & PF_FROZEN) {
		pr_info(INIT_WATCHDOG_LOG_TAG "init process is frozen, normal state\n");
		g_init_watchdog_data->block_count = 0;
		return;
	}

	g_init_watchdog_data->block_count++;

	/* D-state, unrecoverable, just dump debug message */
	if (taskp->state == TASK_UNINTERRUPTIBLE) {
		pr_err(INIT_WATCHDOG_LOG_TAG "init process block in D-state more than 30s\n");

		snprintf(extra_info, EXTRA_INFO_BUF_LEN, "init process block in D-state "
			"more than 30s, block_count is %u", g_init_watchdog_data->block_count);
	} else {
		pr_err(INIT_WATCHDOG_LOG_TAG "init process blocked for long time, "
			"try to send signal to wakeup\n");
		/* will enable wakeup init when detected stuck in sleep state in the future */

		snprintf(extra_info, EXTRA_INFO_BUF_LEN, "init process block for more "
			"than 30s, block_count is %u", g_init_watchdog_data->block_count);
	}
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_THEIA)
	theia_send_event(THEIA_EVENT_INIT_HANG, THEIA_LOGINFO_KERNEL_LOG,
		current->pid, extra_info);
#endif
	pr_err(INIT_WATCHDOG_LOG_TAG "block_count is %u\n",
		g_init_watchdog_data->block_count);
	pr_err(INIT_WATCHDOG_LOG_TAG "dump init process kernel backtrace\n");
	sched_show_task(taskp);

	/* dump all d-state process stacktrace if necessary */
}

static ssize_t init_watchdog_proc_write(struct file *filp,
	const char __user *buf, size_t count, loff_t *off)
{
	char str[BUF_STR_LEN] = {0};

	if (count < 4 || count > 9) {
		pr_err(INIT_WATCHDOG_LOG_TAG "proc_write invalid parameter\n");
		return -EINVAL;
	}

	if (copy_from_user(str, buf, count)) {
		pr_err(INIT_WATCHDOG_LOG_TAG "copy_from_user failed\n");
		return -EFAULT;
	}

	if (likely(!strncmp(str, "kick", strlen(str)))) {
		pr_info(INIT_WATCHDOG_LOG_TAG "init process kicked\n");
		g_init_watchdog_data->state = STATE_CHECKED;
		g_init_watchdog_data->kick_state = STATE_KICKED;
	} else if (!strncmp(str, "enalbe", strlen(str))) {
		pr_info(INIT_WATCHDOG_LOG_TAG "init watchdog is enabled\n");
		g_init_watchdog_data->state = STATE_ENABLE;
	} else if (unlikely(!strncmp(str, "disable", strlen(str)))) {
		pr_info(INIT_WATCHDOG_LOG_TAG "init watchdog is disabled\n");
		g_init_watchdog_data->state = STATE_DISABLE;
	} else {
		pr_err(INIT_WATCHDOG_LOG_TAG "invalid set value, ignore\n");
	}

	return (ssize_t)count;
}

static ssize_t init_watchdog_proc_read(struct file *filp,
	char __user *buf, size_t count, loff_t *off)
{
	if (g_init_watchdog_data->kick_state == STATE_KICKED)
		return snprintf(buf, BUF_STR_LEN, "kick\n");

	if (g_init_watchdog_data->state == STATE_CHECKED)
		return snprintf(buf, BUF_STR_LEN, "checked\n");
	else if (g_init_watchdog_data->state == STATE_ENABLE)
		return snprintf(buf, BUF_STR_LEN, "enable\n");
	else
		return snprintf(buf, BUF_STR_LEN, "disable\n");
}

static int init_watchdog_proc_show(struct seq_file *filp, void *data)
{
	seq_printf(filp, "%s called\n", __func__);
	return 0;
}

static int init_watchdog_proc_open(struct inode *inodp, struct file *filp)
{
	return single_open(filp, init_watchdog_proc_show, NULL);
}

static struct proc_ops init_watchdog_proc_fops = {
	.proc_open = init_watchdog_proc_open,
	.proc_read = init_watchdog_proc_read,
	.proc_write = init_watchdog_proc_write,
	.proc_lseek = seq_lseek,
	.proc_release = seq_release,
};

static void init_check_timer_func(struct timer_list *t)
{
	mod_timer(t, jiffies + HZ * INIT_WATCHDOG_CHECK_INTERVAL);
	schedule_work(&g_init_watchdog_data->work);
}

static int __init init_watchdog_init(void)
{
	/* vmalloc init_watchdog_data */
	g_init_watchdog_data = kzalloc(sizeof(*g_init_watchdog_data), GFP_KERNEL);
	if (g_init_watchdog_data == NULL) {
		pr_err(INIT_WATCHDOG_LOG_TAG "kzalloc init_watchdog_data failed\n");
		return -1;
	}

	/* create proc node */
	if (proc_create(PROC_INIT_WATCHDOG, 0660, NULL, &init_watchdog_proc_fops) == NULL) {
		pr_err(INIT_WATCHDOG_LOG_TAG "proc_create failed\n");
		kfree(g_init_watchdog_data);
		return -1;
	}

	g_init_watchdog_data->state = STATE_DISABLE;
	g_init_watchdog_data->kick_state = STATE_NOT_KICK;
	g_init_watchdog_data->block_count = 0;

	/* create work, init timer */
	INIT_WORK(&g_init_watchdog_data->work, init_check_work_handler);

	timer_setup(&g_init_watchdog_data->timer, init_check_timer_func,
		TIMER_DEFERRABLE);
	mod_timer(&g_init_watchdog_data->timer,
		jiffies + HZ * INIT_WATCHDOG_CHECK_INTERVAL);

	return 0;
}

static void __exit init_watchdog_exit(void)
{
	del_timer_sync(&g_init_watchdog_data->timer);
	cancel_work_sync(&g_init_watchdog_data->work);
	remove_proc_entry(PROC_INIT_WATCHDOG, NULL);
	kfree(g_init_watchdog_data);
	g_init_watchdog_data = NULL;
}

module_init(init_watchdog_init);
module_exit(init_watchdog_exit);

MODULE_DESCRIPTION("oplus_init_watchdog");
MODULE_VERSION("1.0");
MODULE_LICENSE("GPL v2");

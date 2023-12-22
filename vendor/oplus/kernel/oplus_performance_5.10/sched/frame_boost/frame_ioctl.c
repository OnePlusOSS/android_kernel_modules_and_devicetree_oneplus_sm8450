// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020 Oplus. All rights reserved.
 */

#include <linux/ioctl.h>
#include <linux/compat.h>
#include <linux/sysctl.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <../fs/proc/internal.h>

#include <../kernel/oplus_perf_sched/sched_assist/sa_common.h>
#include "frame_boost.h"

static struct proc_dir_entry *frame_boost_proc;
int stune_boost[BOOST_MAX_TYPE];

static void crtl_update_refresh_rate(int pid, unsigned int vsyncNs)
{
	unsigned int frame_rate =  NSEC_PER_SEC / (unsigned int)(vsyncNs);
	bool is_sf = false;

	is_sf = (oplus_get_im_flag(current) == IM_FLAG_SURFACEFLINGER);

	if (is_sf) {
		set_max_frame_rate(frame_rate);
		set_frame_group_window_size(vsyncNs);
		return;
	}

	if ((pid != current->pid) || (pid != get_frame_group_ui()))
		return;

	if (set_frame_rate(frame_rate))
		set_frame_group_window_size(vsyncNs);
}

/*********************************
 * frame boost ioctl proc
 *********************************/
static long ofb_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	long ret = 0;
	struct ofb_ctrl_data data;
	void __user *uarg = (void __user *)arg;

	if (_IOC_TYPE(cmd) != OFB_MAGIC)
		return -EINVAL;

	if (_IOC_NR(cmd) >= CMD_ID_MAX)
		return -EINVAL;

	if (copy_from_user(&data, uarg, sizeof(data))) {
		ofb_err("invalid address");
		return -EFAULT;
	}

	switch (cmd) {
	case CMD_ID_SET_FPS:
		if (data.vsyncNs <= 0)
			return -EINVAL;
		crtl_update_refresh_rate(data.pid, (unsigned int)data.vsyncNs);
		break;
	case CMD_ID_BOOST_HIT:
		/* App which is not our frame boost target may request frame vsync(like systemui),
		 * just ignore hint from them! Return zero to avoid too many androd error log
		 */
		if ((data.pid != current->pid) || (data.pid != get_frame_group_ui()))
			return ret;

		if (data.stage == BOOST_FRAME_START) {
			set_frame_state(FRAME_START);
			rollover_frame_group_window(false);
			default_group_update_cpufreq();
		}

		if (data.stage == BOOST_OBTAIN_VIEW) {
			if (is_high_frame_rate()) {
				set_frame_util_min(data.util_min, true);
				default_group_update_cpufreq();
			}
		}

		if (data.stage == BOOST_SET_RENDER_THREAD)
			set_render_thread(data.pid, data.tid);

		if (data.stage == BOOST_FRAME_TIMEOUT) {
			if (is_high_frame_rate() && check_putil_over_thresh(205)) {
				set_frame_util_min(data.util_min, true);
				default_group_update_cpufreq();
			}
		}

		break;
	case CMD_ID_END_FRAME:
		/* ofb_debug("CMD_ID_END_FRAME pid:%d tid:%d", data.pid, data.tid); */
		break;
	case CMD_ID_SF_FRAME_MISSED:
		/* ofb_debug("CMD_ID_END_FRAME pid:%d tid:%d", data.pid, data.tid); */
		break;
	case CMD_ID_SF_COMPOSE_HINT:
		/* ofb_debug("CMD_ID_END_FRAME pid:%d tid:%d", data.pid, data.tid); */
		break;
	case CMD_ID_IS_HWUI_RT:
		/* ofb_debug("CMD_ID_END_FRAME pid:%d tid:%d", data.pid, data.tid); */
		break;
	case CMD_ID_SET_TASK_TAGGING:
		/* ofb_debug("CMD_ID_END_FRAME pid:%d tid:%d", data.pid, data.tid); */
		break;
	default:
		/* ret = -EINVAL; */
		break;
	}

	return ret;
}

static long ofb_sys_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	long ret = 0;
	struct ofb_ctrl_data data;
	void __user *uarg = (void __user *)arg;

	if (_IOC_TYPE(cmd) != OFB_MAGIC)
		return -EINVAL;

	if (_IOC_NR(cmd) >= CMD_ID_MAX)
		return -EINVAL;

	if (copy_from_user(&data, uarg, sizeof(data))) {
		ofb_debug("invalid address");
		return -EFAULT;
	}

	switch (cmd) {
	case CMD_ID_BOOST_HIT:
		if (data.stage == BOOST_MOVE_FG) {
			set_ui_thread(data.pid, data.tid);
			set_render_thread(data.pid, data.tid);
			set_frame_state(FRAME_END);
			rollover_frame_group_window(false);
		}

		if (data.stage == BOOST_ALL_STAGE) {
			if (data.boost_migr != INVALID_VAL)
				stune_boost[BOOST_DEF_MIGR] = data.boost_migr;

			if (data.boost_freq != INVALID_VAL)
				stune_boost[BOOST_DEF_FREQ] = data.boost_freq;

			if (data.vutil_margin != INVALID_VAL)
				set_frame_margin(data.vutil_margin);
		}

		if (data.stage == BOOST_ADD_FRAME_TASK)
			add_rm_related_frame_task(data.pid, data.tid, data.capacity_need,
				data.related_depth, data.related_width);

		break;
	case CMD_ID_SET_SF_MSG_TRANS:
		if (data.stage == BOOST_MSG_TRANS_START)
			rollover_frame_group_window(true);

		if (data.stage == BOOST_CLIENT_COMPOSITION) {
			bool boost_allow = true;

			/* This frame is not using client composition if data.level is zero.
			 * But we still keep client composition setting with one frame extension.
			 */
			if (check_last_compose_time(data.level) && !data.level)
				boost_allow = false;

			if (boost_allow && data.boost_migr != INVALID_VAL)
				stune_boost[BOOST_SF_MIGR] = data.boost_migr;

			if (boost_allow && data.boost_freq != INVALID_VAL)
				stune_boost[BOOST_SF_FREQ] = data.boost_freq;
		}

		if (data.stage == BOOST_SF_EXECUTE) {
			if (data.pid == data.tid) {
				set_sf_thread(data.pid, data.tid);
			} else {
				set_renderengine_thread(data.pid, data.tid);
			}
		}

		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long ofb_ctrl_compat_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	return ofb_ioctl(file, cmd, (unsigned long)(compat_ptr(arg)));
}

static long ofb_sys_ctrl_compat_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	return ofb_sys_ioctl(file, cmd, (unsigned long)(compat_ptr(arg)));
}
#endif

static int ofb_ctrl_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int ofb_ctrl_release(struct inode *inode, struct file *file)
{
	return 0;
}

static const struct proc_ops ofb_ctrl_fops = {
	.proc_ioctl	= ofb_ioctl,
	.proc_open	= ofb_ctrl_open,
	.proc_release	= ofb_ctrl_release,
#ifdef CONFIG_COMPAT
	.proc_compat_ioctl	= ofb_ctrl_compat_ioctl,
#endif
};

static const struct proc_ops ofb_sys_ctrl_fops = {
	.proc_ioctl	= ofb_sys_ioctl,
	.proc_open	= ofb_ctrl_open,
	.proc_release	= ofb_ctrl_release,
#ifdef CONFIG_COMPAT
	.proc_compat_ioctl	= ofb_sys_ctrl_compat_ioctl,
#endif
};

static ssize_t proc_stune_boost_write(struct file *file, const char __user *buf,
		size_t count, loff_t *ppos)
{
	int i = 0, err;
	char buffer[32];
	char *temp_str, *token;
	char str_val[BOOST_MAX_TYPE][8];

	memset(buffer, 0, sizeof(buffer));

	if (count > sizeof(buffer) - 1)
		count = sizeof(buffer) - 1;

	if (copy_from_user(buffer, buf, count))
		return -EFAULT;

	buffer[count] = '\0';
	temp_str = strstrip(buffer);
	while ((token = strsep(&temp_str, " ")) && *token && (i < BOOST_MAX_TYPE)) {
		int boost_val = 0;

		strlcpy(str_val[i], token, sizeof(str_val[i]));
		err = kstrtoint(strstrip(str_val[i]), 10, &boost_val);
		if (err)
			ofb_err("failed to write boost param (i=%d str=%s)\n", i, str_val[i]);

		if (boost_val >= 0 && i < BOOST_MAX_TYPE) {
			stune_boost[i] = min(boost_val, 100);
			ofb_debug("write boost param=%d, i=%d\n", boost_val, i);
		}

		i++;
	}

	return count;
}

static ssize_t proc_stune_boost_read(struct file *file, char __user *buf,
		size_t count, loff_t *ppos)
{
	char buffer[32];
	int i;
	size_t len = 0;

	for (i = 0; i < BOOST_MAX_TYPE; ++i)
		len += snprintf(buffer + len, sizeof(buffer) - len, "%d ", stune_boost[i]);

	len += snprintf(buffer + len, sizeof(buffer) - len, "\n");

	return simple_read_from_buffer(buf, count, ppos, buffer, len);
}

static const struct proc_ops ofb_stune_boost_fops = {
	.proc_write		= proc_stune_boost_write,
	.proc_read		= proc_stune_boost_read,
};

static int info_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, info_show, NULL);
}

static const struct proc_ops ofb_frame_group_info_fops = {
	.proc_open	= info_proc_open,
	.proc_read	= seq_read,
	.proc_lseek	= seq_lseek,
	.proc_release	= single_release,
};

#define GLOBAL_SYSTEM_UID KUIDT_INIT(1000)
#define GLOBAL_SYSTEM_GID KGIDT_INIT(1000)
int frame_ioctl_init(void)
{
	int ret = 0;
	struct proc_dir_entry *pentry;

	frame_boost_proc = proc_mkdir(FRAMEBOOST_PROC_NODE, NULL);

	pentry = proc_create("ctrl", S_IRWXUGO, frame_boost_proc, &ofb_ctrl_fops);
	if (!pentry)
		goto ERROR_INIT;

	pentry = proc_create("sys_ctrl", (S_IRWXU|S_IRWXG), frame_boost_proc, &ofb_sys_ctrl_fops);
	if (!pentry) {
		goto ERROR_INIT;
	} else {
		pentry->uid = GLOBAL_SYSTEM_UID;
		pentry->gid = GLOBAL_SYSTEM_GID;
	}

	pentry = proc_create("stune_boost", (S_IRUGO|S_IWUSR|S_IWGRP), frame_boost_proc, &ofb_stune_boost_fops);
	if (!pentry)
		goto ERROR_INIT;

	pentry = proc_create("info", S_IRUGO, frame_boost_proc, &ofb_frame_group_info_fops);
	if (!pentry)
		goto ERROR_INIT;

	return ret;

ERROR_INIT:
	remove_proc_entry(FRAMEBOOST_PROC_NODE, NULL);
	return -ENOENT;
}

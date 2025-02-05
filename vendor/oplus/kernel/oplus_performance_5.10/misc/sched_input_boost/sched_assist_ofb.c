#include <linux/init.h>
#include <linux/module.h>
#include <linux/ioctl.h>
#include <linux/compat.h>
#include <linux/sysctl.h>
#include <linux/proc_fs.h>
#include <trace/hooks/binder.h>
#include <trace/hooks/sched.h>
#include <trace/events/task.h>

#include "sched_assist_ofb.h"
#include "frame_boost_group.h"
#include "frame_boost_binder.h"
#include "webview_boost.h"
#include "cluster_boost.h"
#include "frame_info.h"
#include "walt.h"
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_SCHED_ASSIST)
#include <../kernel/oplus_perf_sched/sched_assist/sa_common.h>
#endif

static struct proc_dir_entry *frame_boost_proc = NULL;
static atomic_t fbg_pid = ATOMIC_INIT(-1);
static atomic_t fbg_tid = ATOMIC_INIT(-1);
extern atomic_t start_frame;
extern int ishighfps;
extern struct frame_boost_group default_frame_boost_group;
extern int stune_boost;
extern int oplus_input_boost_init(void);
extern unsigned int timeout_load;
#define DEBUG 0

#define ofb_debug(fmt, ...) \
	printk_deferred(KERN_INFO "[frame_boost][%s]"fmt, __func__, ##__VA_ARGS__)

void ctrl_set_fbg(int pid, int tid, int hwtid1, int hwtid2)
{
	if (pid != atomic_read(&fbg_pid))
		atomic_set(&fbg_pid, pid);

	if (tid != atomic_read(&fbg_tid))
		atomic_set(&fbg_tid, tid);

	update_frame_thread(pid, tid);
	set_frame_timestamp(FRAME_END);

	if (DEBUG) {
		unsigned long irqflag;
		struct task_struct *p = NULL;
		struct walt_task_struct *wts;
		struct frame_boost_group *grp = &default_frame_boost_group;
		raw_spin_lock_irqsave(&grp->lock, irqflag);

		list_for_each_entry(wts, &grp->tasks, fbg_list) {
			p = wts_to_ts(wts);
			ofb_debug("grp_thread pid:%d comm:%s depth:%d", p->pid, p->comm, wts->fbg_depth);
		}
		if (list_empty(&grp->tasks)) {
			ofb_debug("group is empty");
		}
		raw_spin_unlock_irqrestore(&grp->lock, irqflag);
	}
}

void ctrl_frame_state(int pid, bool is_enter)
{
	if ((pid != current->pid) || (pid != atomic_read(&fbg_pid)))
		return;

	update_frame_state(pid, is_enter);
}

unsigned int max_rate;
extern raw_spinlock_t fbg_lock;
extern struct frame_info global_frame_info;
char sf[] = "surfaceflinger";
void crtl_update_refresh_rate(int pid, int64_t vsyncNs)
{
	unsigned long flags;
	unsigned int frame_rate =  NSEC_PER_SEC / (unsigned int)(vsyncNs);
	bool is_sf = false;

#if IS_ENABLED(CONFIG_OPLUS_FEATURE_SCHED_ASSIST)
	is_sf = (oplus_get_im_flag(current) == IM_FLAG_SURFACEFLINGER || strstr(current->comm, sf));
#endif
	if (!is_sf && frame_rate == global_frame_info.frame_qos)
		return;

	raw_spin_lock_irqsave(&fbg_lock, flags);
	if (is_sf) {
		max_rate = frame_rate;
	} else if (pid != atomic_read(&fbg_pid) || frame_rate > max_rate) {
		raw_spin_unlock_irqrestore(&fbg_lock, flags);
		return;
	}

	set_frame_rate(frame_rate);
	sched_set_group_window_size(vsyncNs);
	raw_spin_unlock_irqrestore(&fbg_lock, flags);
}
void ctrl_frame_obt(int pid)
{
	if (atomic_read(&start_frame) == 0)
		return;

	if ((pid != current->pid) || (pid != atomic_read(&fbg_pid)))
		return;

	if (!ishighfps)
		return;

	set_frame_min_util(600, true);
}

void ctrl_set_render(int pid , int tid)
{
	if ((pid != current->pid) || (pid != atomic_read(&fbg_pid)))
		return;

	update_frame_thread(pid, tid);
}

void ctrl_set_hwuitasks(int pid , int hwtid1, int hwtid2)
{
	if (pid != atomic_read(&fbg_pid))
		return;

	update_hwui_tasks(hwtid1, hwtid2);
}
void ctrl_set_timeout(int pid)
{
	unsigned long curr_load = 0;
	struct frame_info *frame_info = NULL;
	struct frame_boost_group *grp = NULL;

	if (atomic_read(&start_frame) == 0)
		return;

	if ((pid != current->pid) || (pid != atomic_read(&fbg_pid)))
		return;

	if (!ishighfps)
		return;

	grp = frame_grp();
	rcu_read_lock();
	frame_info = fbg_frame_info(grp);
	rcu_read_unlock();

	if (!frame_info)
		return;

	curr_load = calc_frame_load(frame_info, false);
	if (curr_load > timeout_load) {
		set_frame_min_util(500, true);
	}
}

static long ofb_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	long ret = 0;
	struct ofb_ctrl_data data;
	void __user *uarg = (void __user *)arg;

	if (_IOC_TYPE(cmd) != OFB_MAGIC)
		return -ENOTTY;

	if (_IOC_NR(cmd) >= CMD_ID_MAX)
		return -ENOTTY;

	if (copy_from_user(&data, uarg, sizeof(data))) {
		ofb_debug("invalid address");
		return -EFAULT;
	}

	if (unlikely(walt_disabled))
		return -EFAULT;

	switch (cmd) {
	case CMD_ID_SET_FPS:
		crtl_update_refresh_rate(data.pid, data.vsyncNs);
		break;
	case CMD_ID_BOOST_HIT:
		if (data.stage == BOOST_FRAME_START) {
			ctrl_frame_state(data.pid, true);
		}
		if (data.stage == BOOST_OBTAIN_VIEW) {
			ctrl_frame_obt(data.pid);
		}
		if (data.stage == BOOST_SET_RENDER_THREAD) {
			ctrl_set_render(data.pid, data.tid);
		}
		if (data.stage == BOOST_FRAME_TIMEOUT) {
			ctrl_set_timeout(data.pid);
		}
		break;
	case CMD_ID_END_FRAME:
		break;
	case CMD_ID_SF_FRAME_MISSED:
		break;
	case CMD_ID_SF_COMPOSE_HINT:
		break;
	case CMD_ID_IS_HWUI_RT:
		break;
	case CMD_ID_SET_TASK_TAGGING:
		break;
	default:
		ret = -ENOTTY;
		break;
	}

	return ret;
}

static bool is_ofb_extra_cmd(unsigned int cmd)
{
	if (cmd == CMD_ID_SET_TASK_PREFERED_CLUSTER) {
		return true;
	}

	return false;
}

static long handle_ofb_extra_cmd(unsigned int cmd, void __user *uarg)
{
	long ret = 0;

	if (unlikely(walt_disabled))
		return -EFAULT;

	switch (cmd) {
	case CMD_ID_SET_TASK_PREFERED_CLUSTER:
		return fbg_set_task_preferred_cluster(uarg);
	default:
		ret = -ENOTTY;
		break;
	}

	return ret;
}

static long sys_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	long ret = 0;
	struct ofb_ctrl_data data;
	void __user *uarg = (void __user *)arg;

	if (_IOC_TYPE(cmd) != OFB_MAGIC)
		return -ENOTTY;

	if (is_ofb_extra_cmd(cmd)) {
		return handle_ofb_extra_cmd(cmd, uarg);
	}

	if (_IOC_NR(cmd) >= CMD_ID_MAX)
		return -ENOTTY;

	if (copy_from_user(&data, uarg, sizeof(data))) {
		ofb_debug("invalid address");
		return -EFAULT;
	}

	if (unlikely(walt_disabled))
		return -EFAULT;

	switch (cmd) {
	case CMD_ID_BOOST_HIT:
		if (data.stage == BOOST_MOVE_FG) {
			ctrl_set_fbg(data.pid, data.tid, data.hwtid1, data.hwtid2);
		}

		break;
	default:
		ret = -ENOTTY;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long ofb_ctrl_compat_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	return ofb_ioctl(file, cmd, (unsigned long)(compat_ptr(arg)));
}

static long sys_ctrl_compat_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	return sys_ioctl(file, cmd, (unsigned long)(compat_ptr(arg)));
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
	.proc_ioctl    = ofb_ioctl,
	.proc_open		 = ofb_ctrl_open,
	.proc_release	= ofb_ctrl_release,
#ifdef CONFIG_COMPAT
	.proc_compat_ioctl = ofb_ctrl_compat_ioctl,
#endif
};

static const struct proc_ops sys_ctrl_fops = {
	.proc_ioctl    = sys_ioctl,
	.proc_open		 = ofb_ctrl_open,
	.proc_release	= ofb_ctrl_release,
#ifdef CONFIG_COMPAT
	.proc_compat_ioctl = sys_ctrl_compat_ioctl,
#endif
};

void register_fbg_vendor_hooks(void)
{
	/* register vender hook in driver/android/binder.c */
	register_trace_android_vh_binder_wakeup_ilocked(fbg_binder_wakeup_hook, NULL);
	register_trace_android_vh_binder_restore_priority(fbg_binder_restore_priority_hook, NULL);
	register_trace_android_vh_binder_wait_for_work(fbg_binder_wait_for_work_hook, NULL);
	register_trace_android_vh_sync_txn_recvd(fbg_sync_txn_recvd_hook, NULL);

	/* register vendor hook in fs/exec.c*/
	register_trace_task_rename(task_rename_hook, NULL);
}


int frame_boost_init(void)
{
	int ret = 0;
	struct proc_dir_entry *pentry;
	frame_boost_proc = proc_mkdir(FRAMEBOOST_PROC_NODE, NULL);

	pentry = proc_create("ctrl", S_IRWXUGO, frame_boost_proc, &ofb_ctrl_fops);
	if(!pentry) {
		goto ERROR_INIT_VERSION;
	}

	pentry = proc_create("sys_ctrl", (S_IRWXU|S_IRWXG), frame_boost_proc, &sys_ctrl_fops);
	if(!pentry) {
		goto ERROR_INIT_VERSION;
	}

	fbg_cluster_boost_init(frame_boost_proc);
	register_fbg_vendor_hooks();
	ret = oplus_input_boost_init();
	return ret;
ERROR_INIT_VERSION:
	remove_proc_entry(FRAMEBOOST_PROC_NODE, NULL);
	return -ENOENT;
}

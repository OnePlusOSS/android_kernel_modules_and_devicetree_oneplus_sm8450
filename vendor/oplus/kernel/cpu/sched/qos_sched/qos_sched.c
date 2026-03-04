// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Oplus. All rights reserved.
 */

#include "linux/err.h"
#include "linux/types.h"
#include <linux/module.h>
#include <linux/version.h>
#include <linux/compat.h>
#include <linux/proc_fs.h>
#include <linux/cgroup.h>
#include <linux/sched/prio.h>
#include <linux/sched.h>
#include <linux/sched/cputime.h>
#include <kernel/sched/sched.h>
#include <uapi/linux/sched/types.h>
#include <trace/hooks/sched.h>

#include "qos_sched.h"
#include "../sched_assist/sa_common.h"
#include "../sched_assist/sa_sysfs.h"
#include "qos_sched_lut.h"


#define INVALID_VAL (INT_MIN)

#define VERSION	"1.0"			/* Module version */
#define NAME	"oplus_qos_sched"	/* Module name */

#define QOS_SCHED_DIR "oplus_qos_sched"
#define QOS_PID_LEVEL_MASK 0xFFFFFF

static struct proc_dir_entry *qos_sched_dir;
static int local_per_task_read_pid;

atomic_long_t latency_pid;
atomic_long_t uclamp_pid;
atomic_long_t prio_pid;

typedef struct qos_sched_node {
	char *name;
	umode_t mode;
	const struct proc_ops *opts;
} qos_sched_node_t;

noinline int tracing_mark_write(const char *buf)
{
	trace_printk(buf);
	return 0;
}

static int qs_get_task_qos_level(struct task_struct *p)
{
	struct oplus_task_struct *ots;
	int level = -1;

	get_task_struct(p);
	ots = get_oplus_task_struct(p);
	if (IS_ERR_OR_NULL(ots)) {
		put_task_struct(p);
		return level;
	}

	if (ots->qos_level != -1)
		level = ots->qos_level;

	put_task_struct(p);

	return level;
}

static void qs_set_task_uclamp(struct task_struct *task, int min, int max)
{
	unsigned long cur_min = 0, cur_max = 0;
	u64 uclamp_flag = SCHED_FLAG_UTIL_CLAMP;
	struct sched_attr attr = { };

	if (QOS_SCHED_UCLAMP_DEFAULT == min)
		uclamp_flag &= ~SCHED_FLAG_UTIL_CLAMP_MIN;

	if (QOS_SCHED_UCLAMP_DEFAULT == max)
		uclamp_flag &= ~SCHED_FLAG_UTIL_CLAMP_MAX;

	if (min < QOS_SCHED_UCLAMP_DEFAULT ||
		max < QOS_SCHED_UCLAMP_DEFAULT)
		return;

	if (!uclamp_flag)
		return;

	if (!task || !pid_alive(task))
		return;

	attr.sched_policy = task->policy;
	attr.sched_flags =
		SCHED_FLAG_KEEP_ALL |
		uclamp_flag |
		SCHED_FLAG_RESET_ON_FORK;
	attr.sched_util_min = min;
	attr.sched_util_max = max;

	cur_min = uclamp_eff_value(task, UCLAMP_MIN);
	cur_max = uclamp_eff_value(task, UCLAMP_MAX);

	if (cur_min != attr.sched_util_min ||
		cur_max != attr.sched_util_max) {
		if (rt_policy(task->policy))
			attr.sched_priority = task->rt_priority;
		sched_setattr_nocheck(task, &attr);
	}
}

static void qs_set_task_rt_prio(struct task_struct *p, int prio, int policy)
{
	struct sched_param param;

	if (!rt_policy(policy))
		return;

	param.sched_priority = MAX_RT_PRIO - 1 - prio;
	sched_setscheduler(p, policy, &param);
}

static void qs_set_task_fair_prio(struct task_struct *p, int prio)
{
	int nice = PRIO_TO_NICE(prio);
	set_user_nice(p, nice);
}

static void qs_reset_task_prio(struct task_struct *p);

static void qs_set_task_prio(struct task_struct *p, int prio, int policy)
{
	int prio_bak = 0;
	struct oplus_task_struct *ots = get_oplus_task_struct(p);

	if (IS_ERR_OR_NULL(ots))
		return;

	if (prio <= QOS_SCHED_PRIO_DEFAULT || prio >= MAX_PRIO) {
		pr_err("invalid or default qos prio\n");
		return;
	}

	if (QOS_SCHED_PRIO_RESET == prio) {
		qs_reset_task_prio(p);
		return;
	}

	prio_bak += p->policy;
	prio_bak += p->prio * 10;

	WRITE_ONCE(ots->qos_recover_prio, prio_bak);

	if (rt_prio(prio))
		qs_set_task_rt_prio(p, prio, policy);
	else {
		if (!fair_policy(policy)) {
			pr_err("mixed task %d policy %d and prio %d\n", p->pid, policy, prio);
			return;
		}
		if (rt_task(p)) {
			struct sched_attr attr = {
				.sched_policy	= policy,
				.sched_nice	= PRIO_TO_NICE(prio),
			};
			sched_setattr_nocheck(p, &attr);
			return;
		}
		qs_set_task_fair_prio(p, prio);
	}
}

static void qs_reset_task_prio(struct task_struct *task)
{
	struct oplus_task_struct *ots = get_oplus_task_struct(task);
	int policy;

	if (IS_ERR_OR_NULL(ots))
		return;

	/*don't reset qos task prio before setting it*/
	if (ots->qos_recover_prio < 0)
		return;

	policy = do_div(ots->qos_recover_prio, 10);
	qs_set_task_prio(task, ots->qos_recover_prio, policy);
	WRITE_ONCE(ots->qos_recover_prio, -1);
}

/*task qos support prio/latency/uclamp setting*/
static void qs_set_task_qos(struct task_struct *p, struct qos_lut_item *item)
{
	int prio;
	s64 latency;
	u8 latency_magic;

	if (!item)
		return;

	prio = item->share_or_prio.prio;

	/*sched policy deafault setting*/
	qs_set_task_prio(p, prio, rt_prio(prio) ? SCHED_FIFO : SCHED_NORMAL);

	qs_set_task_uclamp(p, item->uclamp_min, item->uclamp_max);

	/*qos latency setting*/
	latency = item->latency;
	latency_magic = (latency & SCHED_QOS_LATENCY_MAGIC_MASK) >> SCHED_QOS_LATENCY_MAGIC_SHIFT;
	if (SCHED_QOS_LATENCY_MAGIC == latency_magic) {
		latency &= ~(SCHED_QOS_LATENCY_MAGIC_MASK);
		write_task_ux(p->tgid, p->pid, (int)latency, true);
	}
}

static bool qs_set_qos_level_by_tid(int tid, struct qos_lut_item *item)
{
	struct task_struct *p;
	struct oplus_task_struct *ots;

	if (IS_ERR_OR_NULL(item))
		return false;

	rcu_read_lock();
	p = find_task_by_vpid(tid);
	if (IS_ERR_OR_NULL(p)) {
		rcu_read_unlock();
		return false;
	}
	get_task_struct(p);
	rcu_read_unlock();

	ots = get_oplus_task_struct(p);
	if (IS_ERR_OR_NULL(ots)) {
		put_task_struct(p);
		return false;
	}

	mutex_lock(&ots->qs_mutex);
	qs_set_task_qos(p, item);
	ots->qos_level = item->qos_level;
	mutex_unlock(&ots->qs_mutex);

	put_task_struct(p);
	return true;
}

static bool qs_set_qos_level_by_pid(int pid, struct qos_lut_item *item, bool pid_qos_active)
{
	struct task_struct *p, *t;
	struct oplus_task_struct *ots;

	rcu_read_lock();
	p = find_task_by_vpid(pid);
	if (IS_ERR_OR_NULL(p)) {
                rcu_read_unlock();
		return false;
	}
	get_task_struct(p);
	rcu_read_unlock();

	ots = get_oplus_task_struct(p);
	if (IS_ERR_OR_NULL(ots)) {
		put_task_struct(p);
		return false;
	}

	for_each_thread(p, t) {
		qs_set_qos_level_by_tid(t->pid, item);
	}

	ots->qos_level |= pid_qos_active << SCHED_PIDQOS_ACTIVE_MAGIC_SHIFT;
	put_task_struct(p);

	return true;
}

static long qs_ctrl_ioctl(struct file *file, unsigned int cmd,
			  unsigned long __arg)
{
	struct qos_sched_ioctl_data data;
	void __user *arg = (void __user *) __arg;
	long ret = 0;
	int i = 0;
	int qos_level_pid;
	bool pid_qos_active;
	struct qos_lut_item *item;


	if (_IOC_TYPE(cmd) != QOS_SCHED_MAGIC ||
		_IOC_NR(cmd) >= SET_LEVEL_MAX) {
		return -EINVAL;
	}

	if (copy_from_user(&data, (void __user *)arg, sizeof(data))) {
		pr_err("Invalid address!!!");
		return -EFAULT;
	}

	switch (cmd) {
	case IOCTL_SET_TID_LEVEL:
		item = qs_get_lut_item(data.level, QOS_LUT_TASK);
		if (IS_ERR_OR_NULL(item)) {
			pr_err("No lut item\n");
			return -EINVAL;
		}
		pr_info("IOCTL: Set TID: %d with level: %d\n",
			data.info.tid, data.level);
		qs_set_qos_level_by_tid(data.info.tid, item);
		break;

	case IOCTL_SET_PID_LEVEL:
		qos_level_pid = data.level & QOS_PID_LEVEL_MASK;
		pid_qos_active = data.level >> SCHED_PIDQOS_ACTIVE_MAGIC_SHIFT ? 1 : 0;
		item = qs_get_lut_item(qos_level_pid, QOS_LUT_PROCESS);
		if (IS_ERR_OR_NULL(item)) {
			pr_err("No lut item\n");
			return -EINVAL;
		}
		pr_info("IOCTL: Set PID: %d with level: %d\n",
		       data.info.pid, data.level);
		qs_set_qos_level_by_pid(data.info.pid, item, pid_qos_active);
		break;

	case IOCTL_SET_TID_ARRAY_LEVEL:
		item = qs_get_lut_item(data.level, QOS_LUT_TASK);
		if (IS_ERR_OR_NULL(item)) {
			pr_err("No lut item\n");
			return -EINVAL;
		}
		pr_info("IOCTL: Set TID Array with level: %d, count: %d\n",
			data.level, data.info.tarray.count);
		for (i = 0; i < data.info.tarray.count; i++) {
			pr_info("TID[%d] = %d", i, data.info.tarray.tids[i]);
			qs_set_qos_level_by_tid(data.info.tarray.tids[i],
							item);
		}
		break;
	default:
		/* ret = -EINVAL; */
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long qs_ctrl_compat_ioctl(struct file *file, unsigned int cmd,
				 unsigned long arg)
{
	return qs_ctrl_ioctl(file, cmd, (unsigned long)(compat_ptr(arg)));
}

#endif

static int qs_ctrl_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int qs_ctrl_release(struct inode *inode, struct file *file)
{
	return 0;
}

static ssize_t task_qos_level_write(struct file *file, const char __user *buf,
				    size_t count, loff_t *ppos)
{
	char buffer[8];
	int err, val;

	memset(buffer, 0, sizeof(buffer));

	if (count > sizeof(buffer) - 1)
		count = sizeof(buffer) - 1;

	if (copy_from_user(buffer, buf, count))
		return -EFAULT;

	buffer[count] = '\0';
	err = kstrtoint(strstrip(buffer), 10, &val);
	if (err)
		return err;

	local_per_task_read_pid = val;

	return count;
}

static ssize_t task_qos_level_read(struct file *file, char __user *buf,
				   size_t count, loff_t *ppos)
{
	char buffer[64];
	size_t len = 0;
	struct task_struct *task;
	int level = -1;

	task = find_task_by_vpid(local_per_task_read_pid);
	if (task) {
		level = qs_get_task_qos_level(task);
	}
	len = snprintf(buffer, sizeof(buffer), "pid: %d, level: %d\n",
			local_per_task_read_pid, level);

	return simple_read_from_buffer(buf, count, ppos, buffer, len);
}

static int write_qos_pid(const char __user *buf, size_t count,
			 atomic_long_t *qos_pid)
{
	char buffer[MAX_OUTPUT];
	char *str, *token;
	char opt_str[OPT_STR_MAX][13] = {"0", "0", "0"};
	int cnt = 0, err = 0, pid;
	bool process = false;
	struct task_struct *task;
	uid_t uid = task_uid(current).val;
	/* only accept qos from system server or performance binder */
	if (SYSTEM_UID != uid && ROOT_UID != uid) {
		return -EFAULT;
	}
	memset(buffer, 0, sizeof(buffer));

	if (count > sizeof(buffer) - 1)
		count = sizeof(buffer) - 1;

	if (copy_from_user(buffer, buf, count))
		return -EFAULT;

	buffer[count] = '\0';
	str = strstrip(buffer);
	while ((token = strsep(&str, " ")) && *token && (cnt < OPT_STR_MAX)) {
		strlcpy(opt_str[cnt], token, sizeof(opt_str[cnt]));
		cnt += 1;
	}

	if (cnt == OPT_STR_MAX) {
		err = kstrtoint(strstrip(opt_str[OPT_STR_VAL]), 10, &pid);
		if (err)
			return err;

		if (pid < 0 || pid > PID_MAX_DEFAULT)
			return -EFAULT;

		rcu_read_lock();
		task = find_task_by_vpid(pid);
		if (task)
			get_task_struct(task);
		rcu_read_unlock();

		if (!strncmp(opt_str[OPT_STR_PID], "p", 1)
			&& task && !thread_group_leader(task)) {
			put_task_struct(task);
			return -EFAULT;
		}
		if (task)
			put_task_struct(task);

		if (!strncmp(opt_str[OPT_STR_TYPE], "r", 1)) {
			if (!strncmp(opt_str[OPT_STR_PID], "p", 1))
				process = true;
			else if (strncmp(opt_str[OPT_STR_PID], "t", 1))
				return -EFAULT;

			atomic_long_set(qos_pid, (pid << QOS_TASK_PID_FLAG_BITS) | process);

			return 0;
		}
	}

	return -EFAULT;
}

static pid_t read_qos_pid(atomic_long_t *qos_pid, bool *group_leader)
{
	unsigned long pid = atomic_long_read(qos_pid);
	*group_leader = pid & QOS_TASK_PID_FLAG_MASK;

	return (pid_t)(pid >> QOS_TASK_PID_FLAG_BITS);
}

static bool qos_task_latency(struct task_struct *task)
{
	return test_task_ux(task);
}

static int qos_task_prio(struct task_struct *task)
{
	return task->prio;
}

static int qos_task_uclamp(struct task_struct *task)
{
	int uclamp = 0;
	uclamp += uclamp_eff_value(task, UCLAMP_MIN);
	uclamp += uclamp_eff_value(task, UCLAMP_MAX) * 1000;
	return uclamp;
}

#define QOS_SCHED_TASK_FILE(name) \
static ssize_t proc_task_##name##_write(struct file *file, \
		const char __user *buf, size_t count, loff_t *ppos) \
{ \
	int err; \
	err = write_qos_pid(buf, count, &name ## _pid); \
	if (err) \
		return err; \
	return count; \
} \
static ssize_t proc_task_##name##_read(struct file *file, \
			char __user *buf, size_t count, loff_t *ppos) \
{ \
	char buffer[MAX_OUTPUT]; \
	size_t len = 0; \
	bool group_leader = false; \
	struct task_struct *task, *t; \
	pid_t pid; \
\
	pid = read_qos_pid(& name ## _pid, &group_leader); \
\
	if (pid < 0 || pid > PID_MAX_DEFAULT) \
		return -EFAULT; \
\
	rcu_read_lock(); \
	task = find_task_by_vpid(pid); \
	if (task) \
		get_task_struct(task); \
	rcu_read_unlock(); \
\
	if (task) { \
		rcu_read_lock(); \
		if (group_leader) { \
			for_each_thread(task, t) { \
				len += snprintf(buffer + len, sizeof(buffer) - len, \
				"(%d %d) ", t->pid, qos_task_##name(t)); \
				if (len > MAX_GUARD_SIZE) { \
					len += snprintf(buffer + len, \
						sizeof(buffer) - len, "... "); \
					break; \
				} \
			} \
		} else { \
			len = snprintf(buffer, sizeof(buffer),  "(%d %d) ", \
					task->pid, qos_task_##name(task)); \
		} \
		put_task_struct(task); \
		rcu_read_unlock(); \
		buffer[len-1] = '\n'; \
	} \
\
	return simple_read_from_buffer(buf, count, ppos, buffer, len); \
} \
\
static const struct proc_ops qos_task_##name##_proc_fops = { \
	.proc_read	= proc_task_##name##_read, \
	.proc_write	= proc_task_##name##_write, \
	.proc_lseek	= default_llseek, \
}

QOS_SCHED_TASK_FILE(latency);
QOS_SCHED_TASK_FILE(prio);
QOS_SCHED_TASK_FILE(uclamp);

static const struct proc_ops qos_sched_ctrl_fops = {
	.proc_ioctl = qs_ctrl_ioctl,
	.proc_open = qs_ctrl_open,
	.proc_release = qs_ctrl_release,
	.proc_read = task_qos_level_read,
	.proc_write = task_qos_level_write,
#ifdef CONFIG_COMPAT
	.proc_compat_ioctl = qs_ctrl_compat_ioctl,
#endif
	.proc_lseek = default_llseek,
};

static long qs_lut_ioctl(struct file *file, unsigned int cmd,
			 unsigned long __arg)
{
	unsigned int version;
	struct qos_lut_ioctl_request request;
	void __user *uarg = (void __user *)__arg;

	if (_IOC_TYPE(cmd) != LUT_CTRL_MAGIC || _IOC_NR(cmd) > CTRL_LUT_MAX)
		return -EINVAL;
	if (copy_from_user(&request, uarg, sizeof(struct qos_lut_ioctl_request)))
		return -EFAULT;

	if (unlikely(request.type != QOS_LUT_TASK &&
		request.type != QOS_LUT_PROCESS &&
		request.type != QOS_LUT_GROUP)) {
		pr_err("invalid lut type %d\n", request.type);
		return -EINVAL;
	}
	pr_info("%s: lut_size = %d, type = %d, version = %lld\n", __func__,
			request.lut_size, request.type, request.version);

	switch (cmd) {
	case IOCTL_GET_LUT_VERSION:
		version = qs_get_lut_version(request.type);
		break;
	case IOCTL_UPDATE_LUT_REQUEST:
		qs_set_lut_update_type(request.type);
		if (qs_update_lut_request(&request))
			return -ENOMEM;
		break;
	default:
		break;
	}
	return 0;
}

static int qs_lut_open(struct inode *inode, struct file *file)
{
	return 0;
}

static ssize_t qs_lut_read(struct file *file, char __user *buf,
			   size_t count, loff_t *ppos)
{
	return 0;
}

static int qs_lut_release(struct inode *inode, struct file *file)
{
	return 0;
}

static ssize_t qs_lut_write(struct file *file, const char __user *buf,
			    size_t count, loff_t *ppos)
{
	return 0;
}

static int qs_lut_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct qos_lut_ctl *lut_ctl = NULL;
	struct qos_lut_info *cur_lut_info = NULL;
	unsigned long lut_table_physical_mem = 0;
	unsigned long lut_mem_size = 0;
	unsigned long pfn = 0;
	unsigned char cur;

	lut_ctl = qs_get_lut_ctl(qs_get_lut_update_type());

	if (!lut_ctl) {
		pr_info("lut_ctl is null\n");
		return -EINVAL;
	}
	pr_info("lut_version:%lld", lut_ctl->version);

	cur = lut_ctl->cur;
	cur_lut_info = lut_ctl->lut_info[cur];

	if (!cur_lut_info) {
		pr_info("cur_lut_info in null\n");
		return -EINVAL;
	}

	lut_table_physical_mem = virt_to_phys(cur_lut_info->mem);
	lut_mem_size = vma->vm_end - vma->vm_start;
	pfn = virt_to_pfn(cur_lut_info->mem);

	if (remap_pfn_range(vma, vma->vm_start, pfn,
		lut_mem_size, vma->vm_page_prot) < 0) {
		return -EAGAIN;
	}
	return 0;
}
#ifdef CONFIG_COMPAT
static long qs_lut_table_compat_ioctl(struct file *file, unsigned int cmd,
				      unsigned long arg)
{
	return qs_lut_ioctl(file, cmd, (unsigned long)(compat_ptr(arg)));
}
#endif

static const struct proc_ops qos_sched_lut_table_fops = {
	.proc_ioctl = qs_lut_ioctl,
	.proc_open = qs_lut_open,
	.proc_release = qs_lut_release,
	.proc_read = qs_lut_read,
	.proc_write = qs_lut_write,
	.proc_mmap = qs_lut_mmap,
#ifdef CONFIG_COMPAT
	.proc_compat_ioctl = qs_lut_table_compat_ioctl,
#endif
	.proc_lseek = default_llseek,
};

static qos_sched_node_t qos_sched_entries[] = {
	{ "qos_level",	0666,	&qos_sched_ctrl_fops },
	{ "qos_lut", 		0666,	&qos_sched_lut_table_fops },
	{ "qos_task_latency",	0666,	&qos_task_latency_proc_fops },
	{ "qos_task_prio",	0666,	&qos_task_prio_proc_fops },
	{ "qos_task_uclamp",	0666,	&qos_task_uclamp_proc_fops },
	{ NULL,		0		},
};

static void android_vh_setscheduler_uclamp_handler(void *unused, struct task_struct *p,
	int id, unsigned int uclamp_value)
{
	const char *uclamp_name[UCLAMP_CNT+1] = {
		[UCLAMP_MIN] = "uclamp_min",
		[UCLAMP_MAX] = "uclamp_max",
		[UCLAMP_CNT] = "invaild",
	};
	struct task_group *tg = task_group(p);
	u32 tg_uv = tg ? tg->uclamp[id].value : id ? 1024 : 0;

	char buffer[256];
	int len;
        int remaining;
	len = strlcat(buffer, "C|9999|qs_uclamp|", sizeof(buffer));
        remaining = sizeof(buffer) - len;

        if (remaining > 0) {
                snprintf(buffer + len, remaining, "task:[%s %d] uclamp req:[%s %d] tg:[%s %d]\n",
                        p->comm, p->pid, uclamp_name[id], uclamp_value,
                        tg && tg->css.cgroup ? tg->css.cgroup->kn->name : "invalid", tg_uv);
        }

	if (unlikely(global_debug_enabled & DEBUG_FTRACE))
		tracing_mark_write(buffer);
}

static bool pid_qos_active(struct oplus_task_struct *ots)
{
	int qs_lvl = ots->qos_level;
	int active_magic;
	if (qs_lvl < 0)
		return false;

	active_magic = (qs_lvl & SCHED_PIDQOS_ACTIVE_MAGIC_MASK) >> SCHED_PIDQOS_ACTIVE_MAGIC_SHIFT;
	if (SCHED_PIDQOS_ACTIVE_MAGIC == active_magic)
		return true;

	return false;
}

static void android_rvh_wake_up_new_task_handler(void *unused, struct task_struct *p)
{
	struct task_struct *leader = NULL;
	struct oplus_task_struct *l_ots = NULL;
	int qs_lvl = 0;
	struct qos_lut_item *item;

	/* group leader's qos will be set successfully, ignore it*/
	if (thread_group_leader(p))
		return;

	rcu_read_lock();
	leader = p->group_leader;
	if (leader && pid_alive(leader)) {
		l_ots =  get_oplus_task_struct(leader);
		if (!IS_ERR_OR_NULL(l_ots) && pid_qos_active(l_ots)) {
			qs_lvl = l_ots->qos_level;
			rcu_read_unlock();
			qs_lvl &= ~(SCHED_PIDQOS_ACTIVE_MAGIC_MASK);
			item = qs_get_lut_item(qs_lvl, QOS_LUT_PROCESS);
			if (IS_ERR_OR_NULL(item)) {
                                pr_err("No lut item\n");
                                return;
                        }
			qs_set_qos_level_by_tid(p->pid, item);
			return;
		}
	}
	rcu_read_unlock();
}

static void qs_register_vendor_hooks(void)
{
	register_trace_android_vh_setscheduler_uclamp(android_vh_setscheduler_uclamp_handler, NULL);
	register_trace_android_rvh_wake_up_new_task(android_rvh_wake_up_new_task_handler, NULL);
}

static int __init qos_sched_init(void)
{
	int ret = 0;
	qos_sched_node_t *entry;

	qos_sched_dir = proc_mkdir(QOS_SCHED_DIR, NULL);
	if (!qos_sched_dir) {
		pr_err("Couldn't create dir /proc/%s!", QOS_SCHED_DIR);
		ret = -ENOMEM;
		goto err_create_dir;
	}

	entry = qos_sched_entries;
	while (entry->name) {
		if (!proc_create(entry->name, entry->mode,
			qos_sched_dir, entry->opts)) {
			pr_err("Couldn't create node %s!", entry->name);
			ret = -ENOMEM;
			goto err_create_qos_attr;
		}
		entry++;
	}
	qs_register_vendor_hooks();
	pr_info("%s: version %s init successfully.", NAME, VERSION);

	return ret;

err_create_qos_attr:
	remove_proc_subtree(QOS_SCHED_DIR, NULL);
err_create_dir:
	return ret;
}

static void __exit qos_sched_exit(void)
{
	remove_proc_subtree(QOS_SCHED_DIR, NULL);
	pr_info("exit successfully.");
}

module_init(qos_sched_init);
module_exit(qos_sched_exit);
MODULE_DESCRIPTION("Oplus Qos Sched Driver");
MODULE_LICENSE("GPL v2");

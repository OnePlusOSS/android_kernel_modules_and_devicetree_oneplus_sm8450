#include <linux/kernel.h>
#include <linux/kprobes.h>
#include <linux/sysfs.h>
#include <linux/kobject.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/rcupdate.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <uapi/linux/sched/types.h>
#include <linux/pid.h>
#include <linux/rwsem.h>
#include <linux/version.h>
#include <linux/sched/signal.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <../kernel/oplus_cpu/sched/sched_assist/sa_common.h>
#include <trace/hooks/cgroup.h>
#include <linux/time.h>
#include <linux/atomic.h>

#define CREATE_TRACE_POINTS
#include "kp_freeze_trace.h"
#define KP_FREEZE_DETECT_APPID 20120
#define FREEZE_DETECT_DCS_TAG "CriticalLog"
#define FREEZE_DETECT_DCS_EVENTID "key_process_frozen"
#define KP_FREEZE_DETECT_LOG_TAG "[kp_freeze_detect]"

#define MODULE_NAME "kp_freeze_detect"

static atomic_t cgroup_hook_enabled = ATOMIC_INIT(0);
static atomic_t is_kprobe_registered = ATOMIC_INIT(0);

static char *bad_ptr;
static struct kprobe kp_cgroup_write;

 static long get_timestamp_ms(void)
{
	struct timespec64 now;
	ktime_get_real_ts64(&now);
	return timespec64_to_ns(&now) / NSEC_PER_MSEC;
 }

static bool is_key_process(struct task_struct *tsk)
{
	struct task_struct *t;
	t = tsk->group_leader;

	if(!strncmp(t->comm,"system_server", TASK_COMM_LEN)
			|| !strncmp(t->comm,"surfaceflinger", TASK_COMM_LEN) ){
		return true;
	}
	if(!strncmp(t->comm,"Binder:netd", TASK_COMM_LEN) ){
		return true;
	}
	if(!strncmp(t->comm,"Binder:vold", TASK_COMM_LEN) ){
		return true;
	}
	if(!strncmp(t->comm,"Binder:camerase", TASK_COMM_LEN) ){
		return true;
	}
	if(!strncmp(t->comm,"audioserver", TASK_COMM_LEN) ){
		return true;
	}

  	return false;
}


static int handler_pre_cgroup_procs_write(struct kprobe *p, struct pt_regs *regs)
{
	struct task_struct *task = NULL;
	pid_t pid;
	char *buf;

	if(!atomic_read(&cgroup_hook_enabled)) {
		pr_info("cgroup_hook_enabled = 0,return\n");
		return 0;
	}

	buf = (char *)regs->regs[1];

	if (kstrtoint(strstrip(buf), 0, &pid) || pid < 0)
		return -EINVAL;

	rcu_read_lock();
	task = find_task_by_vpid(pid);
	if (!task) {
		goto out_unlock;
	}

	if(is_key_process(task)) {
		pr_info(KP_FREEZE_DETECT_LOG_TAG "kprobe:key process (pid:%d comm:%s)frozen detected\n",task->pid, task->comm);
		trace_key_process_frozen(get_timestamp_ms(), KP_FREEZE_DETECT_APPID, FREEZE_DETECT_DCS_TAG, FREEZE_DETECT_DCS_EVENTID,
		task->comm);
		regs->regs[1] = (unsigned long)bad_ptr;
		goto out_unlock;
	}

out_unlock:
	rcu_read_unlock();
	return 0;
}

static int register_cgroup_kprobe(void) {
	int ret;
	if (atomic_cmpxchg(&is_kprobe_registered, 0, 1) != 0) {
		pr_info(KP_FREEZE_DETECT_LOG_TAG "cgroup_kprobe already registered\n");
		return -EBUSY;
	}

	kp_cgroup_write.pre_handler = handler_pre_cgroup_procs_write;
	kp_cgroup_write.symbol_name = "cgroup_procs_write";

	ret = register_kprobe(&kp_cgroup_write);
	if (ret < 0) {
		pr_info(KP_FREEZE_DETECT_LOG_TAG "register cgroup_kprobe failed: %d\n", ret);
		atomic_set(&is_kprobe_registered, 0);
		return ret;
	}

	pr_info(KP_FREEZE_DETECT_LOG_TAG "cgroup_kprobe registered\n");
	return 0;
}


// （cat /sys/kernel/kp_freeze_detect/kp_freeze_detect/cgroup_hook_enable）
static ssize_t cgroup_hook_enabled_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%d\n", atomic_read(&cgroup_hook_enabled));
}

// （echo 1 > /sys/kernel/kp_freeze_detect/kp_freeze_detect/cgroup_hook_enable）
static ssize_t cgroup_hook_enabled_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	int ret;
	bool val;

	ret = kstrtobool(buf, &val);
	if (ret < 0)
		return ret;

	atomic_set(&cgroup_hook_enabled, val ? 1 : 0);

	if(atomic_read(&cgroup_hook_enabled)) {
		ret = register_cgroup_kprobe();
		if (ret < 0)
			return ret;
	}

	return count;
}


static struct kobj_attribute cgroup_hook_attr = __ATTR(cgroup_hook_enabled, 0644, cgroup_hook_enabled_show, cgroup_hook_enabled_store);


static struct attribute *kp_attrs[] = {
	&cgroup_hook_attr.attr,
	NULL,
};

static struct attribute_group kp_attr_group = {
	.name = MODULE_NAME,   //  /sys/kernel/kp_freeze_detect/kp_freeze_detect
	.attrs = kp_attrs,
};

static struct kobject *kp_kobj;

static int __init kp_freeze_detect_init(void){
	int ret;

	bad_ptr = kmalloc(9, GFP_KERNEL);
	if(!bad_ptr) {
		return -ENOMEM;
	}
	snprintf(bad_ptr, 9, "nofreeze");

	kp_kobj = kobject_create_and_add(MODULE_NAME, kernel_kobj);
	if (!kp_kobj) {
		return -ENOMEM;
	}

	ret = sysfs_create_group(kp_kobj, &kp_attr_group);
	if (ret) {
		kfree(bad_ptr);
		kobject_put(kp_kobj);
		return ret;
	}

	pr_info(KP_FREEZE_DETECT_LOG_TAG "kp_freeze_detect_loaded\n");
	return ret;
}

static void __exit kp_freeze_detect_exit(void){
	pr_info(KP_FREEZE_DETECT_LOG_TAG "kp_freeze_detect exit\n");
	sysfs_remove_group(kp_kobj, &kp_attr_group);
	kobject_put(kp_kobj);
	unregister_kprobe(&kp_cgroup_write);
	kfree(bad_ptr);
}

module_init(kp_freeze_detect_init);
module_exit(kp_freeze_detect_exit);
MODULE_LICENSE("GPL");
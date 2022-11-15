/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2020 Oplus. All rights reserved.
 */


#ifndef _OPLUS_OB_CONTROL_H_
#define _OPLUS_OB_CONTROL_H_
#define OBPROC_CHECK_CYCLE_NS 100000000
#define OBWORK_TIMEOUT_NS 800000000
#define BG_THREAD (2)
#define TF_ASYNC_BOOST 0x40
#define BT_CGROUP_BACKGROUND (3)

enum {
	BINDER_LOOPER_STATE_REGISTERED  = 0x01,
	BINDER_LOOPER_STATE_ENTERED     = 0x02,
	BINDER_LOOPER_STATE_BACKGROUND  = 0x40,
};


struct ob_struct {
	struct binder_proc *ob_proc;
	struct list_head ob_list;
	pid_t pid;
	u64 ob_check_ts;
	bool init;
};

/* Please add your own members of binder_transaction here */
struct oplus_binder_transaction {
	u64 ob_begin;
};

/* kmi mismatch now, use until hook was added in binder_proc */
struct oplus_binder_proc {
	struct list_head ux_todo;
	uint32_t ux_count;
};

struct binder_proc_status{
	u64 warning;
	u64 warning_cg_bg;
	u64 async_mem_over_high;
	u64 async_mem_over_low;
	u64 sync_mem_over_high;
	u64 sync_mem_over_low;
};

struct binder_proc;
struct binder_thread;
struct binder_transaction;
extern struct binder_proc_status system_server_proc_status;
extern struct ob_struct ob_target;
extern int sysctl_ob_control_enable;
extern int sysctl_find_bg;
extern int binder_boost_pid;
extern pid_t ob_pid;
extern void obwork_check_restrict_off(struct binder_proc *proc);
extern bool obwork_is_restrict(struct binder_transaction *t);
extern bool obwork_is_async_boost(struct binder_transaction *t);
extern void obtrans_restrict_start(struct binder_proc *proc, struct binder_transaction *t);
extern void oblist_dequeue_all(void);
extern void oblist_dequeue_topapp_change(uid_t topuid);
extern void obwork_restrict(struct binder_proc *proc, struct binder_transaction *t, bool pending_async);
extern void obthread_init(struct binder_proc *proc, struct binder_thread *thread);
extern bool obthread_has_work(struct binder_thread *thread);
extern void obproc_has_work(struct binder_proc *proc);
extern void obthread_wakeup(struct binder_proc *proc);
extern struct binder_thread *obthread_get(struct binder_proc *proc, struct binder_transaction *t, bool oneway);
extern void obproc_free(struct binder_proc *proc);
extern void obprint_oblist(void);
extern int sysctl_ob_control_handler(struct ctl_table *table, int write, void __user *buffer, size_t *lenp, loff_t *ppos);
#endif /* _OPLUS_OB_CONTROL_H_ */

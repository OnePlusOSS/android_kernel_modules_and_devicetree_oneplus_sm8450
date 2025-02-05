#ifndef _FRAME_BOOST_BINDER_H_
#define _FRAME_BOOST_BINDER_H_
extern void fbg_binder_wakeup_hook(void *unused, struct task_struct *task, bool sync, struct binder_proc *proc);
extern void fbg_binder_restore_priority_hook(void *unused, struct binder_transaction *t, struct task_struct *task);
extern void fbg_binder_wait_for_work_hook(void *unused, bool do_proc_work, struct binder_thread *tsk, struct binder_proc *proc);
extern void fbg_sync_txn_recvd_hook(void *unused, struct task_struct *tsk, struct task_struct *from);
#endif

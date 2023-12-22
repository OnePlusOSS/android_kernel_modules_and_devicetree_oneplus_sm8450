#ifndef _CLUSTER_BOOST_H
#define _CLUSTER_BOOST_H
int fbg_set_task_preferred_cluster(void __user *uarg);
void fbg_cluster_boost(struct task_struct *p, int *fbg_best_cpu);
void fbg_cluster_boost_init(struct proc_dir_entry *parent);
#endif

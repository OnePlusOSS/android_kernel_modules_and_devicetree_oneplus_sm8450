
#ifndef _OPLUS_SA_GROUP_H_
#define _OPLUS_SA_GROUP_H_

#include <linux/cgroup-defs.h>

#define NR_TG_GRP (40)
#define SHARE_DEFAULT (100)
#define BG_SHARE_DEFAULT (50)
#define MAX_OUTPUT	(512)
#define EXTRA_SIZE (100)
#define MAX_GUARD_SIZE (MAX_OUTPUT - EXTRA_SIZE)

struct css_tg_map {
	struct list_head map_list;
	const char *tg_name;
	int id;
	int share_pct;
};

extern struct task_group root_task_group;
void oplus_update_tg_map(struct cgroup_subsys_state *css);
void oplus_sg_map_init(void);
void oplus_sched_group_init(struct proc_dir_entry *pde);
#endif
#include <linux/cgroup.h>
#include <linux/proc_fs.h>
#include <linux/sched/cputime.h>
#include <kernel/sched/sched.h>
#include "sa_group.h"

LIST_HEAD(css_tg_map_list);

static ssize_t tg_map_read(struct file *file, char __user *buf,
		size_t count, loff_t *ppos)
{
	char buffer[MAX_OUTPUT];
	size_t len = 0;
	struct css_tg_map *iter = NULL;

	memset(buffer, 0, sizeof(buffer));

	rcu_read_lock();
	list_for_each_entry_rcu(iter, &css_tg_map_list, map_list) {
		len += sprintf(buffer + len, "%s:%d ",
			iter->tg_name, iter->id);
		if (len > MAX_GUARD_SIZE) {
			len += sprintf(buffer + len, "... ");
			break;
		}
	}
	rcu_read_unlock();

	if (len < sizeof(buffer) - 1) {
		buffer[len++] = '\n';
	} else if (sizeof(buffer) > 0) {
		buffer[sizeof(buffer) - 1] = '\n';
		len = sizeof(buffer);
	}

	return simple_read_from_buffer(buf, count, ppos, buffer, len);
};

struct css_tg_map *get_tg_map(const char *tg_name)
{
	struct css_tg_map *iter = NULL;

	rcu_read_lock();
	list_for_each_entry_rcu(iter, &css_tg_map_list, map_list) {
		if (!(strcmp(iter->tg_name, tg_name))) {
			rcu_read_unlock();
			return iter;
		}
	}
	rcu_read_unlock();

	return NULL;
}

static const struct proc_ops tg_map_fops = {
	.proc_read		= tg_map_read,
};

struct css_tg_map *map_node_init(struct cgroup_subsys_state *css)
{
	struct cgroup *cgrp = NULL;
	struct css_tg_map *map = NULL;

	if (!css)
		return NULL;

	map = kzalloc(sizeof(struct css_tg_map), GFP_ATOMIC);
	if (!map)
		return NULL;

	cgrp = css->cgroup;
	if (cgrp && cgrp->kn) {
		map->tg_name = kstrdup_const(cgrp->kn->name, GFP_ATOMIC);
		map->id = css->id;
		return map;
	}else {
		kfree(map);
	}
	return NULL;
}

void oplus_update_tg_map(struct cgroup_subsys_state *css)
{
	struct cgroup *cgrp = css->cgroup;
	struct css_tg_map *map = NULL, *iter = NULL;

	map = map_node_init(css);
	if(!map)
		return;

	if (cgrp && cgrp->kn) {
		iter = get_tg_map(cgrp->kn->name);
		if (iter) {
			list_replace_rcu(&iter->map_list, &map->map_list);
			synchronize_rcu();
			kfree_const(iter->tg_name);
			kfree(iter);
			return;
		}
		list_add_tail_rcu(&map->map_list, &css_tg_map_list);
	}else {
		kfree(map);
		return;
	}
}
EXPORT_SYMBOL(oplus_update_tg_map);

void oplus_sg_map_init(void)
{
	struct cgroup_subsys_state *css = &root_task_group.css;
	struct cgroup_subsys_state *top_css = css;

	rcu_read_lock();
	css_for_each_child(css, top_css)
		oplus_update_tg_map(css);
	rcu_read_unlock();
}

void oplus_sched_group_init(struct proc_dir_entry *pde)
{
	struct proc_dir_entry *proc_node;

	proc_node = proc_create("tg_map", 0666, pde, &tg_map_fops);
	if (!proc_node) {
		pr_err("failed to create proc node tg_css_map\n");
		remove_proc_entry("tg_map", pde);
	}
}

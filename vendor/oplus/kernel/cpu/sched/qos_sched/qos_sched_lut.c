// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Oplus. All rights reserved.
 */


#include <linux/rcupdate.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>
#include "qos_sched.h"
#include "qos_sched_lut.h"

#define QOS_LUT_MAX (4)

static struct qos_lut_ctl lut_ctl[QOS_LUT_MAX];
static s32 qos_table_update_type;

s64 qs_get_lut_version(s32 type)
{
	s64 version;
	s32 qos_lut_index;

	qos_lut_index = type - 1;

	rcu_read_lock();
	version = lut_ctl[qos_lut_index].version;
	rcu_read_unlock();

	return version;
}

u64 qos_sched_get_latecny_by_qos_level(u64 qos_level, s32 type)
{
	u64 qos_latency;
	u8 cur;
	struct qos_lut_info *lut_info;
	struct qos_lut *qos_lut;
	struct qos_lut_item *lut_item;
	s32 qos_lut_index;

	qos_lut_index = type - 1;

	rcu_read_lock();
	cur = lut_ctl[qos_lut_index].cur;
	lut_info = lut_ctl[qos_lut_index].lut_info[cur];
	qos_lut = (struct qos_lut *)lut_info->mem;
	lut_item = qos_lut->items + qos_level;
	qos_latency = lut_item->latency;
	rcu_read_unlock();

	return qos_latency;
}

struct qos_lut_item *qs_get_lut_item(s32 level, s32 type)
{
	struct qos_lut_ctl *lut_ctl;
	struct qos_lut_item *items;
	struct qos_lut_item *item;
	s32 cur;


	if (unlikely(type != QOS_LUT_TASK &&
		type != QOS_LUT_PROCESS &&
		type != QOS_LUT_GROUP)) {
		pr_err("invalid lut type %d\n", type);
		return NULL;
	}
	rcu_read_lock();

	lut_ctl = qs_get_lut_ctl(type);
	if (!lut_ctl) {
		pr_err("Failed to get lut control\n");
		rcu_read_unlock();
		return NULL;
	}

	cur = READ_ONCE(lut_ctl->cur);
	if (unlikely(cur < 0 || cur > 1)) {
		pr_err("Invalid cur=%d\n", cur);
		rcu_read_unlock();
		return NULL;
	}
	if (!lut_ctl->lut_info[cur]) {
		pr_err("No valid lut info for cur=%d\n", cur);
		rcu_read_unlock();
		return NULL;
	}

        if (unlikely(level < 0 || level >= (lut_ctl->lut_info[cur]->lut_num))) {
                pr_err("Invalid level=%d, lut_num:%d\n", level, lut_ctl->lut_info[cur]->lut_num);
                rcu_read_unlock();
                return NULL;
        }

	items = rcu_dereference(lut_ctl->lut_info[cur]->mem);
	if (!items) {
		pr_err("No valid lut memory\n");
		rcu_read_unlock();
		return NULL;
	}

	item = &items[level];

	rcu_read_unlock();

	return item;
}

static inline void qos_sched_rollover_lut_table(struct qos_lut_ctl *table_ctl)
{
	table_ctl->cur = 1 - table_ctl->cur;
}


/**
 * @description: We use ping-pong operations to maintain two qos_lut tables.
 * @lut_size: lut_table's size
 * @return: 0 on success
 */
s32 qs_update_lut_request(struct qos_lut_ioctl_request *request)
{
	struct qos_lut_item *new_lut_table_mem = NULL;
	struct qos_lut_ctl *cur_lut_ctl;
	struct qos_lut_info *old_lut_table_info;
	struct qos_lut_info *new_lut_table_info;
	u8 new = 0;
	s32 qos_lut_index;

	qos_lut_index = request->type - 1;
	cur_lut_ctl = &lut_ctl[qos_lut_index];
	cur_lut_ctl->version = request->version;

        if (cur_lut_ctl->cur > 1) {
                pr_err("Invalid cur=%d\n", cur_lut_ctl->cur);
                return -EINVAL;
        }
        new = cur_lut_ctl->cur ^ 1;

	new_lut_table_info = (struct qos_lut_info *)vmalloc(sizeof(struct qos_lut_info));
	if (!new_lut_table_info)
		return -ENOMEM;

	new_lut_table_mem = (struct qos_lut_item *)__get_free_pages(GFP_KERNEL, get_order(request->lut_size));
	if (!new_lut_table_mem) {
                vfree(new_lut_table_info);
                return -ENOMEM;
        }

	new_lut_table_info->lut_size = request->lut_size;
        new_lut_table_info->lut_num = new_lut_table_info->lut_size / sizeof(struct qos_lut_item);
	new_lut_table_info->mem = new_lut_table_mem;

	if (!cur_lut_ctl->lut_info[new]) {
		rcu_assign_pointer(cur_lut_ctl->lut_info[new], new_lut_table_info);
	} else {
		old_lut_table_info = rcu_dereference(cur_lut_ctl->lut_info[new]);
		rcu_assign_pointer(cur_lut_ctl->lut_info[new], new_lut_table_info);
		synchronize_rcu();
                if (old_lut_table_info->mem) {
                        free_pages((unsigned long)old_lut_table_info->mem, get_order(old_lut_table_info->lut_size));
                }
		vfree(old_lut_table_info);
	}

	qos_sched_rollover_lut_table(cur_lut_ctl);
	return 0;
}

/**
 * @description: This type is updated by a native request and must hold the lock.
 * So we don't need extra lock here
 */
void qs_set_lut_update_type(s32 type)
{
	qos_table_update_type = type;
}

/**
 * @description: Updating the LUT table is performed within a serialized
 * locked critical section, so the update and retrieval of the type are safe.
 */
s32 qs_get_lut_update_type(void)
{
	return qos_table_update_type;
}

struct qos_lut_ctl *qs_get_lut_ctl(s32 type)
{
	s32 qos_lut_index;

	qos_lut_index = type - 1;
	return &lut_ctl[qos_lut_index];
}

// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024-2025 Oplus. All rights reserved.
 */

#define pr_fmt(fmt) "[MSG]([%s][%d]): " fmt, __func__, __LINE__

#include <linux/module.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/list.h>
#include <linux/workqueue.h>

#include <oplus_msg_filter.h>
#include <oplus_chg.h>

#define MSG_FILTER_MAGIC 0x10012009

struct oplus_msg_queue {
	char name[OPLUS_MSG_QUEUE_NAME_MAX + 1];
	void *private_data;
	struct oplus_msg_filter *filter;
	struct delayed_work update_work;
	struct list_head list;

	unsigned long last_update_jiffies;
	unsigned long update_request_jiffies;
	bool first_update;
};

int oplus_msg_filter_init(struct oplus_msg_filter *filter,
			  unsigned int dead_zone_ms,
			  unsigned int max_delay_ms,
			  void (*update_func)(void *))
{
	if (filter == NULL) {
		chg_err("filter is NULL\n");
		return -EINVAL;
	}
	if (update_func == NULL) {
		chg_err("update_func is NULL\n");
		return -EINVAL;
	}

	filter->magic = MSG_FILTER_MAGIC;
	spin_lock_init(&filter->list_lock);
	INIT_LIST_HEAD(&filter->queue_list);
	filter->dead_zone_jiffies = msecs_to_jiffies(dead_zone_ms);
	filter->max_delay_jiffies = msecs_to_jiffies(max_delay_ms);
	filter->update_func = update_func;

	return 0;
}

struct oplus_msg_queue *oplus_msg_filter_find_queue(
	struct oplus_msg_filter *filter, const char *name)
{
	struct oplus_msg_queue *queue;

	if (filter == NULL) {
		chg_err("filter is NULL\n");
		return NULL;
	}
	if (name == NULL) {
		chg_err("name is NULL\n");
		return NULL;
	}
	if (filter->magic != MSG_FILTER_MAGIC) {
		chg_err("filter magic error\n");
		return NULL;
	}

	spin_lock(&filter->list_lock);
	list_for_each_entry(queue, &filter->queue_list, list) {
		if (strncmp(queue->name, name, OPLUS_MSG_QUEUE_NAME_MAX) == 0) {
			spin_unlock(&filter->list_lock);
			return queue;
		}
	}
	spin_unlock(&filter->list_lock);

	return NULL;
}

static unsigned long calculate_jiffies_diff(unsigned long start, unsigned long end)
{
	if (time_after_eq(end, start))
		return end - start;
	else
		return (MAX_JIFFY_OFFSET - start + end + 1);
}

static void oplus_msg_queue_update_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_msg_queue *queue = container_of(dwork,
		struct oplus_msg_queue, update_work);

	if (queue->update_request_jiffies) {
		chg_info("%s: update delay: %u ms, update interval: %u ms\n",
			 queue->name,
			 jiffies_to_msecs(calculate_jiffies_diff(queue->update_request_jiffies, jiffies)),
			 jiffies_to_msecs(calculate_jiffies_diff(queue->last_update_jiffies, jiffies)));
		queue->update_request_jiffies = 0;
	}
	if (queue->filter->update_func)
		queue->filter->update_func(queue->private_data);
	queue->last_update_jiffies = jiffies;
}

int oplus_msg_filter_create_queue(
	struct oplus_msg_filter *filter,
	const char *name, void *data)
{
	struct oplus_msg_queue *queue;

	if (filter == NULL) {
		chg_err("filter is NULL\n");
		return -EINVAL;
	}
	if (name == NULL) {
		chg_err("name is NULL\n");
		return -EINVAL;
	}
	if (data == NULL) {
		chg_err("data is NULL\n");
		return -EINVAL;
	}
	if (filter->magic != MSG_FILTER_MAGIC) {
		chg_err("filter magic error\n");
		return -EFAULT;
	}

	if (oplus_msg_filter_find_queue(filter, name) != NULL) {
		chg_err("msg queue[%s] already exists\n", name);
		return -EFAULT;
	}

	queue = kzalloc(sizeof(struct oplus_msg_queue), GFP_KERNEL);
	if (queue == NULL) {
		chg_err("alloc msg queue error\n");
		return -ENOMEM;
	}

	snprintf(queue->name, OPLUS_MSG_QUEUE_NAME_MAX, "%s", name);
	queue->private_data = data;
	queue->filter = filter;
	queue->first_update = true;
	INIT_DELAYED_WORK(&queue->update_work, oplus_msg_queue_update_work);
	spin_lock(&filter->list_lock);
	list_add(&queue->list, &filter->queue_list);
	spin_unlock(&filter->list_lock);

	return 0;
}

int oplus_msg_filter_release_queue(struct oplus_msg_filter *filter)
{
	struct oplus_msg_queue *queue, *tmp;
	LIST_HEAD(queue_list);

	if (filter == NULL) {
		chg_err("filter is NULL\n");
		return -EINVAL;
	}
	if (filter->magic != MSG_FILTER_MAGIC) {
		chg_err("filter magic error\n");
		return -EFAULT;
	}

	spin_lock(&filter->list_lock);
	list_for_each_entry_safe(queue, tmp, &filter->queue_list, list) {
		list_del(&queue->list);
		list_add(&queue->list, &queue_list);
	}
	spin_unlock(&filter->list_lock);

	list_for_each_entry_safe(queue, tmp, &queue_list, list) {
		list_del(&queue->list);
		kfree(queue);
	}

	return 0;
}

static bool oplus_msg_queue_check_reschedule(struct oplus_msg_queue *queue, unsigned long *running_time)
{
	if (queue->update_request_jiffies == 0)
		queue->update_request_jiffies = jiffies;
	if (work_busy(&queue->update_work.work) & WORK_BUSY_RUNNING)
		return false;
	if (unlikely(queue->first_update)) {
		queue->first_update = false;
		schedule_delayed_work(&queue->update_work, 0);
		return false;
	}
	*running_time = calculate_jiffies_diff(queue->last_update_jiffies, jiffies);
	if (*running_time >= queue->filter->max_delay_jiffies) {
		schedule_delayed_work(&queue->update_work, 0);
		return false;
	}

	return true;
}

static unsigned long oplus_msg_queue_get_remaining_time(struct oplus_msg_queue *queue)
{
	unsigned long remaining_time;

	remaining_time = calculate_jiffies_diff(jiffies, queue->update_work.timer.expires);
	if (remaining_time > queue->filter->max_delay_jiffies)
		remaining_time = 0;

	return remaining_time;
}

static int oplus_msg_queue_update(struct oplus_msg_queue *queue,
				  unsigned long grace_period_jiffies)
{
	unsigned long remaining_time;
	unsigned long running_time;
	struct oplus_msg_filter *filter = queue->filter;

	chg_info("%s: update, gp=%u ms\n", queue->name, jiffies_to_msecs(grace_period_jiffies));
	if (!oplus_msg_queue_check_reschedule(queue, &running_time))
		return 0;

	if (delayed_work_pending(&queue->update_work)) {
		remaining_time = oplus_msg_queue_get_remaining_time(queue);
		if (remaining_time <= grace_period_jiffies)
			return 0;
		if ((running_time + grace_period_jiffies) < filter->dead_zone_jiffies)
			grace_period_jiffies = filter->dead_zone_jiffies - running_time;
		mod_delayed_work(system_wq, &queue->update_work, grace_period_jiffies);
	} else {
		if (running_time + grace_period_jiffies > filter->max_delay_jiffies)
			grace_period_jiffies = filter->max_delay_jiffies - running_time;
		schedule_delayed_work(&queue->update_work,
			max(filter->dead_zone_jiffies, grace_period_jiffies));
	}

	return 0;
}

int oplus_msg_filter_update_by_name_gp(struct oplus_msg_filter *filter,
				       const char *name,
				       unsigned int grace_period_ms)
{
	struct oplus_msg_queue *queue;

	if (filter == NULL) {
		chg_err("filter is NULL\n");
		return -EINVAL;
	}
	if (filter->magic != MSG_FILTER_MAGIC) {
		chg_err("filter magic error\n");
		return -EFAULT;
	}
	if (name == NULL) {
		chg_err("name is NULL\n");
		return -EINVAL;
	}

	queue = oplus_msg_filter_find_queue(filter, name);
	if (queue == NULL) {
		chg_err("msg queue[%s] not found\n", name);
		return -EFAULT;
	}

	return oplus_msg_queue_update(queue, msecs_to_jiffies(grace_period_ms));
}

int oplus_msg_filter_update_gp(struct oplus_msg_queue *queue,
			       unsigned int grace_period_ms)
{
	if (queue == NULL) {
		chg_err("queue is NULL\n");
		return -EINVAL;
	}
	if (queue->filter->magic != MSG_FILTER_MAGIC) {
		chg_err("filter magic error\n");
		return -EFAULT;
	}

	return oplus_msg_queue_update(queue, msecs_to_jiffies(grace_period_ms));
}

int oplus_msg_filter_update_by_name(struct oplus_msg_filter *filter,
				    const char *name)
{
	struct oplus_msg_queue *queue;

	if (filter == NULL) {
		chg_err("filter is NULL\n");
		return -EINVAL;
	}
	if (filter->magic != MSG_FILTER_MAGIC) {
		chg_err("filter magic error\n");
		return -EFAULT;
	}
	if (name == NULL) {
		chg_err("name is NULL\n");
		return -EINVAL;
	}

	queue = oplus_msg_filter_find_queue(filter, name);
	if (queue == NULL) {
		chg_err("msg queue[%s] not found\n", name);
		return -EFAULT;
	}

	return oplus_msg_queue_update(queue, filter->max_delay_jiffies);
}

int oplus_msg_filter_update(struct oplus_msg_queue *queue)
{
	if (queue == NULL) {
		chg_err("queue is NULL\n");
		return -EINVAL;
	}
	if (queue->filter->magic != MSG_FILTER_MAGIC) {
		chg_err("filter magic error\n");
		return -EFAULT;
	}

	return oplus_msg_queue_update(queue, queue->filter->max_delay_jiffies);
}

// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024-2025 Oplus. All rights reserved.
 */

#define OPLUS_MSG_QUEUE_NAME_MAX 32

struct oplus_msg_queue;

struct oplus_msg_filter {
	unsigned int magic;
	spinlock_t list_lock;
	struct list_head queue_list;

	unsigned long dead_zone_jiffies;
	unsigned long max_delay_jiffies;

	void (*update_func)(void *);
};

int oplus_msg_filter_init(struct oplus_msg_filter *filter,
			  unsigned int dead_zone_ms,
			  unsigned int max_delay_ms,
			  void (*update_func)(void *));
struct oplus_msg_queue *oplus_msg_filter_find_queue(
	struct oplus_msg_filter *filter, const char *name);
int oplus_msg_filter_create_queue(
	struct oplus_msg_filter *filter,
	const char *name, void *data);
int oplus_msg_filter_release_queue(struct oplus_msg_filter *filter);
int oplus_msg_filter_update_by_name_gp(struct oplus_msg_filter *filter,
				       const char *name,
				       unsigned int grace_period_ms);
int oplus_msg_filter_update_gp(struct oplus_msg_queue *queue,
			       unsigned int grace_period_ms);
int oplus_msg_filter_update_by_name(struct oplus_msg_filter *filter,
				    const char *name);
int oplus_msg_filter_update(struct oplus_msg_queue *queue);

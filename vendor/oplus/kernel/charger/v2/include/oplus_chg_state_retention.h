
/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Copyright (C) 2023-2023 Oplus. All rights reserved.
 */

#ifndef __OPLUS_CHG_STATUS_RETENTION_H__
#define __OPLUS_CHG_STATUS_RETENTION_H__

#include <oplus_mms.h>

enum retention_topic_item {
	RETENTION_ITEM_CONNECT_STATUS,
	RETENTION_ITEM_DISCONNECT_COUNT,
	RETENTION_ITEM_TOTAL_DISCONNECT_COUNT,
	RETENTION_ITEM_STATE_READY,
};
struct oplus_chg_retention;

struct oplus_chg_retention_desc {
	const char *name;
	struct list_head list;
	int (*state_retention)(void);
	int (*disconnect_count)(void);
	int (*clear_disconnect_count)(void);
	int (*state_retention_notify)(bool);
};

struct oplus_chg_retention {
	struct list_head list;
	struct oplus_chg_retention_desc *desc;
};

int oplus_chg_retention_register(struct oplus_chg_retention_desc *desc);
#endif /*__OPLUS_CHG_STATUS_RETENTION_H__*/

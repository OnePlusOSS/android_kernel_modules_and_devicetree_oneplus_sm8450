// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024-2024 Oplus. All rights reserved.
 */

#ifndef __OPLUS_CHG_PLC_H__
#define __OPLUS_CHG_PLC_H__

#include <oplus_mms.h>
#define PLC_IBUS_MAX 1500
#define PLC_IBUS_MIN 200
#define PLC_IBUS_DEFAULT 500

enum plc_topic_item {
	PLC_ITEM_STATUS,
	PLC_ITEM_ENABLE_CNTS,
	PLC_ITEM_CHG_MODE,
};

enum oplus_plc_strategy_type {
	PLC_STRATEGY_STEP,
	PLC_STRATEGY_SIMPLE,
	PLC_STRATEGY_PID,
	PLC_STRATEGY_MAX
};

enum oplus_plc_chg_mode {
	PLC_CHG_MODE_BUCK,
	PLC_CHG_MODE_CP,
	PLC_CHG_MODE_AUTO,
};

struct oplus_plc_protocol;

struct oplus_plc_protocol_ops {
	int (*enable)(struct oplus_plc_protocol *, enum oplus_plc_chg_mode);
	int (*disable)(struct oplus_plc_protocol *);
	int (*reset_protocol)(struct oplus_plc_protocol *);
	int (*set_ibus)(struct oplus_plc_protocol *, int);
	int (*get_ibus)(struct oplus_plc_protocol *);
	int (*get_chg_mode)(struct oplus_plc_protocol *);
};

struct oplus_plc_protocol_desc {
	const char *name;
	unsigned int protocol;
	bool current_active;

	struct oplus_plc_protocol_ops ops;
};

const char *oplus_plc_chg_mode_str(enum oplus_plc_chg_mode mode);
void *oplus_plc_protocol_get_priv_data(struct oplus_plc_protocol *opp);
struct oplus_plc_protocol *oplus_plc_register_protocol(
	struct oplus_mms *topic,
	struct oplus_plc_protocol_desc *desc,
	struct device_node *node,
	void *data);
void oplus_plc_release_protocol(struct oplus_mms *topic, struct oplus_plc_protocol *opp);
int oplus_plc_protocol_set_strategy(struct oplus_plc_protocol *opp, const char *name);
int oplus_chg_plc_enable(struct oplus_mms *topic, bool enable);

#endif /* __OPLUS_CHG_PLC_H__ */

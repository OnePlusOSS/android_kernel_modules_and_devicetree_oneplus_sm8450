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
	PLC_ITEM_SUPPORT,
	PLC_ITEM_STATUS,
	PLC_ITEM_BUCK,
	PLC_ITEM_CURR,
	PLC_ITEM_DISCHG_NORMAL,
	PLC_ITEM_DISCHG_SOC,
	PLC_ITEM_DISCHG_RETRY,
	PLC_ITEM_ENABLE_CNTS,
};

#endif /* __OPLUS_CHG_PLC_H__ */

// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#ifndef _OPLUS_BATT_BAL_H_
#define _OPLUS_BATT_BAL_H_

#include <linux/i2c.h>
#include <linux/power_supply.h>
#include <oplus_mms.h>

enum batt_bal_topic_item {
	BATT_BAL_ITEM_ENABLE,
	BATT_BAL_ITEM_CURR_LIMIT,
	BATT_BAL_ITEM_ABNORMAL_STATE,
	BATT_BAL_ITEM_STATUS,
};

enum batt_bal_flow_dir {
	DEFAULT_DIR,
	B1_TO_B2,
	B2_TO_B1,
};

enum batt_bal_abnormal_state {
	BATT_BAL_NO_ABNORMAL,
	BATT_BAL_I2C_ABNORMAL,
	BATT_BAL_VOL_ABNORMAL,
	BATT_BAL_TEMP_ABNORMAL,
	BATT_BAL_IC_ABNORMAL,
};

int oplus_batt_bal_pmos_disable(struct oplus_mms *topic);

#endif /* _OPLUS_BATT_BAL_H */

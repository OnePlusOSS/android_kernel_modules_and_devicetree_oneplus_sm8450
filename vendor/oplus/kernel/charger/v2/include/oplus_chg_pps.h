// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023-2023 Oplus. All rights reserved.
 */

#ifndef __OPLUS_CHG_PPS_H__
#define __OPLUS_CHG_PPS_H__

#include <oplus_mms.h>

#define PPS_PDO_MAX			7

#define PPS_PDO_VOL_MAX(pdo)		(pdo * 100)
#define PPS_PDO_VOL_MIN(pdo)		(pdo * 100)
#define PPS_PDO_CURR_MAX(pdo)		(pdo * 50)
#define PPS_STATUS_VOLT(pps_status) (((pps_status) >> 0) & 0xFFFF)
#define PPS_STATUS_CUR(pps_status) (((pps_status) >> 16) & 0xFF)

#define PD_PDO_VOL(pdo)			((pdo) * 50)
#define PD_PDO_CURR_MAX(pdo)		((pdo) * 10)

enum pps_topic_item {
	PPS_ITEM_ONLINE,
	PPS_ITEM_CHARGING,
	PPS_ITEM_OPLUS_ADAPTER,
	PPS_ITEM_ONLINE_KEEP,
};

typedef enum
{
	USBPD_PDMSG_PDOTYPE_FIXED_SUPPLY,
	USBPD_PDMSG_PDOTYPE_BATTERY,
	USBPD_PDMSG_PDOTYPE_VARIABLE_SUPPLY,
	USBPD_PDMSG_PDOTYPE_AUGMENTED
}USBPD_PDMSG_PDOTYPE_TYPE;

typedef union {
	u32 pdo_data;
	struct {
	        u32 max_current_10ma			: 10;    /*bit [ 9: 0]*/
	        u32 voltage_50mv			: 10;    /*bit [19:10]*/
	        u32 peak_current			: 2;    /*bit [21:20]*/
	        u32 					: 1;    /*bit [22:22]*/
	        u32 epr_mode_capable			: 1;    /*bit [23:23]*/
	        u32 unchunked_ext_msg_supported		: 1;    /*bit [24:24]*/
	        u32 dual_role_data			: 1;    /*bit [25:25]*/
	        u32 usb_comm_capable			: 1;    /*bit [26:26]*/
	        u32 unconstrained_pwer			: 1;    /*bit [27:27]*/
	        u32 usb_suspend_supported		: 1;    /*bit [28:28]*/
	        u32 dual_role_power			: 1;    /*bit [29:29]*/
	        u32 pdo_type				: 2;    /*bit [31:30]*/
	};
} pd_msg_data;

typedef union
{
	u32 pdo_data;
	struct {
		u32 max_current50ma           : 8;    /*bit [ 6: 0]*/
		u32 min_voltage100mv          : 8;    /*bit [15: 8]*/
		u32                           : 1;    /*bit [16:16]*/
		u32 max_voltage100mv          : 8;    /*bit [24:17]*/
		u32                           : 2;    /*bit [26:25]*/
		u32 pps_power_limited         : 1;    /*bit [27:27]*/
		u32 pps                       : 2;    /*bit [29:28]*/
		u32 pdo_type                  : 2;    /*bit [31:30]*/
	};
} pps_msg_data;

struct pps_dev_ops {
	int (*pps_pdo_set)(int vol_mv, int curr_ma);
	int (*verify_adapter)(void);
	int (*get_pdo_info)(u32 *pdo, int num);
	u32 (*get_pps_status)(void);
};

struct oplus_pps_phy_ic {
	struct pps_dev_ops *ops;
	int phy_ic_exist;
};

enum pps_fastchg_type {
	PPS_FASTCHG_TYPE_UNKOWN,
	PPS_FASTCHG_TYPE_THIRD,
	PPS_FASTCHG_TYPE_V1,
	PPS_FASTCHG_TYPE_V2,
	PPS_FASTCHG_TYPE_V3,
	PPS_FASTCHG_TYPE_OTHER,
};

enum pps_power_type {
	PPS_POWER_TYPE_UNKOWN = 0,
	PPS_POWER_TYPE_THIRD = 33,
	PPS_POWER_TYPE_V1 = 125,
	PPS_POWER_TYPE_V2 = 150,
	PPS_POWER_TYPE_V3 = 240,
	OPLUS_PPS_POWER_MAX = 0xFFFF,
};

int oplus_pps_current_to_level(struct oplus_mms *mms, int ibus_curr);
int oplus_pps_get_charging_power_watt(struct oplus_mms *mms);
int oplus_pps_get_adapter_power_mw(struct oplus_mms *mms);
int oplus_pps_get_curve_ibus(struct oplus_mms *mms);
int oplus_pps_level_to_current(struct oplus_mms *mms, int level);
#endif /* __OPLUS_CHG_PPS_H__ */

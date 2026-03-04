// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-2022 Oplus. All rights reserved.
 */

#include <linux/configfs.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/nls.h>
#include <linux/fs.h>
#include <linux/kdev_t.h>
#include <linux/reboot.h>
#include "oplus_chg_core.h"
#include "oplus_charger.h"
#include "oplus_gauge.h"
#include "oplus_vooc.h"
#include "oplus_pps.h"
#include "oplus_short.h"
#include "oplus_adapter.h"
#include "oplus_wireless.h"
#include "charger_ic/oplus_short_ic.h"
#include "charger_ic/oplus_switching.h"
#include "oplus_debug_info.h"
#include "oplus_chg_track.h"
#include "op_wlchg_v2/oplus_chg_wls.h"
#include "wireless_ic/oplus_nu1619.h"
#include "voocphy/oplus_voocphy.h"
#include "oplus_ufcs.h"
#include "oplus_quirks.h"
#include "oplus_battery_log.h"
#include "bob_ic/oplus_tps6128xd.h"
#include "oplus_chg_exception.h"
#include "oplus_region_check.h"
#ifndef CONFIG_DISABLE_OPLUS_FUNCTION
#include <soc/oplus/system/oplus_project.h>
#endif

#define OPLUS_SVOOC_ID_MIN    10

static struct class *oplus_chg_class;
static struct device *oplus_ac_dir;
static struct device *oplus_usb_dir;
static struct device *oplus_battery_dir;
static struct device *oplus_wireless_dir;
static struct device *oplus_common_dir;

__maybe_unused static bool is_wls_ocm_available(struct oplus_chg_chip *chip)
{
	if (!chip->wls_ocm)
		chip->wls_ocm = oplus_chg_mod_get_by_name("wireless");
	return !!chip->wls_ocm;
}

__maybe_unused static bool is_comm_ocm_available(struct oplus_chg_chip *chip)
{
	if (!chip->comm_ocm)
		chip->comm_ocm = oplus_chg_mod_get_by_name("common");
	return !!chip->comm_ocm;
}

/**********************************************************************
* ac device nodes
**********************************************************************/
static ssize_t ac_online_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_ac_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (chip->charger_exist) {
		if ((chip->charger_type == POWER_SUPPLY_TYPE_USB_DCP) || (oplus_vooc_get_fastchg_started() == true) ||
		    (oplus_vooc_get_fastchg_to_normal() == true) || (oplus_vooc_get_fastchg_to_warm() == true) ||
		    (oplus_vooc_get_fastchg_dummy_started() == true) ||
		    (oplus_vooc_get_adapter_update_status() == ADAPTER_FW_NEED_UPDATE) ||
		    (oplus_vooc_get_btb_temp_over() == true)) {
			chip->ac_online = true;
		} else {
			chip->ac_online = false;
		}
	} else {
		if ((oplus_vooc_get_fastchg_started() == true) || (oplus_vooc_get_fastchg_to_normal() == true) ||
		    (oplus_vooc_get_fastchg_to_warm() == true) || (oplus_vooc_get_fastchg_dummy_started() == true) ||
		    (oplus_vooc_get_adapter_update_status() == ADAPTER_FW_NEED_UPDATE) ||
		    (oplus_vooc_get_btb_temp_over() == true) || chip->mmi_fastchg == 0) {
			chip->ac_online = true;
		} else {
			chip->ac_online = false;
		}
	}

	if (chip->ac_online == false && oplus_quirks_keep_connect_status() == 1)
		chip->ac_online = true;

	if (chip->ac_online) {
		chg_err("chg_exist:%d, ac_online:%d\n", chip->charger_exist, chip->ac_online);
	}

	return sprintf(buf, "%d\n", chip->ac_online);
}
static DEVICE_ATTR(online, S_IRUGO, ac_online_show, NULL);

static ssize_t ac_type_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%s\n", "Mains");
}
static DEVICE_ATTR(type, S_IRUGO, ac_type_show, NULL);

static struct device_attribute *oplus_ac_attributes[] = {
	&dev_attr_online,
	&dev_attr_type,
	NULL
};

/**********************************************************************
* usb device nodes
**********************************************************************/
int __attribute__((weak)) oplus_get_fast_chg_type(void)
{
	return 0;
}

int __attribute__((weak)) oplus_get_otg_online_status(void)
{
	return 0;
}

static ssize_t otg_online_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;
	int otg_online = 0;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_usb_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	otg_online = oplus_get_otg_online_status();

	return sprintf(buf, "%d\n", otg_online);
}
static DEVICE_ATTR_RO(otg_online);

int __attribute__((weak)) oplus_get_otg_switch_status(void)
{
	return 0;
}

static ssize_t otg_switch_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_usb_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	oplus_get_otg_switch_status();
	return sprintf(buf, "%d\n", chip->otg_switch);
}

void __attribute__((weak)) oplus_set_otg_switch_status(bool value)
{
	return;
}

static ssize_t otg_switch_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int val = 0;
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_usb_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (kstrtos32(buf, 0, &val)) {
		chg_err("buf error\n");
		return -EINVAL;
	}

	if (val == 1) {
		chip->otg_switch = true;
		oplus_set_otg_switch_status(true);
	} else {
		chip->otg_switch = false;
		chip->otg_online = false;
		oplus_set_otg_switch_status(false);
	}

	return count;
}
static DEVICE_ATTR_RW(otg_switch);

int __attribute__((weak)) oplus_get_usb_status(void)
{
	return 0;
}

static ssize_t usb_status_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int status = 0;

	status = oplus_get_usb_status();
	return sprintf(buf, "%d\n", status);
}
static DEVICE_ATTR_RO(usb_status);

int __attribute__((weak)) oplus_get_usbtemp_volt_l(void)
{
	return 0;
}

static ssize_t usbtemp_volt_l_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int volt = 0;

	volt = oplus_get_usbtemp_volt_l();
	return sprintf(buf, "%d\n", volt);
}
static DEVICE_ATTR_RO(usbtemp_volt_l);

int __attribute__((weak)) oplus_get_usbtemp_volt_r(void)
{
	return 0;
}

static ssize_t usbtemp_volt_r_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int volt = 0;

	volt = oplus_get_usbtemp_volt_r();
	return sprintf(buf, "%d\n", volt);
}
static DEVICE_ATTR_RO(usbtemp_volt_r);

static int fast_chg_type_by_user = -1;
static ssize_t fast_chg_type_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;
	int type = oplus_chg_get_fast_chg_type();

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_usb_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (chip->charger_type == POWER_SUPPLY_TYPE_USB_PD_SDP ||
	    (CHARGER_SUBTYPE_PD == type && (chip->pd_svooc || chip->charger_type == POWER_SUPPLY_TYPE_USB ||
						chip->charger_type == POWER_SUPPLY_TYPE_USB_CDP ||
						chip->charger_type == POWER_SUPPLY_TYPE_UNKNOWN))) {
		type = CHARGER_SUBTYPE_DEFAULT;
	}

	if (fast_chg_type_by_user > 0)
		type = fast_chg_type_by_user;
	return sprintf(buf, "%d\n", type);
}
static ssize_t fast_chg_type_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int val = 0;
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (kstrtos32(buf, 0, &val)) {
		chg_err("buf error\n");
		return -EINVAL;
	}
	/*
	if (get_eng_version() == RELEASE)
		return count;
	*/

	fast_chg_type_by_user = val;
	chg_err("costumer set val [%d], fast_chg_type_by_user [%d]\n", val, fast_chg_type_by_user);

	return count;
}
static DEVICE_ATTR_RW(fast_chg_type);

int __attribute__((weak)) oplus_get_typec_cc_orientation(void)
{
	return 0;
}

static ssize_t typec_cc_orientation_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int cc_orientation = 0;

	cc_orientation = oplus_get_typec_cc_orientation();

	return sprintf(buf, "%d\n", cc_orientation);
}
static DEVICE_ATTR_RO(typec_cc_orientation);

int __attribute__((weak)) oplus_get_water_detect(void)
{
	return 0;
}

void __attribute__((weak)) oplus_set_water_detect(bool enable)
{
	return;
}

static ssize_t water_detect_feature_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_usb_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d\n", oplus_get_water_detect());
}

static ssize_t water_detect_feature_store(struct device *dev, struct device_attribute *attr, const char *buf,
					  size_t count)
{
	int val = 0;
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_usb_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (kstrtos32(buf, 0, &val)) {
		chg_err("buf error\n");
		return -EINVAL;
	}

	chg_err("set water_detect_feature = [%d].\n", val);

	if (val == 0) {
		oplus_set_water_detect(false);
	} else {
		oplus_set_water_detect(true);
	}

	return count;
}
static DEVICE_ATTR_RW(water_detect_feature);

static struct device_attribute *oplus_usb_attributes[] = {
	&dev_attr_otg_online,
	&dev_attr_otg_switch,
	&dev_attr_usb_status,
	&dev_attr_typec_cc_orientation,
	&dev_attr_fast_chg_type,
	&dev_attr_usbtemp_volt_l,
	&dev_attr_usbtemp_volt_r,
	&dev_attr_water_detect_feature,
	NULL
};

/**********************************************************************
* battery device nodes
**********************************************************************/
static ssize_t authenticate_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d\n", chip->authenticate);
}
static DEVICE_ATTR_RO(authenticate);

static ssize_t battery_cc_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (!chip->batt_debug_cycle_count)
		return sprintf(buf, "%d\n", chip->batt_cc);
	else
		return sprintf(buf, "%d\n", chip->batt_debug_cycle_count);
}
static DEVICE_ATTR_RO(battery_cc);

static ssize_t battery_fcc_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d\n", chip->batt_fcc);
}
static DEVICE_ATTR_RO(battery_fcc);

#define BATT_RM_LEN 10
static ssize_t battery_rm_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;
	int batt_rm;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	batt_rm = chip->batt_rm < 0 ? 0 : chip->batt_rm;
	return scnprintf(buf, BATT_RM_LEN, "%d\n", batt_rm);
}
static DEVICE_ATTR_RO(battery_rm);

static ssize_t design_capacity_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d\n", chip->batt_capacity_mah);
}
static DEVICE_ATTR_RO(design_capacity);

static ssize_t smartchg_soh_support_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d\n", chip->smart_chg_soh_support);
}
static DEVICE_ATTR_RO(smartchg_soh_support);

static ssize_t battery_soh_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d\n", chip->batt_soh);
}
static DEVICE_ATTR_RO(battery_soh);

static ssize_t soh_report_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", oplus_chg_get_soh_report());
}
static DEVICE_ATTR_RO(soh_report);

static ssize_t cc_report_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", oplus_chg_get_cc_report());
}
static DEVICE_ATTR_RO(cc_report);

#define ECO_DESIGN_UPDATE_TIME_DEBUG_FLAG 0xffff
static ssize_t battery_sn_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int len = 0;
	int ret = 0;
	char batt_sn[OPLUS_BATT_SERIAL_NUM_SIZE * 2] = {"\0"};
	char eco_design_update_time[] = {"debugTime"};
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	ret = oplus_gauge_get_bat_info_sn(batt_sn, sizeof(batt_sn));
	if (ret < 0)
		chg_err("get battery sn error");
	else {
		if (chip->debug_battery_sn_data == ECO_DESIGN_UPDATE_TIME_DEBUG_FLAG) {
			len = sprintf(buf, "%s\n", eco_design_update_time);
			return len;
		}
		len = sprintf(buf, "%s\n", batt_sn);
	}

	if (!chip->debug_battery_sn_data) {
		return len;
	} else {
		return sprintf(buf, "%d\n", chip->debug_battery_sn_data);
	}
}
static DEVICE_ATTR_RO(battery_sn);

static ssize_t debug_battery_sn_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (!chip->debug_battery_sn_data) {
		return -EINVAL;
	} else {
		return sprintf(buf, "%d\n", chip->debug_battery_sn_data);
	}
}

static ssize_t debug_battery_sn_store(struct device *dev, struct device_attribute *attr,
	const char *buf, size_t count)
{
	int battery_debug_sn;
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (kstrtos32(buf, 0, &battery_debug_sn)) {
		chg_err("buf error\n");
		return -EINVAL;
	}

	chip->debug_battery_sn_data = battery_debug_sn;

	return count;
}
static DEVICE_ATTR_RW(debug_battery_sn);

static ssize_t battery_manu_date_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int len = 0;
	int ret = 0;
	char batt_date[OPLUS_BATTINFO_DATE_SIZE] = {"\0"};

	ret = oplus_gauge_get_bat_info_manu_date(batt_date, sizeof(batt_date));
	if (ret < 0)
		chg_err("get battery manu date error");
	else
		len = sprintf(buf, "%s\n", batt_date);

	return len;
}
static DEVICE_ATTR_RO(battery_manu_date);

static ssize_t battery_seal_flag_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int seal_flag = 0;
	int len = 0;

	seal_flag = oplus_pack_gauge_get_seal_flag();
	if (seal_flag < 0)
		chg_err("get battery ui cycle count error");
	else
		len = sprintf(buf, "%d\n", seal_flag);

	return len;
}

static ssize_t  battery_seal_flag_store(struct device *dev, struct device_attribute *attr,
	const char *buf, size_t count)
{
	int ret = 0;
	int seal_flag;

	if (kstrtos32(buf, 0, &seal_flag)) {
		chg_err("buf error\n");
		return -EINVAL;
	}

	ret = oplus_pack_gauge_set_seal_flag(seal_flag);
	if (ret < 0)
		chg_err("set battery ui cycle count error");

	return count;
}
static DEVICE_ATTR_RW(battery_seal_flag);

static ssize_t battery_first_usage_date_show(struct device *dev, struct device_attribute *attr,
	char *buf)
{
	int len = 0;
	int ret = 0;
	char batt_date[OPLUS_BATTINFO_DATE_SIZE] = {"\0"};

	ret = oplus_gauge_get_bat_info_first_usage_date(batt_date, sizeof(batt_date));
	if (ret < 0)
		chg_err("get battery first usage date error");
	else
		len = sprintf(buf, "%s\n", batt_date);

	return len;
}

static ssize_t battery_first_usage_date_store(struct device *dev, struct device_attribute *attr,
	const char *buf, size_t count)
{
	int ret = 0;

	ret = oplus_gauge_set_bat_info_first_usage_date(buf);
	if (ret < 0)
		chg_err("set battery first usage date error");

	return count;
}
static DEVICE_ATTR_RW(battery_first_usage_date);

static ssize_t battery_ui_cc_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int ui_cycle_count = 0;
	int len = 0;

	ui_cycle_count = oplus_gauge_get_battinfo_ui_cc();
	if (ui_cycle_count < 0)
		chg_err("get battery ui cycle count error");
	else
		len = sprintf(buf, "%d\n", ui_cycle_count);

	return len;
}

static ssize_t  battery_ui_cc_store(struct device *dev, struct device_attribute *attr,
	const char *buf, size_t count)
{
	int ret = 0;
	int ui_cycle_count;

	if (kstrtos32(buf, 0, &ui_cycle_count)) {
		chg_err("buf error\n");
		return -EINVAL;
	}

	ret = oplus_gauge_set_battinfo_ui_cc(ui_cycle_count);
	if (ret < 0)
		chg_err("set battery ui cycle count error");

	return count;
}
static DEVICE_ATTR_RW(battery_ui_cc);

static ssize_t  battery_debug_cc_store(struct device *dev, struct device_attribute *attr,
	const char *buf,  size_t count)
{
	int ui_cycle_count;
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (kstrtos32(buf, 0, &ui_cycle_count)) {
		chg_err("buf error\n");
		return -EINVAL;
	}

	chip->batt_debug_cycle_count = ui_cycle_count;

	return count;
}
static DEVICE_ATTR_WO(battery_debug_cc);

static ssize_t battery_ui_soh_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int ui_soh = 0;
	int len = 0;

	ui_soh = oplus_gauge_get_battinfo_ui_soh();
	if (ui_soh < 0)
		chg_err("get battery ui soh error");
	else
		len = sprintf(buf, "%d\n", ui_soh);

	return len;
}

static ssize_t  battery_ui_soh_store(struct device *dev, struct device_attribute *attr,
	const char *buf, size_t count)
{
	int ret = 0;
	int ui_soh;

	if (kstrtos32(buf, 0, &ui_soh)) {
		chg_err("buf error\n");
		return -EINVAL;
	}

	ret = oplus_gauge_set_battinfo_ui_soh(ui_soh);
	if (ret < 0)
		chg_err("set battery ui soh error");

	return count;
}
static DEVICE_ATTR_RW(battery_ui_soh);

static ssize_t battery_used_flag_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int used_flag = 0;
	int len = 0;

	used_flag = oplus_gauge_get_battinfo_used_flag();
	if (used_flag < 0)
		chg_err("get battery used flag error");
	else
		len = sprintf(buf, "%d\n", used_flag);

	return len;
}

static ssize_t  battery_used_flag_store(struct device *dev, struct device_attribute *attr,
	const char *buf, size_t count)
{
	int ret = 0;
	int used_flag;

	if (kstrtos32(buf, 0, &used_flag)) {
		chg_err("buf error\n");
		return -EINVAL;
	}

	ret = oplus_gauge_set_battinfo_used_flag(used_flag);
	if (ret < 0)
		chg_err("set battery used flag error");

	return count;
}
static DEVICE_ATTR_RW(battery_used_flag);

#ifdef CONFIG_OPLUS_CALL_MODE_SUPPORT
static ssize_t call_mode_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d\n", chip->calling_on);
}

static ssize_t call_mode_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int val = 0;
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (kstrtos32(buf, 0, &val)) {
		chg_err("buf error\n");
		return -EINVAL;
	}
	chip->calling_on = val;
	if (is_wls_ocm_available(chip))
		oplus_chg_anon_mod_event(chip->wls_ocm, val ? OPLUS_CHG_EVENT_CALL_ON : OPLUS_CHG_EVENT_CALL_OFF);

	return count;
}
static DEVICE_ATTR_RW(call_mode);
#endif /*CONFIG_OPLUS_CALL_MODE_SUPPORT*/

static ssize_t gsm_call_ongoing_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d\n", chip->gsm_call_ongoing);
}

static ssize_t gsm_call_ongoing_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int val = 0;
	struct oplus_chg_chip *chip = NULL;
	struct oplus_voocphy_manager *voocphy_chip = NULL;
	if (oplus_chg_get_gsm_call_on() == true) {
		chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
		if (!chip) {
			chg_err("chip is NULL\n");
			return -EINVAL;
		}

		oplus_voocphy_get_chip(&voocphy_chip);
		if (!voocphy_chip)
			return -EINVAL;

		if (kstrtos32(buf, 0, &val)) {
			chg_err("buf error\n");
			return -EINVAL;
		}
		chip->gsm_call_ongoing = val;
		chg_err("set val [%d]\n", chip->gsm_call_ongoing);

		if (voocphy_chip->ops && voocphy_chip->ops->set_fix_mode)
			voocphy_chip->ops->set_fix_mode(chip->gsm_call_ongoing);
	}
	return count;
}
static DEVICE_ATTR_RW(gsm_call_ongoing);

static ssize_t charge_technology_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (oplus_limit_svooc_current())
		chip->vooc_project = chip->limit_current_area_vooc_project;

	return sprintf(buf, "%d\n", chip->vooc_project);
}

static ssize_t charge_technology_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int val = 0;
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (kstrtos32(buf, 0, &val)) {
		chg_err("buf error\n");
		return -EINVAL;
	}
	/*
	if (get_eng_version() == RELEASE)
		return count;
	*/

	if (val > NO_VOOC && val < INVALID_VOOC_PROJECT)
		chip->vooc_project = val;
	chg_err("costumer set val [%d], new_vooc-project [%d]\n", val, chip->vooc_project);

	return count;
}
static DEVICE_ATTR_RW(charge_technology);

#ifdef CONFIG_OPLUS_CHIP_SOC_NODE
static ssize_t chip_soc_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d\n", chip->soc);
}
static DEVICE_ATTR_RO(chip_soc);
#endif /*CONFIG_OPLUS_CHIP_SOC_NODE*/

#ifdef CONFIG_OPLUS_SMART_CHARGER_SUPPORT
static ssize_t cool_down_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d\n", chip->cool_down);
}

static ssize_t cool_down_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int val = 0;
	struct oplus_chg_chip *chip = NULL;
	union oplus_chg_mod_propval pval;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (kstrtos32(buf, 0, &val)) {
		chg_err("buf error\n");
		return -EINVAL;
	}
	oplus_smart_charge_by_cool_down(chip, val);
	if (is_wls_ocm_available(chip)) {
		pval.intval = val;
		oplus_chg_mod_set_property(chip->wls_ocm, OPLUS_CHG_PROP_COOL_DOWN, &pval);
	}

	return count;
}
static DEVICE_ATTR_RW(cool_down);
#endif /*CONFIG_OPLUS_SMART_CHARGER_SUPPORT*/

static ssize_t em_mode_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d\n", chip->em_mode);
}

static ssize_t em_mode_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int val = 0;
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (kstrtos32(buf, 0, &val)) {
		chg_err("buf error\n");
		return -EINVAL;
	}

	if (val == 0) {
		chip->em_mode = false;
	} else {
		chip->em_mode = true;
#ifndef CONFIG_OPLUS_CHARGER_MTK
		if (chip->chg_ops && chip->chg_ops->subcharger_force_enable)
			chip->chg_ops->subcharger_force_enable();
#endif
	}

	return count;
}
static DEVICE_ATTR_RW(em_mode);

static ssize_t normal_current_now_store(struct device *dev, struct device_attribute *attr, const char *buf,
					size_t count)
{
	int val = 0;
	struct oplus_chg_chip *chip = NULL;
	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);

	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (kstrtos32(buf, 0, &val)) {
		chg_err("buf error\n");
		return -EINVAL;
	}

	val = val & 0XFFFF;
	chg_err("val:%d\n", val);
	if (!chip->led_on) {
		if (chip->smart_normal_cool_down == 0) {
			if (oplus_pps_get_support_type() != PPS_SUPPORT_NOT) {
				chip->normal_cool_down = oplus_convert_pps_current_to_level(chip, val);
			} else if (oplus_ufcs_get_chg_status() == UFCS_CHARGERING) {
				chip->normal_cool_down = oplus_convert_pps_current_to_level(chip, val);
			} else {
				chip->normal_cool_down = oplus_convert_current_to_level(chip, val);
			}
			chg_err("set normal_cool_down:%d\n", val);
		}
	}

	return count;
}
DEVICE_ATTR_WO(normal_current_now);

static ssize_t normal_cool_down_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int val = 0;
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (kstrtos32(buf, 0, &val)) {
		chg_err("buf error\n");
		return -EINVAL;
	}
	chg_err("val:%d\n", val);
	if (chip->smart_charge_version == 1) {
		chip->smart_normal_cool_down = val;
		chip->normal_cool_down = val;
		chg_err("set normal_cool_down:%d\n", val);
	}
	return count;
}
DEVICE_ATTR_WO(normal_cool_down);

static ssize_t get_quick_mode_time_gain_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int total_time = 0, gain_time = 0;
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	total_time = chip->quick_mode_time.tv_sec - chip->start_time;
	gain_time = chip->quick_mode_gain_time_ms / 1000;
	if (gain_time < 0)
		gain_time = 0;
	chg_err("total_time:%d, gain_time:%d, quick_mode_gain_time_ms:%d\n", total_time, gain_time,
		chip->quick_mode_gain_time_ms);
	return sprintf(buf, "%d\n", gain_time);
}
static DEVICE_ATTR_RO(get_quick_mode_time_gain);

static ssize_t get_quick_mode_percent_gain_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int percent = 0, total_time = 0, gain_time = 0;
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}
	total_time = chip->quick_mode_time.tv_sec - chip->start_time;
	gain_time = chip->quick_mode_gain_time_ms / 1000;
	percent = (gain_time * 100) / (total_time + gain_time);
	chg_err("total_time:%d, gain_time:%d, percent:%d\n", total_time, gain_time, percent);
	return sprintf(buf, "%d\n", percent);
}
static DEVICE_ATTR_RO(get_quick_mode_percent_gain);

static ssize_t fast_charge_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int val = 0;
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}
	val = oplus_chg_show_vooc_logo_ornot();

	return sprintf(buf, "%d\n", val);
}
static DEVICE_ATTR_RO(fast_charge);

static ssize_t mmi_charging_enable_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d\n", chip->mmi_chg);
}

static ssize_t mmi_charging_enable_store(struct device *dev, struct device_attribute *attr, const char *buf,
					 size_t count)
{
	int val = 0;
	int ret = 0;
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (kstrtos32(buf, 0, &val)) {
		chg_err("buf error\n");
		return -EINVAL;
	}

	chg_err("set mmi_chg = [%d].\n", val);
	chip->total_time = 0;
	if (val == 0) {
		if (chip->unwakelock_chg == 1) {
			ret = -EINVAL;
			chg_err("unwakelock testing , this test not allowed.\n");
		} else {
			oplus_chg_track_upload_mmi_chg_info(chip, 0);
			chip->mmi_chg = 0;
			if ((chip->vooc_project) && (oplus_vooc_get_fastchg_started() == true)) {
				chip->mmi_fastchg = 0;
			}
			if (oplus_chg_get_voocphy_support() == ADSP_VOOCPHY) {
				oplus_adsp_voocphy_turn_off();
			} else {
				if ((oplus_pps_get_support_type() != PPS_SUPPORT_NOT) &&
					oplus_pps_get_pps_fastchg_started()) {
					oplus_pps_stop_mmi();
				} else if (!(((chip->pd_svooc == false &&
					chip->chg_ops->get_charger_subtype() == CHARGER_SUBTYPE_PD) ||
					chip->chg_ops->get_charger_subtype() == CHARGER_SUBTYPE_QC) &&
					!oplus_vooc_get_fastchg_started()))
					oplus_vooc_turn_off_fastchg();
			}
			/* avoid asic vooc bulk still enable after mmi_chg set 0 */
			oplus_chg_turn_off_charging(chip);
			if (oplus_voocphy_get_bidirect_cp_support() && chip->chg_ops->check_chrdet_status()) {
				oplus_voocphy_set_chg_auto_mode(true);
			}
		}
	} else {
		if (chip->unwakelock_chg == 1) {
			ret = -EINVAL;
			chg_err("unwakelock testing , this test not allowed.\n");
		} else {
			oplus_chg_track_upload_mmi_chg_info(chip, 1);
			chip->mmi_chg = 1;
			if (chip->mmi_fastchg == 0) {
				oplus_chg_clear_chargerid_info();
			}
			chip->mmi_fastchg = 1;
			if (oplus_voocphy_get_bidirect_cp_support()) {
				oplus_voocphy_set_chg_auto_mode(false);
			}
			if (!chip->otg_online && !oplus_vooc_get_fastchg_started())
				oplus_chg_turn_on_charging_in_work();
			if (oplus_chg_get_voocphy_support() == ADSP_VOOCPHY) {
				oplus_adsp_voocphy_turn_on();
			}
		}
	}

	return ret < 0 ? ret : count;
}
static DEVICE_ATTR_RW(mmi_charging_enable);

#ifdef CONFIG_OPLUS_CHARGER_MTK
static ssize_t stop_charging_enable_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d\n", chip->stop_chg);
}

static ssize_t stop_charging_enable_store(struct device *dev, struct device_attribute *attr, const char *buf,
					  size_t count)
{
	int val = 0;
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (kstrtos32(buf, 0, &val)) {
		chg_err("buf error\n");
		return -EINVAL;
	}

	chg_err("set stop_chg = [%d].\n", val);

	if (val == 0) {
		chip->stop_chg = false;
	} else {
		chip->stop_chg = true;
	}

	return count;
}
static DEVICE_ATTR_RW(stop_charging_enable);
#endif

static ssize_t battery_notify_code_store(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t count)
{
	struct oplus_chg_chip *chip = NULL;
	int val = 0;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (kstrtos32(buf, 0, &val)) {
		chg_err("buf error\n");
		return -EINVAL;
	}

	chg_info("notify_code %d, val %d\n", chip->notify_code, val);

	oplus_comm_set_anti_expansion_status(chip, val);

	return count;
}

static ssize_t battery_notify_code_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d\n", chip->notify_code);
}
static DEVICE_ATTR_RW(battery_notify_code);

int __attribute__((weak)) oplus_chg_get_subcurrent(void)
{
	return 0;
}

static ssize_t sub_current_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;
	int sub_current = 0;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (chip->dual_charger_support)
		sub_current = oplus_chg_get_subcurrent();

	return sprintf(buf, "%d\n", sub_current);
}
static DEVICE_ATTR_RO(sub_current);

static ssize_t charge_timeout_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d\n", chip->chging_over_time);
}
static DEVICE_ATTR_RO(charge_timeout);

static ssize_t adapter_fw_update_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d\n", oplus_vooc_get_adapter_update_status());
}
static DEVICE_ATTR_RO(adapter_fw_update);

static ssize_t batt_cb_status_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d\n", oplus_gauge_get_battery_cb_status());
}
static DEVICE_ATTR_RO(batt_cb_status);

int __attribute__((weak)) oplus_get_chg_i2c_err(void)
{
	return 0;
}

void __attribute__((weak)) oplus_clear_chg_i2c_err(void)
{
	return;
}

static ssize_t chg_i2c_err_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d\n", oplus_get_chg_i2c_err());
}

static ssize_t chg_i2c_err_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int val = 0;
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (kstrtos32(buf, 0, &val)) {
		chg_err("buf error\n");
		return -EINVAL;
	}

	oplus_clear_chg_i2c_err();

	return count;
}
static DEVICE_ATTR_RW(chg_i2c_err);

#ifdef CONFIG_OPLUS_SHIP_MODE_SUPPORT
static ssize_t ship_mode_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d\n", chip->enable_shipmode);
}

static ssize_t ship_mode_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int val = 0;
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (kstrtos32(buf, 0, &val)) {
		chg_err("buf error\n");
		return -EINVAL;
	}
	chip->enable_shipmode = val;
	oplus_gauge_update_soc_smooth_parameter();

	return count;
}
static DEVICE_ATTR_RW(ship_mode);
#endif /*CONFIG_OPLUS_SHIP_MODE_SUPPORT*/

#ifdef CONFIG_OPLUS_SHORT_C_BATT_CHECK
#ifdef CONFIG_OPLUS_SHORT_USERSPACE
static ssize_t short_c_limit_chg_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d\n", (int)chip->short_c_batt.limit_chg);
}

static ssize_t short_c_limit_chg_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int val = 0;
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (kstrtos32(buf, 0, &val)) {
		chg_err("buf error\n");
		return -EINVAL;
	}

	printk(KERN_ERR "[OPLUS_CHG] [short_c_bat] set limit chg[%d]\n", !!val);
	chip->short_c_batt.limit_chg = !!val;
	/* for userspace logic */
	if (!!val == 0) {
		chip->short_c_batt.is_switch_on = 0;
	}

	return count;
}
static DEVICE_ATTR_RW(short_c_limit_chg);

static ssize_t short_c_limit_rechg_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d\n", (int)chip->short_c_batt.limit_rechg);
}

static ssize_t short_c_limit_rechg_store(struct device *dev, struct device_attribute *attr, const char *buf,
					 size_t count)
{
	int val = 0;
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (kstrtos32(buf, 0, &val)) {
		chg_err("buf error\n");
		return -EINVAL;
	}

	printk(KERN_ERR "[OPLUS_CHG] [short_c_bat] set limit rechg[%d]\n", !!val);
	chip->short_c_batt.limit_rechg = !!val;

	return count;
}
static DEVICE_ATTR_RW(short_c_limit_rechg);

static ssize_t charge_term_current_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d\n", chip->limits.iterm_ma);
}
static DEVICE_ATTR_RO(charge_term_current);

static ssize_t input_current_settled_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int val = 0;
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	val = 2000;
	if (chip && chip->chg_ops->get_dyna_aicl_result) {
		val = chip->chg_ops->get_dyna_aicl_result();
	}

	return sprintf(buf, "%d\n", val);
}
static DEVICE_ATTR_RO(input_current_settled);
#endif /*CONFIG_OPLUS_SHORT_USERSPACE*/
#endif /*CONFIG_OPLUS_SHORT_C_BATT_CHECK*/

#ifdef CONFIG_OPLUS_SHORT_HW_CHECK
static ssize_t short_c_hw_feature_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d\n", chip->short_c_batt.is_feature_hw_on);
}

static ssize_t short_c_hw_feature_store(struct device *dev, struct device_attribute *attr, const char *buf,
					size_t count)
{
	int val = 0;
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (kstrtos32(buf, 0, &val)) {
		chg_err("buf error\n");
		return -EINVAL;
	}

	printk(KERN_ERR "[OPLUS_CHG] [short_c_hw_check]: set is_feature_hw_on [%d]\n", val);
	chip->short_c_batt.is_feature_hw_on = val;

	return count;
}
static DEVICE_ATTR_RW(short_c_hw_feature);

static ssize_t short_c_hw_status_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d\n", chip->short_c_batt.shortc_gpio_status);
}
static DEVICE_ATTR_RO(short_c_hw_status);
#endif /*CONFIG_OPLUS_SHORT_HW_CHECK*/

#ifdef CONFIG_OPLUS_SHORT_IC_CHECK
static ssize_t short_ic_otp_status_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d\n", chip->short_c_batt.ic_short_otp_st);
}
static DEVICE_ATTR_RO(short_ic_otp_status);

static ssize_t short_ic_volt_thresh_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d\n", chip->short_c_batt.ic_volt_threshold);
}

static ssize_t short_ic_volt_thresh_store(struct device *dev, struct device_attribute *attr, const char *buf,
					  size_t count)
{
	int val = 0;
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (kstrtos32(buf, 0, &val)) {
		chg_err("buf error\n");
		return -EINVAL;
	}

	chip->short_c_batt.ic_volt_threshold = val;
	oplus_short_ic_set_volt_threshold(chip);

	return count;
}
static DEVICE_ATTR_RW(short_ic_volt_thresh);

static ssize_t short_ic_otp_value_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d\n", oplus_short_ic_get_otp_error_value(chip));
}
static DEVICE_ATTR_RO(short_ic_otp_value);
#endif /*CONFIG_OPLUS_SHORT_IC_CHECK*/

static ssize_t voocchg_ing_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int val = 0;
	struct oplus_chg_chip *chip = NULL;
	union oplus_chg_mod_propval pval = {
		0,
	};

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}
	if (oplus_is_ufcs_charging())
		val = oplus_ufcs_get_ufcs_fastchg_started();
	else if (oplus_is_pps_charging())
		val = oplus_pps_get_pps_fastchg_started();
	else {
		val = oplus_vooc_get_fastchg_ing();
#ifndef WPC_NEW_INTERFACE
		if (!val && chip->wireless_support) {
			val = oplus_wpc_get_fast_charging();
		}
#else
		if (!val && chip->wireless_support) {
			val = oplus_wpc_get_status();
		}
#endif

		if (is_wls_ocm_available(chip) && !val) {
			oplus_chg_mod_get_property(chip->wls_ocm, OPLUS_CHG_PROP_FASTCHG_STATUS, &pval);
			val = pval.intval;
		}
	}
	return sprintf(buf, "%d\n", val);
}
static DEVICE_ATTR_RO(voocchg_ing);

static ssize_t ppschg_ing_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int val = 0;
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}
	if (oplus_is_ufcs_charging())
		val = oplus_ufcs_get_protocol_status();
	else
		val = oplus_is_pps_charging();

	if (val == 0
		&& oplus_quirks_keep_connect_status() == 1
		&& oplus_voocphy_get_fastchg_start() == 0) {
		if (oplus_pps_get_last_charging_status())
			val = oplus_pps_get_last_charging_status();
		else if (oplus_ufcs_get_last_charging_status())
			val = oplus_ufcs_get_last_protocol_status();
	}

	return sprintf(buf, "%d\n", val);
}
static DEVICE_ATTR_RO(ppschg_ing);

static ssize_t ppschg_power_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int val = 0;
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}
	if (oplus_is_ufcs_charging())
		val = oplus_ufcs_get_power();
	else
		val = oplus_pps_show_power();

	if (val == OPLUS_PPS_POWER_CLR
		&& oplus_quirks_keep_connect_status() == 1
		&& oplus_voocphy_get_fastchg_start() == 0) {
		if (oplus_ufcs_get_support_type() == UFCS_SUPPORT_NOT)
			val = oplus_pps_get_last_power();
		else
			val = oplus_ufcs_get_last_power();
	}

	return sprintf(buf, "%d\n", val);
}
static DEVICE_ATTR_RO(ppschg_power);

static ssize_t ufcs_oplus_id_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int val = 0;
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}
	if (oplus_is_ufcs_charging())
		val = oplus_ufcs_check_oplus_id();
	else
		val = 0;

	return sprintf(buf, "%d\n", val);
}

static ssize_t ufcs_oplus_id_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int val = 0;
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (kstrtos32(buf, 0, &val)) {
		chg_err("buf error\n");
		return -EINVAL;
	}
	chg_err(" val = %d\n", val);
	oplus_ufcs_set_third_id(val);

	return count;
}
static DEVICE_ATTR_RW(ufcs_oplus_id);

static ssize_t pps_third_support_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int val = 0;

	val = oplus_pps_check_3rd_support();
	val = val < 0 ? 0 : val;

	return sprintf(buf, "%d\n", val);
}
static DEVICE_ATTR_RO(pps_third_support);

static ssize_t pps_third_priority_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int val = 0;

	val = third_pps_priority_than_svooc();
	val = val < 0 ? 0 : val;

	return sprintf(buf, "%d\n", val);
}
static DEVICE_ATTR_RO(pps_third_priority);

static ssize_t limit_svooc_current_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int val = 0;

	val = oplus_limit_svooc_current();
	val = val < 0 ? 0 : val;

	return sprintf(buf, "%d\n", val);
}
static DEVICE_ATTR_RO(limit_svooc_current);

static ssize_t screen_off_by_batt_temp_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int val = 0;
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	val = chip->screen_off_control_by_batt_temp;

	return sprintf(buf, "%d\n", val);
}
static DEVICE_ATTR_RO(screen_off_by_batt_temp);

static ssize_t bcc_exception_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	chg_err("%s\n", buf);
	oplus_chg_bcc_err(buf);

	return count;
}
static DEVICE_ATTR_WO(bcc_exception);

int __attribute__((weak)) oplus_gauge_get_bcc_parameters(char *buf)
{
	return 0;
}

int __attribute__((weak)) oplus_gauge_set_bcc_parameters(const char *buf)
{
	return 0;
}

static ssize_t bcc_parms_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int val = 0;
	ssize_t len = 0;
	struct oplus_chg_chip *chip = NULL;
        int type = oplus_chg_get_fast_chg_type();

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (oplus_vooc_get_reply_bits() == 7
                && oplus_chg_get_voocphy_support() == NO_VOOCPHY
                && (type == CHARGER_SUBTYPE_FASTCHG_SVOOC || type >= OPLUS_SVOOC_ID_MIN)) {
		val = oplus_gauge_get_prev_bcc_parameters(buf);
	} else {
		val = oplus_gauge_get_bcc_parameters(buf);
	}
	len = strlen(buf);
	chg_err("len: %ld\n", len);

	return len;
}

static ssize_t bcc_parms_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int ret = 0;
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	ret = oplus_gauge_set_bcc_parameters(buf);
	if (ret < 0) {
		chg_err("error\n");
		return -EINVAL;
	}

	return count;
}
static DEVICE_ATTR_RW(bcc_parms);

static ssize_t bcc_current_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d\n", chip->bcc_current);
}

static ssize_t bcc_current_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int val = 0, ret = 0;
	struct oplus_chg_chip *chip = NULL;
	union oplus_chg_mod_propval pval;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (kstrtos32(buf, 0, &val)) {
		chg_err("buf error\n");
		return -EINVAL;
	}

	ret = oplus_smart_charge_by_bcc(chip, val);
	if (ret < 0) {
		chg_err("error\n");
		return -EINVAL;
	}
	if (is_wls_ocm_available(chip)) {
		pval.intval = val;
		oplus_chg_mod_set_property(chip->wls_ocm,
			OPLUS_CHG_PROP_BCC_CURRENT, &pval);
	}

	mutex_lock(&chip->bcc_curr_done_mutex);
	chip->bcc_curr_done = BCC_CURR_DONE_REQUEST;
	chg_err("bcc_curr_done:%d\n", chip->bcc_curr_done);
	mutex_unlock(&chip->bcc_curr_done_mutex);

	if (oplus_chg_get_voocphy_support() != NO_VOOCPHY) {
		oplus_chg_check_bcc_curr_done();
	}
	return count;
}
static DEVICE_ATTR_RW(bcc_current);

extern u8 soc_store[4];
extern u8 night_count;
static ssize_t soc_ajust_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d\n", chip->soc_ajust);
}

static ssize_t soc_ajust_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int val = 0;
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (kstrtos32(buf, 0, &val)) {
		chg_err("buf error\n");
		return -EINVAL;
	}

	if (val == 0) {
		chip->soc_ajust = 0;
		night_count = 0;
		chip->modify_soc = 0;
	} else {
		chip->soc_ajust = 1;
		soc_store[0] = chip->soc;
		chg_err("[soc_ajust_feature]:set soc_ajust_switch,soc_store0 = [%d].\n", soc_store[0]);
		set_soc_feature();
	}
	chg_err("[soc_ajust_feature]:set soc_ajust_switch = [%d] soc = [%d].\n", val, chip->soc);

	return count;
}
static DEVICE_ATTR_RW(soc_ajust);

static ssize_t parallel_chg_mos_test_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (oplus_switching_get_hw_enable() == MOS_OPEN ||
	    chip->balancing_bat_status == PARALLEL_BAT_BALANCE_ERROR_STATUS8 ||
	    chip->balancing_bat_status == PARALLEL_BAT_BALANCE_ERROR_STATUS9) {
		chg_err("mos: %d, test next time!\n", oplus_switching_get_hw_enable());
		return 0;
	}
	if (!chip->mos_test_result) {
		if (!chip->mos_test_started)
			schedule_delayed_work(&chip->parallel_chg_mos_test_work, 0);
	} else {
		chg_err("mos test success, use last result!\n");
	}

	return sprintf(buf, "%d\n", chip->mos_test_result);
}
static DEVICE_ATTR_RO(parallel_chg_mos_test);

static ssize_t parallel_chg_mos_status_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;
	int val;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	val = oplus_switching_get_hw_enable();
	return sprintf(buf, "%d\n", val);
}
static DEVICE_ATTR_RO(parallel_chg_mos_status);

static ssize_t aging_ffc_data_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;
	int ffc1_voltage_offset = 0;
	int ffc2_voltage_offset = 0;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	oplus_chg_get_aging_ffc_offset(chip, &ffc1_voltage_offset, &ffc2_voltage_offset);

	return sprintf(buf, "%d,%d,%d,%d,%d,%d,%d,%d,%d\n", chip->aging_ffc_version, chip->vbatt_num,
		       oplus_switching_support_parallel_chg(), chip->debug_batt_cc, chip->batt_cc,
		       chip->limits.default_ffc1_normal_vfloat_sw_limit + ffc1_voltage_offset,
		       chip->limits.default_ffc1_warm_vfloat_sw_limit + ffc1_voltage_offset,
		       chip->limits.default_ffc2_normal_vfloat_sw_limit + ffc2_voltage_offset,
		       chip->limits.default_ffc2_warm_vfloat_sw_limit + ffc2_voltage_offset);
}

static ssize_t aging_ffc_data_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int val = 0;
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (kstrtos32(buf, 0, &val)) {
		chg_err("buf error\n");
		return -EINVAL;
	}

	chip->debug_batt_cc = val;

	return count;
}
static DEVICE_ATTR_RW(aging_ffc_data);

static ssize_t battery_charging_state_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d\n", chip->charging_state);
}
static DEVICE_ATTR_RO(battery_charging_state);

static ssize_t bms_heat_temp_compensation_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d\n", chip->bms_heat_temp_compensation);
}

static ssize_t bms_heat_temp_compensation_store(struct device *dev, struct device_attribute *attr,
					const char *buf, size_t count)
{
	int val = 0;
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (kstrtos32(buf, 0, &val)) {
		chg_err("buf error\n");
		return -EINVAL;
	}

	chip->bms_heat_temp_compensation = val;

	return count;
}
static DEVICE_ATTR_RW(bms_heat_temp_compensation);

static ssize_t battery_log_head_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct oplus_configfs_device *chip = dev->driver_data;

	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (oplus_battery_log_support() != true) {
		return -ENODEV;
	}

	return battery_log_common_operate(BATTERY_LOG_DUMP_LOG_HEAD,
		buf, BATTERY_LOG_MAX_SIZE);
}
static DEVICE_ATTR_RO(battery_log_head);

static ssize_t battery_log_content_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct oplus_configfs_device *chip = dev->driver_data;

	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (oplus_battery_log_support() != true) {
		return -ENODEV;
	}

	return battery_log_common_operate(BATTERY_LOG_DUMP_LOG_CONTENT,
		buf, BATTERY_LOG_MAX_SIZE);
}
static DEVICE_ATTR_RO(battery_log_content);

static ssize_t pkg_name_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	oplus_chg_track_set_app_info(buf);
	return count;
}
static DEVICE_ATTR_WO(pkg_name);

static ssize_t slow_chg_en_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d,%d,%d,%d\n", chip->slow_chg_pct, chip->slow_chg_watt, chip->slow_chg_enable,
		       chip->slow_chg_batt_limit);
}

static ssize_t slow_chg_en_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct oplus_chg_chip *chip = NULL;
	int pct = 0, watt = 0, en = 0;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (!buf) {
		chg_err("buf is NULL\n");
		return -EINVAL;
	}

	if (sscanf(buf, "%d,%d,%d", &pct, &watt, &en) != 3) {
		chg_err("invalid buff %s\n", buf);
		return -EINVAL;
	}

	if (pct <= 0 || pct > 100 || watt <= 0 || watt >= 0xffff) {
		chg_err("pct %d or watt %d invalid\n", pct, watt);
		return -EINVAL;
	}
	oplus_chg_track_upload_slow_chg_info(chip, pct, watt, en);
	mutex_lock(&chip->slow_chg_mutex);
	chip->slow_chg_pct = pct;
	chip->slow_chg_watt = watt;
	chip->slow_chg_enable = (bool)!!en;
	mutex_unlock(&chip->slow_chg_mutex);
	chg_info("%d %d %d\n", chip->slow_chg_pct, chip->slow_chg_watt, chip->slow_chg_enable);
	return count;
}
static DEVICE_ATTR_RW(slow_chg_en);

static ssize_t eco_design_status_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	/* TODO: check nvid is EU or not */
	bool eco_design_support;

	eco_design_support = eco_design_supported_comm_chg_nvid();
	return sprintf(buf, "%d\n", eco_design_support);
}
static DEVICE_ATTR_RO(eco_design_status);

#define GAGUE_INFO_PAGE_SIZE 1024
static ssize_t gauge_info_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int len;
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (oplus_plat_gauge_is_support())
		return -EINVAL;

	oplus_gauge_get_device_name(buf, GAGUE_INFO_PAGE_SIZE);
	len = strlen(buf);
	snprintf(&(buf[len]), GAGUE_INFO_PAGE_SIZE - len, "$$main_gauge@@");
	len = strlen(buf);
	oplus_gauge_get_info(&(buf[len]), GAGUE_INFO_PAGE_SIZE - len);
	if (oplus_gauge_get_sub_batt_soc() > 0) {
		len = strlen(buf);
		snprintf(&(buf[len]), GAGUE_INFO_PAGE_SIZE - len, "$$sub_gauge@@");
		len = strlen(buf);
		oplus_sub_gauge_get_info(&(buf[len]), GAGUE_INFO_PAGE_SIZE - len);
	}
	pr_info("gauge_info_len:%ld, data:%s\n", strlen(buf), buf);
	return strlen(buf);
}
static DEVICE_ATTR_RO(gauge_info);

static ssize_t bqfs_status_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	bool status = false;
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	status = oplus_gauge_get_bqfs_status();

	return sprintf(buf, "%d\n", status);
}
static DEVICE_ATTR_RO(bqfs_status);

static ssize_t batt_temp_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d\n", chip->tbatt_temp);
}
static DEVICE_ATTR_RO(batt_temp);

static ssize_t rechg_soc_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;
	int rechg_soc;
	bool en;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_common_dir);

	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	oplus_comm_get_rechg_soc_limit(&rechg_soc, &en);

	return sprintf(buf, "%d,%d\n", en, rechg_soc);
}

static ssize_t rechg_soc_store(struct device *dev, struct device_attribute *attr,
	const char *buf, size_t count)
{
	struct oplus_chg_chip *chip = NULL;
	int rechg_soc = 0, en = 0;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_common_dir);

	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (!buf) {
		chg_err("buf is NULL\n");
		return -EINVAL;
	}

	if (sscanf(buf, "%d,%d", &en, &rechg_soc) != 2) {
		chg_err("invalid buff %s\n", buf);
		return -EINVAL;
	}

	if (rechg_soc < 0 || rechg_soc > 100) {
		chg_err("rechg_soc %d invalid\n", rechg_soc);
		return -EINVAL;
	} else if ((en == 1) && (rechg_soc == 100)) {
		chg_err("disallow soc_rechg at 100\n");
		return -EINVAL;
	}

	oplus_comm_set_rechg_soc_limit(rechg_soc, (bool)!!en);

	chg_info("%d,%d\n", en, rechg_soc);
	return count;
}
static DEVICE_ATTR_RW(rechg_soc);

static struct device_attribute *oplus_battery_attributes[] = {
	&dev_attr_authenticate,
	&dev_attr_battery_cc,
	&dev_attr_battery_fcc,
	&dev_attr_battery_rm,
	&dev_attr_battery_soh,
	&dev_attr_soh_report,
	&dev_attr_cc_report,
#ifdef CONFIG_OPLUS_CALL_MODE_SUPPORT
	&dev_attr_call_mode,
#endif
	&dev_attr_gsm_call_ongoing,
	&dev_attr_charge_technology,
#ifdef CONFIG_OPLUS_CHIP_SOC_NODE
	&dev_attr_chip_soc,
#endif
#ifdef CONFIG_OPLUS_SMART_CHARGER_SUPPORT
	&dev_attr_cool_down,
#endif
	&dev_attr_em_mode,
	&dev_attr_fast_charge,
	&dev_attr_mmi_charging_enable,
#ifdef CONFIG_OPLUS_CHARGER_MTK
	&dev_attr_stop_charging_enable,
#endif
	&dev_attr_battery_notify_code,
	&dev_attr_sub_current,
	&dev_attr_charge_timeout,
	&dev_attr_adapter_fw_update,
	&dev_attr_batt_cb_status,
	&dev_attr_chg_i2c_err,
#ifdef CONFIG_OPLUS_SHIP_MODE_SUPPORT
	&dev_attr_ship_mode,
#endif
#ifdef CONFIG_OPLUS_SHORT_C_BATT_CHECK
#ifdef CONFIG_OPLUS_SHORT_USERSPACE
	&dev_attr_short_c_limit_chg,
	&dev_attr_short_c_limit_rechg,
	&dev_attr_charge_term_current,
	&dev_attr_input_current_settled,
#endif
#endif
#ifdef CONFIG_OPLUS_SHORT_HW_CHECK
	&dev_attr_short_c_hw_feature,
	&dev_attr_short_c_hw_status,
#endif
#ifdef CONFIG_OPLUS_SHORT_IC_CHECK
	&dev_attr_short_ic_otp_status,
	&dev_attr_short_ic_volt_thresh,
	&dev_attr_short_ic_otp_value,
#endif
	&dev_attr_voocchg_ing,
	&dev_attr_ppschg_ing,
	&dev_attr_ppschg_power,
	&dev_attr_ufcs_oplus_id,
	&dev_attr_pps_third_support,
	&dev_attr_pps_third_priority,
	&dev_attr_limit_svooc_current,
	&dev_attr_screen_off_by_batt_temp,
	&dev_attr_bcc_parms,
	&dev_attr_bcc_current,
	&dev_attr_bcc_exception,
	&dev_attr_soc_ajust,
	&dev_attr_normal_cool_down,
	&dev_attr_normal_current_now,
	&dev_attr_get_quick_mode_time_gain,
	&dev_attr_get_quick_mode_percent_gain,
	&dev_attr_parallel_chg_mos_test,
	&dev_attr_parallel_chg_mos_status,
	&dev_attr_design_capacity,
	&dev_attr_smartchg_soh_support,
	&dev_attr_aging_ffc_data,
	&dev_attr_battery_charging_state,
	&dev_attr_bms_heat_temp_compensation,
	&dev_attr_battery_log_head,
	&dev_attr_battery_log_content,
	&dev_attr_pkg_name,
	&dev_attr_slow_chg_en,
	&dev_attr_battery_sn,
	&dev_attr_debug_battery_sn,
	&dev_attr_battery_seal_flag,
	&dev_attr_gauge_info,
	&dev_attr_bqfs_status,
	&dev_attr_batt_temp,
	&dev_attr_eco_design_status,
	&dev_attr_battery_manu_date,
	&dev_attr_battery_first_usage_date,
	&dev_attr_battery_ui_cc,
	&dev_attr_battery_debug_cc,
	&dev_attr_battery_ui_soh,
	&dev_attr_battery_used_flag,
	&dev_attr_rechg_soc,
	NULL
};

/**********************************************************************
* wireless device nodes
**********************************************************************/
static ssize_t tx_voltage_now_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_wireless_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d\n", 0);
}
static DEVICE_ATTR_RO(tx_voltage_now);

static ssize_t tx_current_now_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_wireless_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d\n", 0);
}
static DEVICE_ATTR_RO(tx_current_now);

static ssize_t cp_voltage_now_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_wireless_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d\n", 0);
}
static DEVICE_ATTR_RO(cp_voltage_now);

static ssize_t cp_current_now_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_wireless_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d\n", 0);
}
static DEVICE_ATTR_RO(cp_current_now);

static ssize_t wireless_mode_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_wireless_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d\n", 0);
}
static DEVICE_ATTR_RO(wireless_mode);

static ssize_t wireless_type_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_wireless_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d\n", 0);
}
static DEVICE_ATTR_RO(wireless_type);

static ssize_t cep_info_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_wireless_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}
	return sprintf(buf, "%d\n", 0);
}
static DEVICE_ATTR_RO(cep_info);

int __attribute__((weak)) oplus_wpc_get_real_type(void)
{
	return 0;
}
static ssize_t real_type_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int real_type = 0;
	struct oplus_chg_chip *chip = NULL;
	union oplus_chg_mod_propval pval = {
		0,
	};

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_wireless_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return 0;
	}

	real_type = oplus_wpc_get_real_type();

	if (is_wls_ocm_available(chip)) {
		oplus_chg_mod_get_property(chip->wls_ocm, OPLUS_CHG_PROP_REAL_TYPE, &pval);
		real_type = pval.intval;
	}

	return sprintf(buf, "%d\n", real_type);
}
static DEVICE_ATTR_RO(real_type);

#ifdef OPLUS_CHG_ADB_ROOT_ENABLE
ssize_t __attribute__((weak))
oplus_chg_wls_upgrade_fw_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return 0;
}
ssize_t __attribute__((weak))
oplus_chg_wls_upgrade_fw_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	return 0;
}
static ssize_t upgrade_firmware_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_wireless_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (is_wls_ocm_available(chip))
		return oplus_chg_wls_upgrade_fw_show(&chip->wls_ocm->dev, attr, buf);
	return 0;
}

static ssize_t upgrade_firmware_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_wireless_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (is_wls_ocm_available(chip))
		count = oplus_chg_wls_upgrade_fw_store(&chip->wls_ocm->dev, attr, buf, count);

	return count;
}
static DEVICE_ATTR_RW(upgrade_firmware);
#endif /*OPLUS_CHG_ADB_ROOT_ENABLE*/

static ssize_t status_keep_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_wireless_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d\n", chip->wls_status_keep);
}

static ssize_t status_keep_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int val = 0;
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_wireless_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (kstrtos32(buf, 0, &val)) {
		chg_err("buf error\n");
		return -EINVAL;
	}

	chg_err("set wls_status_keep=%d\n", val);
	if (val == WLS_SK_BY_HAL && chip->wls_status_keep == WLS_SK_NULL) {
		val = WLS_SK_NULL;
		chg_err("force to set wls_status_keep=%d\n", val);
	}
	WRITE_ONCE(chip->wls_status_keep, val);
	if (chip->wls_status_keep == 0)
		power_supply_changed(chip->batt_psy);

	return count;
}
static DEVICE_ATTR_RW(status_keep);

int __attribute__((weak)) oplus_wpc_get_max_wireless_power(void)
{
	return 0;
}

int __attribute__((weak)) oplus_chg_wls_get_max_wireless_power(struct device *dev)
{
	return 0;
}

static ssize_t max_w_power_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;
	int max_wls_power = 0;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_wireless_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	max_wls_power = oplus_wpc_get_max_wireless_power();

	if (is_wls_ocm_available(chip))
		max_wls_power = oplus_chg_wls_get_max_wireless_power(&chip->wls_ocm->dev);

	return sprintf(buf, "%d\n", max_wls_power < 0 ? 0 : max_wls_power);
}
static DEVICE_ATTR_RO(max_w_power);

static struct device_attribute *oplus_wireless_attributes[] = {
	&dev_attr_tx_voltage_now,
	&dev_attr_tx_current_now,
	&dev_attr_cp_voltage_now,
	&dev_attr_cp_current_now,
	&dev_attr_wireless_mode,
	&dev_attr_wireless_type,
	&dev_attr_cep_info,
	&dev_attr_real_type,
#ifdef OPLUS_CHG_ADB_ROOT_ENABLE
	&dev_attr_upgrade_firmware,
#endif
	&dev_attr_status_keep,
	&dev_attr_max_w_power,
	NULL
};

/**********************************************************************
* common device nodes
**********************************************************************/
#ifdef OPLUS_CHG_ADB_ROOT_ENABLE
ssize_t __attribute__((weak))
oplus_chg_comm_charge_parameter_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return 0;
}
ssize_t __attribute__((weak))
oplus_chg_comm_charge_parameter_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	return 0;
}
static ssize_t charge_parameter_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_common_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (is_comm_ocm_available(chip))
		return oplus_chg_comm_charge_parameter_show(&chip->comm_ocm->dev, attr, buf);
	return 0;
}

static ssize_t charge_parameter_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_common_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (is_comm_ocm_available(chip))
		count = oplus_chg_comm_charge_parameter_store(&chip->comm_ocm->dev, attr, buf, count);

	return count;
}
static DEVICE_ATTR_RW(charge_parameter);
#endif /*OPLUS_CHG_ADB_ROOT_ENABLE*/

ssize_t __attribute__((weak)) oplus_chg_comm_send_mutual_cmd(struct oplus_chg_mod *comm_ocm, char *buf)
{
	return -EINVAL;
}
ssize_t __attribute__((weak))
oplus_chg_comm_response_mutual_cmd(struct oplus_chg_mod *comm_ocm, const char *buf, size_t count)
{
	return -EINVAL;
}
ssize_t __attribute__((weak)) oplus_chg_send_mutual_cmd(char *buf)
{
	return -EINVAL;
}
ssize_t __attribute__((weak)) oplus_chg_response_mutual_cmd(const char *buf, size_t count)
{
	return -EINVAL;
}
static ssize_t mutual_cmd_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int ret = -EINVAL;
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_usb_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (is_comm_ocm_available(chip))
		ret = oplus_chg_comm_send_mutual_cmd(chip->comm_ocm, buf);
	else
		ret = oplus_charger_send_mutual_cmd(chip, buf);

	return ret;
}

static ssize_t mutual_cmd_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_usb_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (is_comm_ocm_available(chip))
		oplus_chg_comm_response_mutual_cmd(chip->comm_ocm, buf, count);
	else
		oplus_charger_response_mutual_cmd(chip, buf, count);

	return count;
}
static DEVICE_ATTR_RW(mutual_cmd);

int __attribute__((weak)) oplus_chg_track_set_hidl_info(const char *buf, size_t count)
{
	return 0;
}

static ssize_t track_hidl_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_common_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	oplus_chg_track_set_hidl_info(buf, count);

	return count;
}
static DEVICE_ATTR_WO(track_hidl);

static ssize_t boot_completed_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_common_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d\n", chip->boot_completed);
}
static DEVICE_ATTR_RO(boot_completed);

static ssize_t chg_olc_config_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_common_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	oplus_chg_olc_config_set(buf);
	return count;
}

static ssize_t chg_olc_config_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;
	int len = 0;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_common_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	len = oplus_chg_olc_config_get(buf);
	return len;
}
static DEVICE_ATTR_RW(chg_olc_config);

static ssize_t battlog_push_config_store(struct device *dev,
			       struct device_attribute *attr, const char *buf,
			       size_t count)
{
	char buffer[2] = { 0 };
	int val = 0;
	struct oplus_chg_chip *chip = NULL;
	int rc = 0;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_common_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (count < 0 || count > sizeof(buffer) - 1) {
		chg_err("%s: count[%zu] -EFAULT.\n", __func__, count);
		return -EFAULT;
	}

	if (copy_from_user(buffer, buf, count)) {
		chg_err("%s:  error.\n", __func__);
		return -EFAULT;
	}
	buffer[count] = '\0';

	if (kstrtos32(buffer, 0, &val)) {
		chg_err("buffer error\n");
		return -EINVAL;
	}

	if (!!val) {
		rc = oplus_chg_batterylog_exception_push();
		if (rc < 0)
			chg_err("push batterylog failed, rc=%d\n", rc);
		else
			chg_info("push batterylog successed\n");
	}

	return (rc < 0) ? rc : count;
}
static DEVICE_ATTR_WO(battlog_push_config);

static int adapter_power_by_user = -1;
static ssize_t adapter_power_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;
	int power = 0;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_common_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	power = oplus_get_adapter_power();

	if (adapter_power_by_user > 0)
		power = adapter_power_by_user;

	return sprintf(buf, "%d\n", power);
}

static ssize_t adapter_power_store(struct device *dev, struct device_attribute *attr,
							const char *buf, size_t count)
{
	int val = 0;

	if (kstrtos32(buf, 0, &val)) {
		chg_err("buf error\n");
		return -EINVAL;
	}

	adapter_power_by_user = val;

	return count;
}
static DEVICE_ATTR_RW(adapter_power);

int __attribute__((weak)) oplus_abnormal_adapter_disconnect_keep(void)
{
	return 0;
}

static int protocol_type_by_user = -1;
static ssize_t protocol_type_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;
	int fast_chg_type = CHARGER_SUBTYPE_DEFAULT;
	static int last_fast_chg_type = CHARGER_SUBTYPE_DEFAULT;
	int subtype = CHARGER_SUBTYPE_DEFAULT;
	int rc = 0;
	bool wls_online = false;
	bool vooc_online = false;
	static int pre_fast_chg_type = CHARGER_SUBTYPE_DEFAULT;
	union oplus_chg_mod_propval pval = {0, };

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_common_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (((last_fast_chg_type != CHARGER_SUBTYPE_DEFAULT) &&
		(last_fast_chg_type != CHARGER_SUBTYPE_PD) &&
		(last_fast_chg_type != CHARGER_SUBTYPE_QC) &&
		oplus_quirks_keep_connect_status() == 1) ||
		(chip->plc_support &&
		(chip->curr_plc_status == PLC_STATUS_ENABLE || chip->plc_status == PLC_STATUS_WAIT)))
		return sprintf(buf, "%d\n", last_fast_chg_type);

	if ((oplus_vooc_get_fastchg_started() == true) ||
		(oplus_vooc_get_fastchg_to_normal() == true) ||
		(oplus_vooc_get_fastchg_to_warm() == true) ||
		(oplus_vooc_get_fastchg_dummy_started() == true)) {
		vooc_online = true;
	}

	subtype = oplus_chg_get_fast_chg_type();

	if (vooc_online) {
		if (oplus_get_vooc_adapter_type(subtype) == CHARGER_TYPE_SVOOC)
			fast_chg_type = CHARGER_SUBTYPE_FASTCHG_SVOOC;
		else
			fast_chg_type = CHARGER_SUBTYPE_FASTCHG_VOOC;
		pre_fast_chg_type = fast_chg_type;
	} else if (oplus_abnormal_adapter_disconnect_keep()) {
		fast_chg_type = pre_fast_chg_type;
	} else {
		fast_chg_type = subtype;
		if ((subtype == CHARGER_SUBTYPE_PD) || (subtype == CHARGER_SUBTYPE_PPS)) {
			if (!chip->check_pd_svooc_complete)
				fast_chg_type = CHARGER_SUBTYPE_DEFAULT;
		}
	}

	chg_err("fast_chg_type: %d\n", fast_chg_type);

	wls_online = oplus_wpc_get_online_status() || oplus_chg_is_wls_present();
	if (wls_online) {
		if (is_wls_ocm_available(chip)) {
			rc = oplus_chg_mod_get_property(chip->wls_ocm,
							OPLUS_CHG_PROP_WLS_TYPE, &pval);
			if (rc < 0)
				fast_chg_type = CHARGER_SUBTYPE_DEFAULT;
			else if (pval.intval == OPLUS_CHG_WLS_VOOC)
				fast_chg_type = CHARGER_SUBTYPE_FASTCHG_VOOC;
			else if (pval.intval == OPLUS_CHG_WLS_SVOOC || pval.intval == OPLUS_CHG_WLS_PD_65W)
				fast_chg_type = CHARGER_SUBTYPE_FASTCHG_SVOOC;
			else
				fast_chg_type = CHARGER_SUBTYPE_DEFAULT;
		}
	}

	if (protocol_type_by_user > 0)
		fast_chg_type = protocol_type_by_user;

	last_fast_chg_type = fast_chg_type;

	return sprintf(buf, "%d\n", fast_chg_type);
}

static ssize_t protocol_type_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int val = 0;

	if (kstrtos32(buf, 0, &val)) {
		chg_err("buf error\n");
		return -EINVAL;
	}

	protocol_type_by_user = val;

	return count;
}
static DEVICE_ATTR_RW(protocol_type);

#define UI_POWER_SHOW_LIMIT 33000
static int ui_power_by_user = -1;
static ssize_t ui_power_show(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	int adapter_power = 0;
	int project_power = 0;
	int ui_power = 0;
	static int last_ui_power = 0;
	int pps_or_ufcs_power = 0;
	bool ufcs_online = false;
	bool pps_online = false;
	struct oplus_chg_chip *chip = NULL;
	static int pre_ui_power = 0;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_common_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (((last_ui_power != 0) && oplus_quirks_keep_connect_status() == 1) ||
		(chip->plc_support &&
		(chip->curr_plc_status == PLC_STATUS_ENABLE || chip->plc_status == PLC_STATUS_WAIT)))
		return sprintf(buf, "%u\n", last_ui_power);

	if (fast_chg_type_by_user > 0)
		adapter_power = oplus_get_vooc_adapter_power(fast_chg_type_by_user) * 1000;
	else
		adapter_power = oplus_get_adapter_power();
	project_power = oplus_get_project_power();

	ufcs_online = oplus_is_ufcs_charging();
	pps_online = oplus_is_pps_charging();

	if (ufcs_online) {
		pps_or_ufcs_power = oplus_ufcs_get_power();
	} else if (pps_online) {
		pps_or_ufcs_power = oplus_pps_show_power();
	}

	if (ufcs_online || pps_online)
		ui_power = pps_or_ufcs_power * 1000;
	else
		ui_power = min(adapter_power, project_power);

	/* Display policy: when the ui_power is less than the project_power or 33W,
	   the ui_power is 0. */
	if (ui_power < UI_POWER_SHOW_LIMIT || ui_power < project_power)
		ui_power = 0;

	if (ui_power_by_user > 0)
		ui_power = ui_power_by_user;

	if (oplus_abnormal_adapter_disconnect_keep())
		ui_power = pre_ui_power;
	else if (ui_power != 0)
		pre_ui_power = ui_power;

	last_ui_power = ui_power;

	chg_info("ui_power_show: %d %d %d %d %d %d %d\n",
		adapter_power, project_power, ufcs_online, pps_online,
		pps_or_ufcs_power, ui_power, ui_power_by_user);

	return sprintf(buf, "%u\n", ui_power);
}

static ssize_t ui_power_store(struct device *dev, struct device_attribute *attr,
						const char *buf, size_t count)
{
	int val = 0;

	if (kstrtos32(buf, 0, &val)) {
		chg_err("buf error\n");
		return -EINVAL;
	}

	ui_power_by_user = val;

	return count;
}
static DEVICE_ATTR_RW(ui_power);

static int device_power_by_user = -1;
static ssize_t device_power_show(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;
	int project_power = 0;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_common_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	project_power = oplus_get_project_power();

	if (project_power < 0)
		project_power = 0;

	if (device_power_by_user > 0)
		project_power = device_power_by_user;

	chg_info("device_power_show %d %d\n", project_power, device_power_by_user);
	return sprintf(buf, "%u\n", project_power);
}

static ssize_t device_power_store(struct device *dev, struct device_attribute *attr,
							const char *buf, size_t count)
{
	int val = 0;

	if (kstrtos32(buf, 0, &val)) {
		chg_err("buf error\n");
		return -EINVAL;
	}

	device_power_by_user = val;

	return count;
}
static DEVICE_ATTR_RW(device_power);

static int cpa_power_by_user = -1;
static ssize_t cpa_power_show(struct device *dev,
                                      struct device_attribute *attr, char *buf)
{
	int adapter_power = 0;
	int project_power = 0;
	int cpa_power = 0;
	int pps_or_ufcs_power = 0;
	bool ufcs_online = false;
	bool pps_online = false;
	struct oplus_chg_chip *chip = NULL;
	static int pre_cpa_power = 0;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_common_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	adapter_power = oplus_get_adapter_power();
	project_power = oplus_get_project_power();

	ufcs_online = oplus_is_ufcs_charging();
	pps_online = oplus_is_pps_charging();

	if (ufcs_online) {
		pps_or_ufcs_power = oplus_ufcs_get_power();
	} else if (pps_online) {
		pps_or_ufcs_power = oplus_pps_show_power();
	}

	if (ufcs_online || pps_online)
		cpa_power = pps_or_ufcs_power * 1000;
	else
		cpa_power = min(adapter_power, project_power);

	if (cpa_power < 0)
		cpa_power = 0;

	if (cpa_power_by_user > 0)
		cpa_power = cpa_power_by_user;

	if (oplus_abnormal_adapter_disconnect_keep())
		cpa_power = pre_cpa_power;
	else if (cpa_power != 0)
		pre_cpa_power = cpa_power;

	return sprintf(buf, "%u\n", cpa_power);
}

static ssize_t cpa_power_store(struct device *dev, struct device_attribute *attr,
							const char *buf, size_t count)
{
	int val = 0;

	if (kstrtos32(buf, 0, &val)) {
		chg_err("buf error\n");
		return -EINVAL;
	}

	cpa_power_by_user = val;

	return count;
}
static DEVICE_ATTR_RW(cpa_power);

static ssize_t super_endurance_mode_status_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_common_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d\n", chip->super_endurance_mode_status);
}

static ssize_t super_endurance_mode_status_store(struct device *dev, struct device_attribute *attr,
					const char *buf, size_t count)
{
	int val = 0;
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_common_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (kstrtos32(buf, 0, &val)) {
		chg_err("buf error\n");
		return -EINVAL;
	}

	chip->super_endurance_mode_status = val;

	return count;
}
static DEVICE_ATTR_RW(super_endurance_mode_status);

static ssize_t super_endurance_mode_count_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_common_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d\n", chip->super_endurance_mode_count);
}

static ssize_t super_endurance_mode_count_store(struct device *dev, struct device_attribute *attr,
					const char *buf, size_t count)
{
	int val = 0;
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_common_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (kstrtos32(buf, 0, &val)) {
		chg_err("buf error\n");
		return -EINVAL;
	}

	chip->super_endurance_mode_count = val;

	return count;
}
static DEVICE_ATTR_RW(super_endurance_mode_count);

static ssize_t bob_status_reg_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;
	int ret = 0;
	int status_reg = 0;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_common_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (chip->support_super_endurance_mode)
		ret = tps6128xd_get_status_reg();
	if (ret < 0)
		return ret;

	status_reg = ret;

	return sprintf(buf, "%d\n", status_reg);
}
static DEVICE_ATTR_RO(bob_status_reg);

static ssize_t bob_status_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;
	int val = 0;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_common_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}
	if (chip->support_super_endurance_mode)
		val = tps6128xd_get_status();

	return sprintf(buf, "%d\n", val);
}

static ssize_t bob_status_store(struct device *dev, struct device_attribute *attr,
					const char *buf, size_t count)
{
	int val = 0;
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_common_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (kstrtos32(buf, 0, &val)) {
		chg_err("buf error\n");
		return -EINVAL;
	}

	if (chip->support_super_endurance_mode)
		tps6128xd_set_high_batt_vol(val);

	return count;
}
static DEVICE_ATTR_RW(bob_status);

static ssize_t time_zone_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_common_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d\n", chip->track_gmtoff);
}

static ssize_t time_zone_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	int val = 0;
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_common_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (kstrtos32(buf, 0, &val)) {
		chg_err("buf error\n");
		return -EINVAL;
	}

	chip->track_gmtoff = val;

	return count;
}
static DEVICE_ATTR_RW(time_zone);

static ssize_t deep_dischg_counts_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_configfs_device *chip = dev->driver_data;
	int counts = 0;

	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (counts < 0)
		return counts;

	return sprintf(buf, "%d\n", counts);
}

static ssize_t deep_dischg_counts_store(struct device *dev, struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct oplus_configfs_device *chip = dev->driver_data;
	int val = 0;

	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (kstrtos32(buf, 0, &val)) {
		chg_err("buf error\n");
		return -EINVAL;
	}

	return count;
}
static DEVICE_ATTR_RW(deep_dischg_counts);

static ssize_t deep_dischg_count_cali_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_configfs_device *chip = dev->driver_data;
	int counts = 0;

	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (counts < 0)
		return counts;

	return sprintf(buf, "%d\n", counts);
}

static ssize_t deep_dischg_count_cali_store(struct device *dev, struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct oplus_configfs_device *chip = dev->driver_data;
	int val = 0;

	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (kstrtos32(buf, 0, &val)) {
		chg_err("buf error\n");
		return -EINVAL;
	}

	return count;
}
static DEVICE_ATTR_RW(deep_dischg_count_cali);

static ssize_t read_gauge_reg_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_common_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d\n", chip->read_by_reg);
}

#define CHG_UP_PAGE_SIZE 128
#define PARMS_LEN 10
#define SEPRATOR_SIGN ","
static char chg_up_buf[CHG_UP_PAGE_SIZE] = {0};
int oplus_update_chg_up_limit_parms(struct oplus_chg_chip *chip, const char *buf)
{
	int ret = 0;
	char temp_buf[CHG_UP_PAGE_SIZE] = {0};
	char *buf_temp = temp_buf;
	char *buf_to_int_begian = temp_buf;
	int lenth_before = 0;
	int lenth_after = 0;
	char buf_atoi[CHG_UP_PAGE_SIZE] = {0};
	int parms[PARMS_LEN] = {0};
	int n = 0;

	if (NULL == buf) {
		return -ENOMEM;
	}

	if (strlen(buf) > CHG_UP_PAGE_SIZE) {
		chg_info("buf:%s\n", buf);
		return -EINVAL;
	}
	strncpy(temp_buf, buf, strlen(buf));
	strncpy(chg_up_buf, buf, strlen(buf));

	while ((*buf_temp != '\0') && (n < 10)) {
		if (n >= PARMS_LEN) {
			chg_info("array lens is invalid\n");
			break;
		}

		buf_to_int_begian = buf_temp;
		lenth_before = strlen(buf_temp);
		buf_temp = strstr(buf_temp, SEPRATOR_SIGN);

		if (buf_to_int_begian == NULL) {
			return -EINVAL;
		}

		if (buf_temp == NULL) {
			if (kstrtos32(buf_to_int_begian, 0, &parms[n])) {
				chg_err("buf error\n");
			}
			break;
		}
		buf_temp += 1;

		lenth_after = strlen(buf_temp);

		if (lenth_before <= lenth_after + 1) {
			return -EINVAL;
		}

		strncpy(buf_atoi, buf_to_int_begian, (lenth_before - lenth_after - 1));
		if (kstrtos32(buf_atoi, 0, &parms[n])) {
			chg_err("buf_atoi[%d] error\n", n);
		}
		memset(buf_atoi, 0, sizeof(char) * CHG_UP_PAGE_SIZE);
		n++;
	}

	chg_info("chg_up_limit_show %d %d %d %d %d\n", parms[0], parms[1], parms[2], parms[3], parms[4]);
	ret = oplus_set_chg_up_limit(parms[0], parms[1], parms[2], parms[3], parms[4]);

	return ret;
}

static ssize_t chg_up_limit_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int rc;
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	chg_info("chg_up_limit_store:%s\n", buf);
	rc = oplus_update_chg_up_limit_parms(chip, buf);
	if (rc < 0)
		return rc;

	return count;
}

static ssize_t chg_up_limit_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_configfs_device *chip = dev->driver_data;

	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "%s\n", chg_up_buf);
}

DEVICE_ATTR_RW(chg_up_limit);

static ssize_t read_gauge_reg_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	int val = 0;
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_common_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (kstrtos32(buf, 0, &val)) {
		chg_err("buf error\n");
		return -EINVAL;
	}

	chip->read_by_reg = val;

	return count;
}
static DEVICE_ATTR_RW(read_gauge_reg);

static ssize_t non_standard_chg_switch_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;
	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_common_dir);

	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d\n", chip->non_standard_chg_switch);
}

static ssize_t non_standard_chg_switch_store(struct device *dev, struct device_attribute *attr,
					const char *buf, size_t count)
{
	int val = 0;
	struct oplus_chg_chip *chip = NULL;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_common_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (kstrtos32(buf, 0, &val)) {
		chg_err("buf error\n");
		return -EINVAL;
	}

	chip->non_standard_chg_switch = val;
	chg_info("rus value store, rus switch = %d\n", chip->non_standard_chg_switch);
	return count;
}
static DEVICE_ATTR_RW(non_standard_chg_switch);

static ssize_t dec_delta_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_configfs_device *chip = dev->driver_data;
	int counts = 0;

	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}
	counts = oplus_charger_get_dec_delta();

	return sprintf(buf, "%d\n", counts);
}

static ssize_t dec_delta_store(struct device *dev, struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct oplus_configfs_device *chip = dev->driver_data;
	int val = 0;

	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (kstrtos32(buf, 0, &val)) {
		chg_err("buf error\n");
		return -EINVAL;
	}
	oplus_charger_set_dec_delta(val);

	return count;
}
static DEVICE_ATTR_RW(dec_delta);

static ssize_t plc_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_chg_chip *chip = NULL;
	int counts = 0;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (chip->plc_support)
		counts = chip->curr_plc_status;

	return sprintf(buf, "status=%d\n", counts);
}

static ssize_t plc_store(struct device *dev, struct device_attribute *attr, const char *buf,
					 size_t count)
{
	int val = 0;
	struct oplus_chg_chip *chip = NULL;
	char key[64] = { 0 };
	int enable_plc_status = PLC_STATUS_ENABLE;

	chip = (struct oplus_chg_chip *)dev_get_drvdata(oplus_battery_dir);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (!chip->plc_support) {
		return -EINVAL;
	}

	if (sscanf(buf, "%63[^=]=%d", key, &val) != 2) {
		chg_err("buf %s error\n", buf);
		return -EINVAL;
	}

	if (sysfs_streq("switch", key)) {
		if (strncmp(buf, "switch=1|callname=", 18) && strncmp(buf, "switch=0|callname=", 18)) {
			chg_info("buf invalid: %s\n", buf);
			return -EINVAL;
		}
		chg_info("buf=[%s], change switch to  %d\n", buf, val);
		if (chip->curr_plc_status == PLC_STATUS_ENABLE && !val) {
			chip->curr_plc_status = PLC_STATUS_WAIT;
			enable_plc_status = PLC_STATUS_WAIT;
		} else if (chip->curr_plc_status == PLC_STATUS_DISABLE && !!val) {
			chip->curr_plc_status = PLC_STATUS_ENABLE;
			enable_plc_status = PLC_STATUS_ENABLE;
		} else {
			return count;
		}
		oplus_plc_based_buck_setting(chip, enable_plc_status);
	} else if (sysfs_streq("adapter_support_mask", key)) {
		chg_info("buf=[%s], change adapter_support_mask to %x\n", buf, val);
		if (val != chip->plc_support) {
			if(val)
				chip->plc_support = true;
			else
				chip->plc_support = false;
		}
	} else if (sysfs_streq("buck", key)) {
	}
	chg_info("[%d, %d, %d][%d, %d]\n",
		val, enable_plc_status, chip->curr_plc_status,
		chip->plc_support, chip->plc_status);

	return count;
}
static DEVICE_ATTR_RW(plc);

static struct device_attribute *oplus_common_attributes[] = {
#ifdef OPLUS_CHG_ADB_ROOT_ENABLE
	&dev_attr_charge_parameter,
#endif
	&dev_attr_mutual_cmd,
	&dev_attr_track_hidl,
	&dev_attr_boot_completed,
	&dev_attr_chg_olc_config,
	&dev_attr_super_endurance_mode_status,
	&dev_attr_super_endurance_mode_count,
	&dev_attr_bob_status_reg,
	&dev_attr_bob_status,
	&dev_attr_time_zone,
	&dev_attr_battlog_push_config,
	&dev_attr_deep_dischg_counts,
	&dev_attr_deep_dischg_count_cali,
	&dev_attr_read_gauge_reg,
	&dev_attr_adapter_power,
	&dev_attr_protocol_type,
	&dev_attr_ui_power,
	&dev_attr_device_power,
	&dev_attr_cpa_power,
	&dev_attr_chg_up_limit,
	&dev_attr_non_standard_chg_switch,
	&dev_attr_dec_delta,
	&dev_attr_plc,
	NULL
};
#ifdef OPLUS_FEATURE_CHG_BASIC
void __attribute__((weak)) oplus_pps_get_adapter_status(struct oplus_chg_chip *chip)
{
	return;
}
void __attribute__((weak)) oplus_get_pps_parameters_from_adsp(void)
{
	return;
}
int __attribute__((weak)) oplus_pps_get_authenticate(void)
{
	return 0;
}
#endif

/**********************************************************************
* ac/usb/battery/wireless/common directory nodes create
**********************************************************************/
static int oplus_ac_dir_create(struct oplus_chg_chip *chip)
{
	int status = 0;
	dev_t devt;
	struct device_attribute **attrs;
	struct device_attribute *attr;

	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	status = alloc_chrdev_region(&devt, 0, 1, "ac");
	if (status < 0) {
		chg_err("alloc_chrdev_region ac fail!\n");
		return -ENOMEM;
	}
	oplus_ac_dir = device_create(oplus_chg_class, NULL, devt, NULL, "%s", "ac");
	oplus_ac_dir->devt = devt;
	dev_set_drvdata(oplus_ac_dir, chip);

	attrs = oplus_ac_attributes;
	for (attr = *attrs; attr != NULL; attr = *(++attrs)) {
		int err;

		err = device_create_file(oplus_ac_dir, attr);
		if (err) {
			chg_err("device_create_file fail!\n");
			device_destroy(oplus_ac_dir->class, oplus_ac_dir->devt);
			return err;
		}
	}

	return 0;
}

static void oplus_ac_dir_destroy(void)
{
	struct device_attribute **attrs;
	struct device_attribute *attr;

	attrs = oplus_ac_attributes;
	for (attr = *attrs; attr != NULL; attr = *(++attrs))
		device_remove_file(oplus_ac_dir, attr);
	device_destroy(oplus_ac_dir->class, oplus_ac_dir->devt);
	unregister_chrdev_region(oplus_ac_dir->devt, 1);
}

static int oplus_usb_dir_create(struct oplus_chg_chip *chip)
{
	int status = 0;
	dev_t devt;
	struct device_attribute **attrs;
	struct device_attribute *attr;

	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	status = alloc_chrdev_region(&devt, 0, 1, "usb");
	if (status < 0) {
		chg_err("alloc_chrdev_region usb fail!\n");
		return -ENOMEM;
	}
	oplus_usb_dir = device_create(oplus_chg_class, NULL, devt, NULL, "%s", "usb");
	oplus_usb_dir->devt = devt;
	dev_set_drvdata(oplus_usb_dir, chip);

	attrs = oplus_usb_attributes;
	for (attr = *attrs; attr != NULL; attr = *(++attrs)) {
		int err;

		err = device_create_file(oplus_usb_dir, attr);
		if (err) {
			chg_err("device_create_file fail!\n");
			device_destroy(oplus_usb_dir->class, oplus_usb_dir->devt);
			return err;
		}
	}

	return 0;
}

static void oplus_usb_dir_destroy(void)
{
	struct device_attribute **attrs;
	struct device_attribute *attr;

	attrs = oplus_usb_attributes;
	for (attr = *attrs; attr != NULL; attr = *(++attrs))
		device_remove_file(oplus_usb_dir, attr);
	device_destroy(oplus_usb_dir->class, oplus_usb_dir->devt);
	unregister_chrdev_region(oplus_usb_dir->devt, 1);
}

static int oplus_battery_dir_create(struct oplus_chg_chip *chip)
{
	int status = 0;
	dev_t devt;
	struct device_attribute **attrs;
	struct device_attribute *attr;

	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	status = alloc_chrdev_region(&devt, 0, 1, "battery");
	if (status < 0) {
		chg_err("alloc_chrdev_region battery fail!\n");
		return -ENOMEM;
	}

	oplus_battery_dir = device_create(oplus_chg_class, NULL, devt, NULL, "%s", "battery");
	oplus_battery_dir->devt = devt;
	dev_set_drvdata(oplus_battery_dir, chip);

	attrs = oplus_battery_attributes;
	for (attr = *attrs; attr != NULL; attr = *(++attrs)) {
		int err;

		err = device_create_file(oplus_battery_dir, attr);
		if (err) {
			chg_err("device_create_file fail!\n");
			device_destroy(oplus_battery_dir->class, oplus_battery_dir->devt);
			return err;
		}
	}

	return 0;
}

static void oplus_battery_dir_destroy(void)
{
	struct device_attribute **attrs;
	struct device_attribute *attr;

	attrs = oplus_battery_attributes;
	for (attr = *attrs; attr != NULL; attr = *(++attrs))
		device_remove_file(oplus_battery_dir, attr);
	device_destroy(oplus_battery_dir->class, oplus_battery_dir->devt);
	unregister_chrdev_region(oplus_battery_dir->devt, 1);
}

static int oplus_wireless_dir_create(struct oplus_chg_chip *chip)
{
	int status = 0;
	dev_t devt;
	struct device_attribute **attrs;
	struct device_attribute *attr;

	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	status = alloc_chrdev_region(&devt, 0, 1, "wireless");
	if (status < 0) {
		chg_err("alloc_chrdev_region wireless fail!\n");
		return -ENOMEM;
	}

	oplus_wireless_dir = device_create(oplus_chg_class, NULL, devt, NULL, "%s", "wireless");
	oplus_wireless_dir->devt = devt;
	dev_set_drvdata(oplus_wireless_dir, chip);

	attrs = oplus_wireless_attributes;
	for (attr = *attrs; attr != NULL; attr = *(++attrs)) {
		int err;

		err = device_create_file(oplus_wireless_dir, attr);
		if (err) {
			chg_err("device_create_file fail!\n");
			device_destroy(oplus_wireless_dir->class, oplus_wireless_dir->devt);
			return err;
		}
	}

	return 0;
}

static void oplus_wireless_dir_destroy(void)
{
	struct device_attribute **attrs;
	struct device_attribute *attr;

	attrs = oplus_wireless_attributes;
	for (attr = *attrs; attr != NULL; attr = *(++attrs))
		device_remove_file(oplus_wireless_dir, attr);
	device_destroy(oplus_wireless_dir->class, oplus_wireless_dir->devt);
	unregister_chrdev_region(oplus_wireless_dir->devt, 1);
}

static int oplus_common_dir_create(struct oplus_chg_chip *chip)
{
	int status = 0;
	dev_t devt;
	struct device_attribute **attrs;
	struct device_attribute *attr;

	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	status = alloc_chrdev_region(&devt, 0, 1, "common");
	if (status < 0) {
		chg_err("alloc_chrdev_region common fail!\n");
		return -ENOMEM;
	}

	oplus_common_dir = device_create(oplus_chg_class, NULL, devt, NULL, "%s", "common");
	oplus_common_dir->devt = devt;
	dev_set_drvdata(oplus_common_dir, chip);

	attrs = oplus_common_attributes;
	for (attr = *attrs; attr != NULL; attr = *(++attrs)) {
		int err;

		err = device_create_file(oplus_common_dir, attr);
		if (err) {
			chg_err("device_create_file fail!\n");
			device_destroy(oplus_common_dir->class, oplus_common_dir->devt);
			return err;
		}
	}

	return 0;
}

static void oplus_common_dir_destroy(void)
{
	struct device_attribute **attrs;
	struct device_attribute *attr;

	attrs = oplus_common_attributes;
	for (attr = *attrs; attr != NULL; attr = *(++attrs))
		device_remove_file(oplus_common_dir, attr);
	device_destroy(oplus_common_dir->class, oplus_common_dir->devt);
	unregister_chrdev_region(oplus_common_dir->devt, 1);
}

/**********************************************************************
* configfs init APIs
**********************************************************************/
int oplus_ac_node_add(struct device_attribute **ac_attributes)
{
	struct device_attribute **attrs;
	struct device_attribute *attr;

	attrs = ac_attributes;
	for (attr = *attrs; attr != NULL; attr = *(++attrs)) {
		int err;

		err = device_create_file(oplus_ac_dir, attr);
		if (err) {
			chg_err("device_create_file fail!\n");
			device_destroy(oplus_ac_dir->class, oplus_ac_dir->devt);
			return err;
		}
	}

	return 0;
}

void oplus_ac_node_delete(struct device_attribute **ac_attributes)
{
	struct device_attribute **attrs;
	struct device_attribute *attr;

	attrs = ac_attributes;
	for (attr = *attrs; attr != NULL; attr = *(++attrs))
		device_remove_file(oplus_ac_dir, attr);
}

int oplus_usb_node_add(struct device_attribute **usb_attributes)
{
	struct device_attribute **attrs;
	struct device_attribute *attr;

	attrs = usb_attributes;
	for (attr = *attrs; attr != NULL; attr = *(++attrs)) {
		int err;

		err = device_create_file(oplus_usb_dir, attr);
		if (err) {
			chg_err("device_create_file fail!\n");
			device_destroy(oplus_usb_dir->class, oplus_usb_dir->devt);
			return err;
		}
	}

	return 0;
}

void oplus_usb_node_delete(struct device_attribute **usb_attributes)
{
	struct device_attribute **attrs;
	struct device_attribute *attr;

	attrs = usb_attributes;
	for (attr = *attrs; attr != NULL; attr = *(++attrs))
		device_remove_file(oplus_usb_dir, attr);
}

int oplus_battery_node_add(struct device_attribute **battery_attributes)
{
	struct device_attribute **attrs;
	struct device_attribute *attr;

	attrs = battery_attributes;
	for (attr = *attrs; attr != NULL; attr = *(++attrs)) {
		int err;

		err = device_create_file(oplus_battery_dir, attr);
		if (err) {
			chg_err("device_create_file fail!\n");
			device_destroy(oplus_battery_dir->class, oplus_battery_dir->devt);
			return err;
		}
	}

	return 0;
}

void oplus_battery_node_delete(struct device_attribute **battery_attributes)
{
	struct device_attribute **attrs;
	struct device_attribute *attr;

	attrs = battery_attributes;
	for (attr = *attrs; attr != NULL; attr = *(++attrs))
		device_remove_file(oplus_battery_dir, attr);
}

int oplus_wireless_node_add(struct device_attribute **wireless_attributes)
{
	struct device_attribute **attrs;
	struct device_attribute *attr;

	attrs = wireless_attributes;
	for (attr = *attrs; attr != NULL; attr = *(++attrs)) {
		int err;

		err = device_create_file(oplus_wireless_dir, attr);
		if (err) {
			chg_err("device_create_file fail!\n");
			device_destroy(oplus_wireless_dir->class, oplus_wireless_dir->devt);
			return err;
		}
	}

	return 0;
}

void oplus_wireless_node_delete(struct device_attribute **wireless_attributes)
{
	struct device_attribute **attrs;
	struct device_attribute *attr;

	attrs = wireless_attributes;
	for (attr = *attrs; attr != NULL; attr = *(++attrs))
		device_remove_file(oplus_wireless_dir, attr);
}

int oplus_common_node_add(struct device_attribute **common_attributes)
{
	struct device_attribute **attrs;
	struct device_attribute *attr;

	attrs = common_attributes;
	for (attr = *attrs; attr != NULL; attr = *(++attrs)) {
		int err;

		err = device_create_file(oplus_common_dir, attr);
		if (err) {
			chg_err("device_create_file fail!\n");
			device_destroy(oplus_common_dir->class, oplus_common_dir->devt);
			return err;
		}
	}

	return 0;
}

void oplus_common_node_delete(struct device_attribute **common_attributes)
{
	struct device_attribute **attrs;
	struct device_attribute *attr;

	attrs = common_attributes;
	for (attr = *attrs; attr != NULL; attr = *(++attrs))
		device_remove_file(oplus_common_dir, attr);
}

int oplus_chg_configfs_init(struct oplus_chg_chip *chip)
{
	int status = 0;

	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0))
	oplus_chg_class = class_create("oplus_chg");
#else
	oplus_chg_class = class_create(THIS_MODULE, "oplus_chg");
#endif
	if (IS_ERR(oplus_chg_class)) {
		chg_err("oplus_chg_configfs_init fail!\n");
		return PTR_ERR(oplus_chg_class);
	}

	status = oplus_ac_dir_create(chip);
	if (status < 0)
		chg_err("oplus_ac_dir_create fail!\n");

	status = oplus_usb_dir_create(chip);
	if (status < 0)
		chg_err("oplus_usb_dir_create fail!\n");

	status = oplus_battery_dir_create(chip);
	if (status < 0)
		chg_err("oplus_battery_dir_create fail!\n");

	status = oplus_wireless_dir_create(chip);
	if (status < 0)
		chg_err("oplus_wireless_dir_create fail!\n");

	status = oplus_common_dir_create(chip);
	if (status < 0)
		chg_err("oplus_common_dir_create fail!\n");

	return 0;
}
EXPORT_SYMBOL(oplus_chg_configfs_init);

int oplus_chg_configfs_exit(void)
{
	oplus_common_dir_destroy();
	oplus_wireless_dir_destroy();
	oplus_battery_dir_destroy();
	oplus_usb_dir_destroy();
	oplus_ac_dir_destroy();

	if (!IS_ERR(oplus_chg_class))
		class_destroy(oplus_chg_class);
	return 0;
}
EXPORT_SYMBOL(oplus_chg_configfs_exit);

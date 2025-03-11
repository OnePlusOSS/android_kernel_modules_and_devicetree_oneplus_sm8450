//SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-2024 Oplus. All rights reserved.
 */

#define pr_fmt(fmt) "[max77939]([%s][%d]): " fmt, __func__, __LINE__

#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/err.h>
#include <linux/of_gpio.h>
#include <linux/interrupt.h>
#include <linux/power_supply.h>
#include <linux/sched/clock.h>
#include <soc/oplus/device_info.h>
#include <oplus_chg_ic.h>
#include <oplus_chg_module.h>
#include <oplus_chg.h>
#include <oplus_mms_wired.h>
#include <oplus_mms.h>
#include <oplus_impedance_check.h>
#include <oplus_chg_vooc.h>
#include <oplus_chg_comm.h>
#include <oplus_chg_voter.h>
#include "../oplus_voocphy.h"
#include "oplus_max77939.h"
#include "../chglib/oplus_chglib.h"
#include "../monitor/oplus_chg_track.h"

static int max77939_set_vac2v2x_ovpuvp_config(struct oplus_voocphy_manager *chip, bool enable);

struct max77939_device {
	struct device *dev;
	struct i2c_client *client;
	struct regmap *regmap;
	struct oplus_voocphy_manager *voocphy;
	struct ufcs_dev *ufcs;

	struct oplus_chg_ic_dev *cp_ic;

	struct mutex i2c_rw_lock;
	struct mutex chip_lock;
	atomic_t suspended;
	atomic_t i2c_err_count;
	struct wakeup_source *chip_ws;

	int ovp_reg;
	int ocp_reg;
	int batt_status;

	bool ufcs_enable;

	enum oplus_cp_work_mode cp_work_mode;

	bool rested;
	bool error_reported;

	bool use_ufcs_phy;
	bool use_vooc_phy;
	bool vac_support;
	bool voocphy_enable;

	struct oplus_mms *comm_topic;
	struct mms_subscribe *comm_subs;

	struct delayed_work set_automode_work;
};

static enum oplus_cp_work_mode g_cp_support_work_mode[] = {
	CP_WORK_MODE_BYPASS,
};

static int max77939_dump_registers(struct oplus_voocphy_manager *chip);

#define I2C_ERR_MAX	10
static void max77939_i2c_error(struct oplus_voocphy_manager *voocphy, bool happen)
{
	struct vphy_chip *v_chip = NULL;

	if (!voocphy)
		return;

	if (happen) {
		voocphy->voocphy_iic_err = true;
		voocphy->voocphy_iic_err_num++;
		if (voocphy->voocphy_iic_err_num >= I2C_ERR_MAX) {
			v_chip = oplus_chglib_get_vphy_chip(voocphy->dev);
			if (v_chip)
				oplus_chglib_creat_i2c_err(v_chip->dev); /* CP I2C error */
		}
	} else {
		voocphy->voocphy_iic_err_num = 0;
	}
}

static bool max77939_check_work_mode_support(enum oplus_cp_work_mode mode)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(g_cp_support_work_mode); i++) {
		if (g_cp_support_work_mode[i] == mode)
			return true;
	}
	return false;
}

static int __max77939_read_byte(struct i2c_client *client, u8 reg, u8 *data)
{
	s32 ret = 0;
	struct oplus_voocphy_manager *chip = i2c_get_clientdata(client);

	ret = i2c_smbus_read_byte_data(client, reg);
	if (ret < 0) {
		max77939_i2c_error(chip, true);
		chg_err("i2c read fail: can't read from reg 0x%02X\n", reg);
		return ret;
	}

	*data = (u8) ret;

	return 0;
}

static int __max77939_write_byte(struct i2c_client *client, int reg, u8 val)
{
	s32 ret = 0;
	struct oplus_voocphy_manager *chip = i2c_get_clientdata(client);

	ret = i2c_smbus_write_byte_data(client, reg, val);
	if (ret < 0) {
		max77939_i2c_error(chip, true);
		chg_err("i2c write fail: can't write 0x%02X to reg 0x%02X: %d\n",
		       val, reg, ret);
		return ret;
	}

	return 0;
}

static int max77939_read_byte(struct i2c_client *client, u8 reg, u8 *data)
{
	struct max77939_device *chip = NULL;
	struct oplus_voocphy_manager *voocphy = i2c_get_clientdata(client);
	int ret = 0;

	if (voocphy == NULL) {
		chg_err("voocphy is NULL\n");
		return -ENODEV;
	}
	chip = voocphy->priv_data;
	if (chip == NULL) {
		chg_err("max77939 chip is NULL\n");
		return -ENODEV;
	}

	mutex_lock(&chip->i2c_rw_lock);
	ret = __max77939_read_byte(client, reg, data);
	mutex_unlock(&chip->i2c_rw_lock);

	return ret;
}

static int max77939_write_byte(struct i2c_client *client, u8 reg, u8 data)
{
	struct max77939_device *chip = NULL;
	struct oplus_voocphy_manager *voocphy = i2c_get_clientdata(client);
	int ret = 0;

	if (voocphy == NULL) {
		chg_err("voocphy is NULL\n");
		return -ENODEV;
	}

	chip = voocphy->priv_data;
	if (chip == NULL) {
		chg_err("max77939 chip is NULL\n");
		return -ENODEV;
	}

	mutex_lock(&chip->i2c_rw_lock);
	ret = __max77939_write_byte(client, reg, data);
	mutex_unlock(&chip->i2c_rw_lock);

	return ret;
}

static int max77939_update_bits(struct i2c_client *client, u8 reg,
                              u8 mask, u8 data)
{
	struct max77939_device *chip = NULL;
	struct oplus_voocphy_manager *voocphy = i2c_get_clientdata(client);
	int ret;
	u8 tmp;

	if (voocphy == NULL) {
		chg_err("voocphy is NULL\n");
		return -ENODEV;
	}

	chip = voocphy->priv_data;
	if (chip == NULL) {
		chg_err("max77939 chip is NULL\n");
		return -ENODEV;
	}

	mutex_lock(&chip->i2c_rw_lock);
	ret = __max77939_read_byte(client, reg, &tmp);
	if (ret) {
		chg_err("failed to read %02X register, ret=%d\n", reg, ret);
		goto out;
	}

	tmp &= ~mask;
	tmp |= data & mask;

	ret = __max77939_write_byte(client, reg, tmp);
	if (ret)
		chg_err("failed to write %02X register, ret=%d\n", reg, ret);
out:
	mutex_unlock(&chip->i2c_rw_lock);

	return ret;
}

static s32 max77939_read_word(struct i2c_client *client, u8 reg)
{
	s32 ret = 0;
	struct oplus_voocphy_manager *voocphy = i2c_get_clientdata(client);
	struct max77939_device *chip = NULL;

	if (voocphy == NULL) {
		chg_err("voocphy is NULL\n");
		return -ENODEV;
	}

	chip = voocphy->priv_data;
	if (chip == NULL) {
		chg_err("max77939 chip is NULL\n");
		return -ENODEV;
	}

	mutex_lock(&chip->i2c_rw_lock);
	ret = i2c_smbus_read_word_data(client, reg);
	if (ret < 0) {
		max77939_i2c_error(voocphy, true);
		chg_err("i2c read word fail: can't read reg:0x%02X \n", reg);
		mutex_unlock(&chip->i2c_rw_lock);
		return ret;
	}
	mutex_unlock(&chip->i2c_rw_lock);

	return ret;
}

static s32 max77939_write_word(struct i2c_client *client, u8 reg, u16 val)
{
	s32 ret = 0;
	struct oplus_voocphy_manager *voocphy = i2c_get_clientdata(client);
	struct max77939_device *chip;

	if (voocphy == NULL) {
		chg_err("voocphy is NULL\n");
		return -ENODEV;
	}

	chip = voocphy->priv_data;
	if (chip == NULL) {
		chg_err("max77939 chip is NULL\n");
		return -ENODEV;
	}

	mutex_lock(&chip->i2c_rw_lock);
	ret = i2c_smbus_write_word_data(client, reg, val);
	if (ret < 0) {
		max77939_i2c_error(voocphy, true);
		chg_err("i2c write word fail: can't write 0x%02X to reg:0x%02X\n", val, reg);
		mutex_unlock(&chip->i2c_rw_lock);
		return ret;
	}
	mutex_unlock(&chip->i2c_rw_lock);

	return 0;
}

static int max77939_read_i2c_nonblock(struct i2c_client *client, u8 reg, u8 length, u8 *data)
{
	int rc = 0;
	int retry = 3;
	struct oplus_voocphy_manager *voocphy = NULL;

	if (client == NULL) {
		chg_err("client is NULL\n");
		return -ENODEV;
	}
	voocphy = i2c_get_clientdata(client);

	rc = i2c_smbus_read_i2c_block_data(client, reg, length, data);
	if (rc < 0) {
		while (retry > 0) {
			usleep_range(5000, 5000);
			rc = i2c_smbus_read_i2c_block_data(client, reg, length, data);
			if (rc < 0)
				retry--;
			else
				break;
		}
	}

	if (rc < 0) {
		max77939_i2c_error(voocphy, true);
		chg_err("read err, rc = %d,\n", rc);
	}

	return rc;
}

static int max77939_read_i2c_block(struct i2c_client *client, u8 reg, u8 length, u8 *data)
{
	struct max77939_device *chip = NULL;
	struct oplus_voocphy_manager *voocphy = i2c_get_clientdata(client);
	int rc = 0;
	int retry = 3;

	if (voocphy == NULL) {
		chg_err("voocphy is NULL\n");
		return -ENODEV;
	}
	chip = voocphy->priv_data;
	if (chip == NULL) {
		chg_err("chip is NULL\n");
		return -ENODEV;
	}

	mutex_lock(&chip->i2c_rw_lock);
	rc = i2c_smbus_read_i2c_block_data(client, reg, length, data);

	if (rc < 0) {
		while (retry > 0) {
			usleep_range(5000, 5000);
			rc = i2c_smbus_read_i2c_block_data(client, reg, length, data);
			if (rc < 0)
				retry--;
			else
				break;
		}
	}

	if (rc < 0) {
		max77939_i2c_error(voocphy, true);
		chg_err("read err, rc = %d,\n", rc);
	} else {
		max77939_i2c_error(voocphy, false);
	}
	mutex_unlock(&chip->i2c_rw_lock);

	return rc;
}


static int max77939_set_predata(struct oplus_voocphy_manager *chip, u16 val)
{
	s32 ret = 0;

	if (!chip) {
		chg_err("chip is null\n");
		return -EINVAL;
	}

	/* predata */
	ret = max77939_write_word(chip->client, MAX77939_REG_2A, val);
	if (ret < 0) {
		chg_err("failed to write predata(%d)\n", ret);
		return ret;
	}

	chg_info("write predata 0x%0x\n", val);

	return ret;
}

static int max77939_set_txbuff(struct oplus_voocphy_manager *chip, u16 val)
{
	s32 ret = 0;

	if (!chip) {
		chg_err("chip is null\n");
		return -EINVAL;
	}

	/* txbuff */
	ret = max77939_write_word(chip->client, MAX77939_REG_25, val);
	if (ret < 0) {
		chg_err("failed to  write txbuff(%d)\n", ret);
		return ret;
	}

	return ret;
}

static int max77939_get_adapter_info(struct oplus_voocphy_manager *chip)
{
	s32 data;

	if (!chip) {
		chg_err("chip is null\n");
		return -EINVAL;
	}

	data = max77939_read_word(chip->client, MAX77939_REG_27);

	if (data < 0) {
		chg_err("failed to get adapter info(%d)\n", data);
		return data;
	}

	VOOCPHY_DATA16_SPLIT(data, chip->voocphy_rx_buff, chip->vooc_flag);
	chg_info("data: 0x%0x, vooc_flag: 0x%0x, vooc_rxdata: 0x%0x\n",
		 data, chip->vooc_flag, chip->voocphy_rx_buff);

	return 0;
}

static void max77939_update_data(struct oplus_voocphy_manager *chip)
{
	if (!chip) {
		chg_err("chip is null\n");
		return;
	}

	/* interrupt_flag */
	/* max77939_read_byte(chip->client, MAX77939_REG_09, &data); */
	chip->interrupt_flag = 0;
	chip->cp_vsys = 0;
	chip->cp_vbat = 0;
	chip->cp_ichg = 0;
	chip->cp_vbus = 0;

	chg_info("cp_ichg = %d cp_vbus = %d, cp_vsys = %d cp_vbat = %d interrupt_flag = %d",
		 chip->cp_ichg, chip->cp_vbus, chip->cp_vsys, chip->cp_vbat, chip->interrupt_flag);
}

static int max77939_cp_vbus(struct oplus_voocphy_manager *chip)
{
	int vbus;
	struct oplus_mms *wired_topic = NULL;
	union mms_msg_data data = { 0 };

	wired_topic = oplus_mms_get_by_name("wired");
	if (!wired_topic)
		return 0;

	oplus_mms_get_item_data(wired_topic, WIRED_ITEM_VBUS, &data, true);
	vbus = data.intval;

	return vbus;
}

static int max77939_get_cp_ichg(struct oplus_voocphy_manager *chip)
{
	return 0;
}

static int max77939_get_cp_vbat(struct oplus_voocphy_manager *chip)
{
	return 0;
}

static int max77939_reg_reset(struct oplus_voocphy_manager *chip, bool enable)
{
	int ret = 0;
	u8 val = 0;

	if (enable)
		val = MAX77939_RESET_REG;
	else
		val = MAX77939_NO_REG_RESET;

	val <<= MAX77939_REG_RESET_SHIFT;

	ret = max77939_update_bits(chip->client, MAX77939_REG_06,
	                         MAX77939_REG_RESET_MASK, val);

	chg_info("%s register reset\n", enable ? "enable" : "false");

	return ret;
}

static int max77939_get_chg_enable(struct oplus_voocphy_manager *chip, u8 *data)
{
	int ret = 0;

	if (!chip) {
		chg_err("chip is null\n");
		return -EINVAL;
	}

	ret = max77939_read_byte(chip->client, MAX77939_REG_02, data);
	if (ret < 0) {
		chg_err("failed get chg enable status(%d)\n", ret);
		return ret;
	}

	*data = *data & MAX77939_CHG_EN_MASK;

	return ret;
}

static int max77939_set_chg_enable(struct oplus_voocphy_manager *chip, bool enable)
{
	int ret = 0;

	if (!chip) {
		chg_err("chip is null\n");
		return -EINVAL;
	}

	/* add for max77939 open mos should close vac2v2x_uvpovp, otherwise lead to intermittent */
	max77939_set_vac2v2x_ovpuvp_config(chip, !enable);

	/* vac range disable */
	max77939_write_byte(chip->client, MAX77939_REG_02, 0x7a);

	if (enable)
		ret = max77939_write_byte(chip->client, MAX77939_REG_02, ENABLE_MOS); /* enable mos */
	else
		ret = max77939_write_byte(chip->client, MAX77939_REG_02, DISENABLE_MOS); /* disable mos */

	if (ret < 0) {
		chg_err("failed to set chg enable(%d)\n", ret);
		return ret;
	}

	chg_info("%s\n", enable ? "enable" : "false");

	return ret;
}


static int max77939_get_adc_enable(struct oplus_voocphy_manager *chip, u8 *data)
{
	return 0;
}

static int max77939_set_adc_enable(struct oplus_voocphy_manager *chip, bool enable)
{
	return 0;
}

static u8 max77939_get_vbus_status(struct oplus_voocphy_manager *chip)
{
	int ret = 0;
	u8 value = 0;

	if (!chip) {
		chg_err("chip is null\n");
		return -EINVAL;
	}

	ret = max77939_read_byte(chip->client, MAX77939_REG_0B, &value);
	if (ret < 0) {
		chg_err("failed to get vbus status(%d)\n", ret);
		return ret;
	}

	value = (value & VBUS_INRANGE_STATUS_MASK) >> VBUS_INRANGE_STATUS_SHIFT;
	chg_info("vbus status: %d\n", value);

	return value;
}

static int max77939_get_voocphy_enable(struct oplus_voocphy_manager *chip, u8 *data)
{
	int ret = 0;

	if (!chip) {
		chg_err("chip is null\n");
		return -EINVAL;
	}

	ret = max77939_read_byte(chip->client, MAX77939_REG_2B, data);
	if (ret < 0)
		chg_err("failed to get voocphy enable status(%d)\n", ret);

	return ret;
}

static void max77939_dump_reg_in_err_issue(struct oplus_voocphy_manager *chip)
{
	if (!chip) {
		chg_err("chip is null\n");
		return;
	}

	chg_info("MAX77939_REG_09 -->09~0E[0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x]\n",
		 chip->reg_dump[9], chip->reg_dump[10], chip->reg_dump[11],
		 chip->reg_dump[12], chip->reg_dump[13], chip->reg_dump[14]);

	return;
}

static u8 max77939_get_int_value(struct oplus_voocphy_manager *chip)
{
	int ret = 0;
	u8 int_column[6];

	if (!chip) {
		chg_err("chip is null\n");
		return -EINVAL;
	}

	ret = max77939_read_i2c_block(chip->client, MAX77939_REG_09, 6, int_column);
	if (ret < 0) {
		max77939_i2c_error(chip, true);
		chg_err("read MAX77939_REG_09 6 bytes failed(%d)\n", ret);
		memset(chip->int_column, 0, sizeof(chip->int_column));
		/* set int_column[1]=1, otherwise the dcdc chg can't be enabled*/
		chip->int_column[1] = BIT(0);
		return chip->int_column[1];
	}

	memcpy(chip->int_column, int_column, sizeof(chip->int_column));
	chg_info("MAX77939_REG_09 -->09~0E[0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x]\n",
		 chip->int_column[0], chip->int_column[1], chip->int_column[2],
		 chip->int_column[3], chip->int_column[4], chip->int_column[5]);

	return int_column[1];
}

static int oplus_chg_get_vooc_online(void)
{
	int vooc_online_true = 0;
	int online = 0;
	int online_keep = 0;
	struct oplus_mms *vooc_topic;
	union mms_msg_data data = { 0 };
	int rc;

	vooc_topic = oplus_mms_get_by_name("vooc");
	if (!vooc_topic)
		return 0;

	rc = oplus_mms_get_item_data(vooc_topic, VOOC_ITEM_ONLINE, &data, false);
	if (!rc)
		online = data.intval;

	rc = oplus_mms_get_item_data(vooc_topic, VOOC_ITEM_ONLINE_KEEP, &data, false);
	if (!rc)
		online_keep = data.intval;

	vooc_online_true = (online || online_keep);

	return vooc_online_true;
}

static int oplus_get_batt_health(void)
{
	int batt_health = POWER_SUPPLY_HEALTH_GOOD;
	struct oplus_mms *comm_topic;
	union mms_msg_data data = { 0 };
	int rc;

	comm_topic = oplus_mms_get_by_name("common");
	if (!comm_topic)
		return 0;

	rc = oplus_mms_get_item_data(comm_topic, COMM_ITEM_BATT_HEALTH, &data, true);
	if (!rc)
		batt_health = data.intval;

	return batt_health;
}

static int oplus_get_prop_status(void)
{
	int prop_status = 0;
	struct oplus_mms *comm_topic;
	union mms_msg_data data = { 0 };
	int rc;

	comm_topic = oplus_mms_get_by_name("common");
	if (!comm_topic)
		return 0;

	rc = oplus_mms_get_item_data(comm_topic, COMM_ITEM_BATT_STATUS, &data, true);
	if (!rc)
		prop_status = data.intval;

	return prop_status;
}

static u8 max77939_get_chg_auto_mode(struct oplus_voocphy_manager *chip)
{
	int ret = 0;
	u8 value = 0;

	if (!chip) {
		chg_err("chip is null\n");
		return -EINVAL;
	}
	ret = max77939_read_byte(chip->client, MAX77939_REG_15, &value);
	value = value & MAX77939_CHG_MODE_MASK;
	chg_info("get auto mode value = %d\n", value);

	return value;
}

static int max77939_set_chg_auto_mode(struct oplus_voocphy_manager *chip, bool enable)
{
	int ret = 0;
	int retry_cnt = 0;
	u8 chg_mode = 0;
	int vooc_online = 0;
	int prop_status = 0;
	int batt_health = POWER_SUPPLY_HEALTH_GOOD;

	if (!chip) {
		chg_err("chip is null\n");
		return -EINVAL;
	}

	chg_mode = max77939_get_chg_auto_mode(chip);
	vooc_online = oplus_chg_get_vooc_online();
	prop_status = oplus_get_prop_status();
	batt_health = oplus_get_batt_health();
	chg_info("enable = %d, chg_mode = %d, vooc_online = %d, prop_status = %d\n",
		  enable, chg_mode, vooc_online, prop_status);

	if (enable && (chg_mode == MAX77939_CHG_FIX_MODE) &&
	    /* when is cold and hot temp region, should not set force
	       automode to avoid max77939 do not access SVOOC handshake */
	    (batt_health != POWER_SUPPLY_HEALTH_COLD &&
	    batt_health != POWER_SUPPLY_HEALTH_OVERHEAT)) {
		/* not force automode to fix CP ERR_TRANS_DET_FLAG when enter SVOOC charging. */
		if (!vooc_online || (prop_status != POWER_SUPPLY_STATUS_CHARGING && vooc_online)) {
			do {
				ret = max77939_update_bits(chip->client, MAX77939_REG_15,
						MAX77939_CHG_MODE_MASK, MAX77939_CHG_AUTO_MODE);
				chg_mode = max77939_get_chg_auto_mode(chip);
				if (chg_mode == MAX77939_CHG_AUTO_MODE || retry_cnt >= 10)
					break;
				retry_cnt++;
				mdelay(10);
			} while (1);
		} else {
			chg_info("current is svooc charging, not force automode!");
		}
	} else if (!enable && (chg_mode == MAX77939_CHG_AUTO_MODE)) {
		ret = max77939_update_bits(chip->client, MAX77939_REG_15,
					 MAX77939_CHG_MODE_MASK,
					 MAX77939_CHG_FIX_MODE);
	}
	if (ret < 0)
		chg_err("failed to %s auto mode\n", enable ? "enable" : "disable");

	return ret;
}

static bool oplus_max77939_get_charge_pause(void)
{
	struct votable *disable_votable;
	struct votable *suspend_votable;
	bool disable_charger = false;
	bool suspend_charger = false;

	disable_votable = find_votable("WIRED_CHARGING_DISABLE");
	if (!disable_votable) {
		chg_err("WIRED_CHARGING_DISABLE votable not found\n");
		return false;
	}

	suspend_votable = find_votable("WIRED_CHARGE_SUSPEND");
	if (!suspend_votable) {
		chg_err("WIRED_CHARGE_SUSPEND votable not found\n");
		return false;
	}

	disable_charger = get_effective_result(disable_votable);
	suspend_charger = get_effective_result(suspend_votable);

	return disable_charger || suspend_charger;
}

static void max77939_set_automode_work(struct work_struct *work)
{
	struct max77939_device *chip = container_of(work,
		struct max77939_device, set_automode_work.work);
	int vbus_mv;
	struct oplus_voocphy_manager *voocphy = NULL;
	bool charge_pause = oplus_max77939_get_charge_pause();

	voocphy = chip->voocphy;
	if (!voocphy) {
		chg_err("voocphy is NULL\n");
		return;
	}

	vbus_mv = max77939_cp_vbus(voocphy);
	if (chip->batt_status != POWER_SUPPLY_STATUS_CHARGING &&
	    vbus_mv > 2500 && charge_pause) {
		max77939_set_chg_auto_mode(voocphy, true);
		chg_info("batt status is %d, vbus = %d\n", chip->batt_status, vbus_mv);
	} else {
		chg_info("not set automode\n");
	}
}

static void oplus_max77939_comm_subs_callback(struct mms_subscribe *subs,
					      enum mms_msg_type type, u32 id, bool sync)
{
	union mms_msg_data data = { 0 };
	struct max77939_device *chip = NULL;

	if (!subs) {
		chg_err("subs is NULL\n");
		return;
	}
	chip = subs->priv_data;
	if (!chip) {
		chg_err("chip is NULL\n");
		return;
	}

	switch (type) {
	case MSG_TYPE_ITEM:
		switch (id) {
		case COMM_ITEM_BATT_STATUS:
			oplus_mms_get_item_data(chip->comm_topic, id, &data, false);
			chip->batt_status = data.intval;
			cancel_delayed_work(&chip->set_automode_work);
			schedule_delayed_work(&chip->set_automode_work, 0);
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}
}

static void oplus_max77939_subscribe_comm_topic(struct oplus_mms *topic,
					        void *prv_data)
{
	struct max77939_device *chip = prv_data;

	chip->comm_topic = topic;
	chip->comm_subs = oplus_mms_subscribe(chip->comm_topic, chip, oplus_max77939_comm_subs_callback, "max77939");
	if (IS_ERR_OR_NULL(chip->comm_subs)) {
		chg_err("subscribe common topic error, rc=%ld\n", PTR_ERR(chip->comm_subs));
		return;
	}
}

static int max77939_set_vac2v2x_ovpuvp_config(struct oplus_voocphy_manager *chip, bool enable)
{
	int ret = 0;
	int val = 0;

	if (!chip) {
		chg_err("chip is null\n");
		return -EINVAL;
	}

	if (enable)
		val = MAX77939_VAC2V2X_OVPUVP_ENABLE;
	else
		val = MAX77939_VAC2V2X_OVPUVP_DISABLE;

	val <<= MAX77939_VAC2V2X_OVPUVP_SHIFT;
	ret = max77939_update_bits(chip->client, MAX77939_REG_15,
				   MAX77939_VAC2V2X_OVPUVP_MASK, val);

	return ret;
}

static void max77939_set_pd_svooc_config(struct oplus_voocphy_manager *chip, bool enable)
{
	if (!chip) {
		chg_err("chip is null\n");
		return;
	}

	max77939_write_byte(chip->client, MAX77939_REG_04, 0x36); /* WD:1000ms */
	max77939_write_byte(chip->client, MAX77939_REG_2C, 0xd1); /* Loose_det=1 */

	chg_info("pd svooc \n");
}

static bool max77939_get_pd_svooc_config(struct oplus_voocphy_manager *chip)
{
	if (!chip) {
		chg_err("Failed\n");
		return false;
	}
	chg_info("no ucp flag,return true\n");

	return true;
}

static void max77939_send_handshake(struct oplus_voocphy_manager *chip)
{
	/* enable voocphy and handshake */
	max77939_write_byte(chip->client, MAX77939_REG_24, 0x81);
}


static int max77939_reset_voocphy(struct oplus_voocphy_manager *chip)
{
	struct max77939_device *device = NULL;

	/* turn off mos */
	max77939_write_byte(chip->client, MAX77939_REG_02, 0x78);
	/* clear tx data */
	max77939_write_byte(chip->client, MAX77939_REG_25, 0x00);
	max77939_write_byte(chip->client, MAX77939_REG_26, 0x00);
	/* disable vooc phy irq */
	max77939_write_byte(chip->client, MAX77939_REG_29, 0x7F); /* mask all flag */
	max77939_write_byte(chip->client, MAX77939_REG_10, 0x78); /* disable irq, different from SC8517, enable mode_change_flg */
	/* set D+ HiZ */
	max77939_write_byte(chip->client, MAX77939_REG_20, 0xc0);

	/* select big bang mode */
	/* disable vooc */
	max77939_write_byte(chip->client, MAX77939_REG_24, 0x00);
	max77939_write_byte(chip->client, MAX77939_REG_04, 0x06); /* disable wdt */

	/* set predata */
	/* max77939_write_word(chip->client, MAX77939_REG_2A, 0x0); */
	max77939_write_byte(chip->client, MAX77939_REG_24, 0x02); /* reset voocphy */
	msleep(1);
	/* max77939_set_predata(0x0); */

	device = chip->priv_data;
	if (device)
		device->voocphy_enable = false;

	chg_info("oplus_vooc_reset_voocphy done");

	return VOOCPHY_SUCCESS;
}


static int max77939_reactive_voocphy(struct oplus_voocphy_manager *chip)
{
	/* set predata 0 */
	max77939_write_word(chip->client, MAX77939_REG_2A, 0x0);

	/* dpdm */
	max77939_write_byte(chip->client, MAX77939_REG_20, 0x21);
	max77939_write_byte(chip->client, MAX77939_REG_21, 0x80);
	max77939_write_byte(chip->client, MAX77939_REG_2C, 0xD1);

	/* clear tx data */
	max77939_write_byte(chip->client, MAX77939_REG_25, 0x00);
	max77939_write_byte(chip->client, MAX77939_REG_26, 0x00);

	/* vooc */
	max77939_write_byte(chip->client, MAX77939_REG_29, 0x05); /* diable vooc int flag mask */
	max77939_send_handshake(chip);

	chg_info("oplus_vooc_reactive_voocphy done");

	return VOOCPHY_SUCCESS;
}

static int max77939_init_device(struct oplus_voocphy_manager *chip)
{
	max77939_reg_reset(chip, true);
	msleep(10);

	max77939_write_byte(chip->client, MAX77939_REG_24, 0x00); /* VOOC_CTRL:disable,no handshake */
	chg_info("\n");

	return 0;
}

static int max77939_init_vooc(struct oplus_voocphy_manager *chip)
{
	struct max77939_device *device = chip->priv_data;

	if (NULL != device)
		device->voocphy_enable = true;

	chg_info("start init vooc\n");

	max77939_write_byte(chip->client, MAX77939_REG_24, 0x02); /* reset voocphy */
	max77939_write_word(chip->client, MAX77939_REG_2A, 0x00); /* reset predata 00 */
	msleep(1);
	max77939_write_byte(chip->client, MAX77939_REG_20, 0x21); /* VOOC_CTRL:disable,no handshake */
	max77939_write_byte(chip->client, MAX77939_REG_21, 0x80); /* VOOC_CTRL:disable,no handshake */
	max77939_write_byte(chip->client, MAX77939_REG_2C, 0xD1); /* VOOC_CTRL:disable,no handshake */
	max77939_write_byte(chip->client, MAX77939_REG_29, 0x25); /* mask sedseq flag,rx start,tx done */
	/* max77939_vac_inrange_enable(chip, MAX77939_VAC_INRANGE_DISABLE); */

	return 0;
}

static void max77939_hardware_init(struct oplus_voocphy_manager *chip)
{
	chg_info("\n");
	max77939_reg_reset(chip, true);
	msleep(10);
	max77939_write_byte(chip->client, MAX77939_REG_01, 0x5e); /* disable v1x_scp */
	max77939_write_byte(chip->client, MAX77939_REG_06, 0x85); /* enable  audio mode */
	max77939_write_byte(chip->client, MAX77939_REG_08, 0xA6); /* REF_SKIP_R 40mv */
	max77939_write_byte(chip->client, MAX77939_REG_29, 0x05); /* Masked Pulse_filtered, RX_Start,Tx_Done */
	max77939_write_byte(chip->client, MAX77939_REG_10, 0x78); /* Masked Pulse_filtered, RX_Start,Tx_Done. Note: different from SC8517 soft enable intflag */
	max77939_write_byte(chip->client, MAX77939_REG_03, 0xFF); /* set rvs and fwd ocp */
	max77939_write_byte(chip->client, MAX77939_REG_18, 0x9F); /* set FWD_OCP1 as 14.25A,Note: this if different from SC8517 */
}

static int max77939_dump_registers(struct oplus_voocphy_manager *chip)
{
	int ret = 0;
	u8 reg[16] = {0};

	if (!chip) {
		chg_err("chip is null\n");
		return -EINVAL;
	}

	ret = max77939_read_i2c_block(chip->client, MAX77939_REG_00, 16, reg);
	if (ret < 0) {
		chg_err("read MAX77939_REG_00 16 bytes failed[%d]\n", ret);
		return ret;
	}
	chg_info("[00~0F][0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x, 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x]\n",
		  reg[0], reg[1], reg[2], reg[3], reg[4], reg[5], reg[6], reg[7],
		  reg[8], reg[9], reg[10], reg[11], reg[12], reg[13], reg[14], reg[15]);

	ret = max77939_read_i2c_block(chip->client, MAX77939_REG_10, 16, reg);
	if (ret < 0) {
		chg_err("read MAX77939_REG_10 16 bytes failed[%d]\n", ret);
		return ret;
	}
	chg_info("[10~1F][0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x, 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x]\n",
		  reg[0], reg[1], reg[2], reg[3], reg[4], reg[5], reg[6], reg[7],
		  reg[8], reg[9], reg[10], reg[11], reg[12], reg[13], reg[14], reg[15]);

	ret = max77939_read_i2c_block(chip->client, 0x20, 14, reg);
	if (ret < 0) {
		chg_err("read MAX77939_REG_20 16 bytes failed[%d]\n", ret);
		return ret;
	}
	chg_info("[20~2D][0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x, 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x]\n",
		  reg[0], reg[1], reg[2], reg[3], reg[4], reg[5], reg[6], reg[7],
		  reg[8], reg[9], reg[10], reg[11], reg[12], reg[13]);

	return ret;
}

static int max77939_svooc_hw_setting(struct oplus_voocphy_manager *chip)
{
	max77939_write_byte(chip->client, MAX77939_REG_04, 0x36); /* WD:1000ms */
	max77939_write_byte(chip->client, MAX77939_REG_2C, 0xd1); /* Loose_det=1 */

	return 0;
}

static int max77939_vooc_hw_setting(struct oplus_voocphy_manager *chip)
{
	max77939_write_byte(chip->client, MAX77939_REG_04, 0x46); /* WD:5000ms */
	max77939_write_byte(chip->client, MAX77939_REG_2C, 0xd1); /* Loose_det=1 */

	return 0;
}

static int max77939_5v2a_hw_setting(struct oplus_voocphy_manager *chip)
{
	max77939_write_byte(chip->client, MAX77939_REG_02, 0x78); /* disable acdrv,close mos */
	max77939_write_byte(chip->client, MAX77939_REG_04, 0x06); /* dsiable wdt */
	max77939_write_byte(chip->client, MAX77939_REG_24, 0x00); /* close voocphy */

	return 0;
}

static int max77939_pdqc_hw_setting(struct oplus_voocphy_manager *chip)
{
	max77939_write_byte(chip->client, MAX77939_REG_02, 0x78); /* disable acdrv,close mos */
	max77939_write_byte(chip->client, MAX77939_REG_04, 0x06); /* dsiable wdt */
	max77939_write_byte(chip->client, MAX77939_REG_24, 0x00); /* close voocphy */

	return 0;
}

static int max77939_hw_setting(struct oplus_voocphy_manager *chip, int reason)
{
	if (!chip) {
		chg_err("chip is null exit\n");
		return -EINVAL;
	}

	switch (reason) {
	case SETTING_REASON_PROBE:
	case SETTING_REASON_RESET:
		max77939_init_device(chip);
		chg_info("SETTING_REASON_RESET OR PROBE\n");
		break;
	case SETTING_REASON_SVOOC:
		max77939_svooc_hw_setting(chip);
		chg_info("SETTING_REASON_SVOOC\n");
		break;
	case SETTING_REASON_VOOC:
		max77939_vooc_hw_setting(chip);
		chg_info("SETTING_REASON_VOOC\n");
		break;
	case SETTING_REASON_5V2A:
		max77939_5v2a_hw_setting(chip);
		chg_info("SETTING_REASON_5V2A\n");
		break;
	case SETTING_REASON_PDQC:
		max77939_pdqc_hw_setting(chip);
		chg_info("SETTING_REASON_PDQC\n");
		break;
	default:
		chg_info("do nothing\n");
		break;
	}
	return 0;
}

static bool max77939_check_cp_int_happened(struct oplus_voocphy_manager *chip,
					 bool *dump_reg, bool *send_info)
{
	int i = 0;

	if (((bidirect_int_flag[0].mask & chip->int_column_pre[1]) == 0) && bidirect_int_flag[i].mark_except) {
		*dump_reg = true;
		*send_info = true;
		memcpy(&chip->reg_dump[9], chip->int_column_pre, sizeof(chip->int_column_pre));
		chg_info("cp int happened %s\n", bidirect_int_flag[i].except_info);
		max77939_dump_registers(chip);
		return true;
	}

	for (i = 1; i < 7; i++) {
		if ((bidirect_int_flag[i].mask & chip->int_column_pre[3]) && bidirect_int_flag[i].mark_except) {
			*dump_reg = true;
			*send_info = true;
			memcpy(&chip->reg_dump[9], chip->int_column_pre, sizeof(chip->int_column_pre));
			chg_info("cp int happened %s\n", bidirect_int_flag[i].except_info);
			max77939_dump_registers(chip);
			return true;
		}
	}

	for (i = 7; i < BIDIRECT_IRQ_EVNET_NUM; i++) {
		if ((bidirect_int_flag[i].mask & chip->int_column_pre[5]) && bidirect_int_flag[i].mark_except) {
			*dump_reg = true;
			*send_info = true;
			memcpy(&chip->reg_dump[9], chip->int_column_pre, sizeof(chip->int_column_pre));
			chg_info("cp int happened %s\n", bidirect_int_flag[i].except_info);
			max77939_dump_registers(chip);
			return true;
		}
	}

	return false;
}

#define TRACK_LOCAL_T_NS_TO_S_THD		1000000000
#define TRACK_UPLOAD_COUNT_MAX			10
#define TRACK_DEVICE_ABNORMAL_UPLOAD_PERIOD	(24 * 3600)
#define REASON_LENGTH_MAX			1024
#define ERR_LENGTH_MAX				64
#define DUMP_LENGTH_MAX				512

static int max77939_track_get_local_time_s(void)
{
	int local_time_s;

	local_time_s = local_clock() / TRACK_LOCAL_T_NS_TO_S_THD;
	return local_time_s;
}

static int max77939_get_int_reg_info(struct oplus_voocphy_manager *chip,
				     char *dump_info, int len)
{
	int index = 0;

	if (!chip || !dump_info)
		return 0;

	index += snprintf(&(dump_info[index]), len - index,
			  "REG_09~REG_0E:[0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x]",
			  chip->int_column_pre[0], chip->int_column_pre[1], chip->int_column_pre[2],
			  chip->int_column_pre[3], chip->int_column_pre[4], chip->int_column_pre[5]);

	return index;
}

static int max77939_get_cp_error_type(struct oplus_voocphy_manager *chip,
				       int *err_type)
{
	int i = 0;

	if (NULL == chip || NULL == err_type) {
		chg_err("chip ir err_type is NULL\n");
		return -EINVAL;
	}

	if (((bidirect_int_flag[0].mask & chip->int_column_pre[1]) == 0) &&
	    bidirect_int_flag[i].mark_except) {
		*err_type = i + 1;
		return 0;
	}

	for (i = 1; i < 7; i++) {
		if ((bidirect_int_flag[i].mask & chip->int_column_pre[3]) &&
		    bidirect_int_flag[i].mark_except) {
			*err_type = i + 1;
			return 0;
		}
	}

	for (i = 7; i < BIDIRECT_IRQ_EVNET_NUM; i++) {
		if ((bidirect_int_flag[i].mask & chip->int_column_pre[5]) &&
		    bidirect_int_flag[i].mark_except) {
			*err_type = i + 1;
			return 0;
		}
	}

	return 1; /* not found the error type */
}

static int max77939_track_upload_cp_err_info(struct oplus_voocphy_manager *chip,
					     int err_type)
{
	int index = 0;
	int curr_time;
	static int upload_count = 0;
	static int pre_upload_time = 0;
	char temp_str[REASON_LENGTH_MAX] = {0};
	struct oplus_mms *err_topic;
	struct mms_msg *msg = NULL;
	int rc = 0;
	char err_reason[ERR_LENGTH_MAX] = {0};
	char dump_info[DUMP_LENGTH_MAX] = {0};

	if (NULL == chip) {
		chg_err("chip is NULL");
		return -EINVAL;
	}

	if (err_type <= TRACK_BIDIRECT_CP_ERR_DEFAULT) {
		chg_err("err_type = %d is invalid", err_type);
		return -EINVAL;
	}

	err_topic = oplus_mms_get_by_name("error");
	if (!err_topic) {
		chg_err("error topic not found\n");
		return -EINVAL;
	}

	curr_time = max77939_track_get_local_time_s();
	if (curr_time - pre_upload_time > TRACK_DEVICE_ABNORMAL_UPLOAD_PERIOD)
		upload_count = 0;

	chg_info("err_type = %d\n", err_type);
	if (upload_count > TRACK_UPLOAD_COUNT_MAX) {
		chg_info("cp_err_uploading upload_count = %d > max %d, should return\n",
			 upload_count, TRACK_UPLOAD_COUNT_MAX);
		return 0;
	}

	upload_count++;
	pre_upload_time = max77939_track_get_local_time_s();

	index += snprintf(&(temp_str[index]), REASON_LENGTH_MAX - index, "$$device_id@@%s", "max77939");
	index += snprintf(&(temp_str[index]), REASON_LENGTH_MAX - index, "$$err_scene@@%s",
			  OPLUS_CHG_TRACK_SCENE_BIDIRECT_CP_ERR);

	oplus_chg_track_get_bidirect_cp_err_reason(err_type, err_reason, sizeof(err_reason));
	index += snprintf(&(temp_str[index]), REASON_LENGTH_MAX - index,
			  "$$err_reason@@%s", err_reason);

	max77939_get_int_reg_info(chip, dump_info, sizeof(dump_info));
	index += snprintf(&(temp_str[index]), REASON_LENGTH_MAX - index,
			  "$$reg_info@@%s", dump_info);

	msg = oplus_mms_alloc_str_msg(MSG_TYPE_ITEM, MSG_PRIO_MEDIUM,
				      ERR_ITEM_BIDIRECT_CP_INFO, temp_str);
	if (msg == NULL) {
		chg_err("alloc msg error\n");
		return -EINVAL;
	}
	rc = oplus_mms_publish_msg_sync(err_topic, msg);
	if (rc < 0) {
		chg_err("publish msg error, rc=%d\n", rc);
		kfree(msg);
	}

	return 0;
}

static ssize_t max77939_show_registers(struct device *dev,
                                     struct device_attribute *attr, char *buf)
{
	struct oplus_voocphy_manager *chip = dev_get_drvdata(dev);
	u8 addr;
	u8 val;
	u8 tmpbuf[300];
	int len;
	int idx = 0;
	int ret;

	idx = snprintf(buf, PAGE_SIZE, "%s:\n", "max77939");
	for (addr = 0x0; addr <= 0x3C; addr++) {
		if ((addr < 0x24) || (addr > 0x2B && addr < 0x33)
		    || addr == 0x36 || addr == 0x3C) {
			ret = max77939_read_byte(chip->client, addr, &val);
			if (ret == 0) {
				len = snprintf(tmpbuf, PAGE_SIZE - idx,
				               "Reg[%.2X] = 0x%.2x\n", addr, val);
				memcpy(&buf[idx], tmpbuf, len);
				idx += len;
			}
		}
	}

	return idx;
}

static ssize_t max77939_store_register(struct device *dev,
                                     struct device_attribute *attr, const char *buf, size_t count)
{
	struct oplus_voocphy_manager *chip = dev_get_drvdata(dev);
	int ret;
	unsigned int reg;
	unsigned int val;

	ret = sscanf(buf, "%x %x", &reg, &val);
	if (ret == 2 && reg <= 0x3C)
		max77939_write_byte(chip->client, (unsigned char)reg, (unsigned char)val);

	return count;
}


static DEVICE_ATTR(registers, 0660, max77939_show_registers, max77939_store_register);

static int max77939_create_device_node(struct device *dev)
{
	int ret = 0;

	ret = device_create_file(dev, &dev_attr_registers);

	if (ret != 0)
		chg_err("create device node failed, ret = %d.", ret);

	return ret;
}


static struct of_device_id max77939_charger_match_table[] = {
	{
		.compatible = "sc,max77939-master",
	},
	{},
};

static void register_voocphy_devinfo(void)
{
#ifndef CONFIG_DISABLE_OPLUS_FUNCTION
	int ret = 0;
	char *version;
	char *manufacture;

	version = "max77939";
	manufacture = "MP";

	ret = register_device_proc("voocphy", version, manufacture);
	if (ret)
		chg_err("register_voocphy_devinfo fail\n");
#endif
}

static struct oplus_voocphy_operations oplus_max77939_ops = {
	.hardware_init		= max77939_hardware_init,
	.hw_setting		= max77939_hw_setting,
	.init_vooc		= max77939_init_vooc,
	.set_predata		= max77939_set_predata,
	.set_txbuff		= max77939_set_txbuff,
	.get_adapter_info	= max77939_get_adapter_info,
	.update_data		= max77939_update_data,
	.get_chg_enable		= max77939_get_chg_enable,
	.set_chg_enable		= max77939_set_chg_enable,
	.reset_voocphy		= max77939_reset_voocphy,
	.reactive_voocphy	= max77939_reactive_voocphy,
	.send_handshake		= max77939_send_handshake,
	.get_cp_vbat		= max77939_get_cp_vbat,
	.get_int_value		= max77939_get_int_value,
	.get_adc_enable		= max77939_get_adc_enable,
	.set_adc_enable		= max77939_set_adc_enable,
	.get_ichg		= max77939_get_cp_ichg,
	.set_pd_svooc_config	= max77939_set_pd_svooc_config,
	.get_pd_svooc_config	= max77939_get_pd_svooc_config,
	.get_vbus_status	= max77939_get_vbus_status,
	.set_chg_auto_mode	= max77939_set_chg_auto_mode,
	.get_voocphy_enable	= max77939_get_voocphy_enable,
	.dump_voocphy_reg	= max77939_dump_reg_in_err_issue,
	.check_cp_int_happened	= max77939_check_cp_int_happened,
	.upload_cp_error	= max77939_track_upload_cp_err_info,
	.get_cp_error_type	= max77939_get_cp_error_type,
};

static irqreturn_t max77939_interrupt_handler(int irq, void *dev_id)
{
	struct oplus_voocphy_manager *voocphy = dev_id;
	struct max77939_device *chip = voocphy->priv_data;

	if (chip && chip->voocphy_enable)
		return oplus_voocphy_interrupt_handler(voocphy);

	return IRQ_HANDLED;
}

static int max77939_irq_gpio_init(struct oplus_voocphy_manager *chip)
{
	int rc;
	struct device_node *node = chip->dev->of_node;

	chip->irq_gpio = of_get_named_gpio(node, "oplus,irq_gpio", 0);
	if (!gpio_is_valid(chip->irq_gpio)) {
		chip->irq_gpio = of_get_named_gpio(node, "oplus_spec,irq_gpio", 0);
		if (!gpio_is_valid(chip->irq_gpio)) {
			chg_err("irq_gpio not specified, rc=%d\n", chip->irq_gpio);
			return chip->irq_gpio;
		}
	}
	rc = gpio_request(chip->irq_gpio, "irq_gpio");
	if (rc) {
		chg_err("unable to request gpio[%d]\n", chip->irq_gpio);
		return rc;
	}
	chg_info("irq_gpio = %d\n", chip->irq_gpio);

	chip->irq = gpio_to_irq(chip->irq_gpio);
	chip->pinctrl = devm_pinctrl_get(chip->dev);
	if (IS_ERR_OR_NULL(chip->pinctrl)) {
		chg_err("get pinctrl fail\n");
		rc = -EINVAL;
		goto pinctrl_err;
	}

	chip->charging_inter_active =
	    pinctrl_lookup_state(chip->pinctrl, "charging_inter_active");
	if (IS_ERR_OR_NULL(chip->charging_inter_active)) {
		chg_err("failed to get the pinctrl state(%d)\n", __LINE__);
		rc = -EINVAL;
		goto pinctrl_err;
	}

	chip->charging_inter_sleep =
	    pinctrl_lookup_state(chip->pinctrl, "charging_inter_sleep");
	if (IS_ERR_OR_NULL(chip->charging_inter_sleep)) {
		chg_err("Failed to get the pinctrl state(%d)\n", __LINE__);
		rc = -EINVAL;
		goto pinctrl_err;
	}

	gpio_direction_input(chip->irq_gpio);
	pinctrl_select_state(chip->pinctrl, chip->charging_inter_active); /* no_PULL */
	rc = gpio_get_value(chip->irq_gpio);
	chg_info("irq_gpio = %d, irq_gpio_stat = %d\n", chip->irq_gpio, rc);

	return 0;

pinctrl_err:
        gpio_free(chip->irq_gpio);
	return rc;
}

static int max77939_irq_register(struct max77939_device *chip)
{
	struct oplus_voocphy_manager *voocphy = chip->voocphy;
	struct irq_desc *desc;
	struct cpumask current_mask;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
	cpumask_var_t cpu_highcap_mask;
#endif
	int ret;

	ret = max77939_irq_gpio_init(voocphy);
	if (ret < 0) {
		chg_err("failed to irq gpio init(%d)\n", ret);
		return ret;
	}

	if (voocphy->irq) {
		ret = request_threaded_irq(voocphy->irq, NULL,
					   max77939_interrupt_handler,
					   IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
					   "voocphy_irq", voocphy);
		if (ret < 0) {
			chg_err("request irq for irq=%d failed, ret =%d\n",
				voocphy->irq, ret);
			return ret;
		}
		enable_irq_wake(voocphy->irq);
		chg_debug("request irq ok\n");
	}

	desc = irq_to_desc(voocphy->irq);
	if (desc == NULL) {
		free_irq(voocphy->irq, voocphy);
		chg_err("desc null\n");
		return ret;
	}
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
	update_highcap_mask(cpu_highcap_mask);
	cpumask_and(&current_mask, cpu_online_mask, cpu_highcap_mask);
#else
	cpumask_setall(&current_mask);
	cpumask_and(&current_mask, cpu_online_mask, &current_mask);
#endif
	ret = set_cpus_allowed_ptr(desc->action->thread, &current_mask);

	return 0;
}

static int max77939_cp_init(struct oplus_chg_ic_dev *ic_dev)
{
	u8 int_column[7] = { 0 };
	int ret = 0;
	int max_count = 3;
	struct max77939_device *chip = NULL;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	chip = oplus_chg_ic_get_priv_data(ic_dev);
	if (chip == NULL) {
		chg_err("oplus_chg_ic_dev chip is NULL");
		return -ENODEV;
	}

	if (chip->voocphy == NULL) {
		chg_err("voocphy is null");
		return -ENODEV;
	}

	/* check if the ic is ok by read register */
	while (max_count--) {
		ret = max77939_read_i2c_block(chip->voocphy->client, MAX77939_REG_00, 7, int_column);
		if (ret < 0) {
			chg_err("count = %d read MAX77939_REG_00 7 bytes failed(%d)\n", max_count, ret);
			continue;
		} else {
			break;
		}
	}

	if (ret < 0) {
		chg_err("read MAX77939_REG_00 7 bytes failed(%d)\n", ret);
		ic_dev->online = false;
		oplus_chg_ic_virq_trigger(ic_dev, OPLUS_IC_VIRQ_OFFLINE);
		return ret;
	} else {
		ic_dev->online = true;
		oplus_chg_ic_virq_trigger(ic_dev, OPLUS_IC_VIRQ_ONLINE);
	}

	return 0;
}

static int max77939_cp_exit(struct oplus_chg_ic_dev *ic_dev)
{
	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	ic_dev->online = false;
	oplus_chg_ic_virq_trigger(ic_dev, OPLUS_IC_VIRQ_OFFLINE);

	return 0;
}

static int max77939_cp_reg_dump(struct oplus_chg_ic_dev *ic_dev)
{
	struct max77939_device *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_priv_data(ic_dev);
	if (chip == NULL) {
		chg_err("chip is NULL");
		return -EINVAL;
	}

	max77939_dump_reg_in_err_issue(chip->voocphy);
	return 0;
}

static int max77939_cp_smt_test(struct oplus_chg_ic_dev *ic_dev, char buf[], int len)
{
	return 0;
}

static int max77939_cp_enable(struct oplus_chg_ic_dev *ic_dev, bool en)
{
	return 0;
}

static int max77939_cp_hw_init(struct oplus_chg_ic_dev *ic_dev)
{
	struct max77939_device *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	chip = oplus_chg_ic_get_priv_data(ic_dev);
	if (chip == NULL) {
		chg_err("chip is NULL");
		return -EINVAL;
	}

	if (chip->rested)
		return 0;

	max77939_hardware_init(chip->voocphy);
	return 0;
}

static int max77939_cp_set_work_mode(struct oplus_chg_ic_dev *ic_dev, enum oplus_cp_work_mode mode)
{
	struct max77939_device *chip = NULL;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	chip = oplus_chg_ic_get_priv_data(ic_dev);
	if (chip == NULL) {
		chg_err("chip is NULL");
		return -EINVAL;
	}

	if (!max77939_check_work_mode_support(mode)) {
		chg_err("mode %d is not support!", mode);
		return -EINVAL;
	}

	chip->cp_work_mode = mode;
	rc = max77939_svooc_hw_setting(chip->voocphy);
	if (rc < 0)
		chg_err("set work mode to %d error\n", mode);

	return rc;
}

static int max77939_cp_get_work_mode(struct oplus_chg_ic_dev *ic_dev, enum oplus_cp_work_mode *mode)
{
	struct max77939_device *chip = NULL;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	chip = oplus_chg_ic_get_priv_data(ic_dev);

	if (chip == NULL) {
		chg_err("chip is NULL");
		return -EINVAL;
	}
	*mode = chip->cp_work_mode;

	return 0;
}

static int max77939_cp_check_work_mode_support(struct oplus_chg_ic_dev *ic_dev, enum oplus_cp_work_mode mode)
{
	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	return max77939_check_work_mode_support(mode);
}

static int max77939_cp_set_iin(struct oplus_chg_ic_dev *ic_dev, int iin)
{
	return 0;
}

static int max77939_cp_get_vin(struct oplus_chg_ic_dev *ic_dev, int *vin)
{
	struct max77939_device *chip = NULL;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	chip = oplus_chg_ic_get_priv_data(ic_dev);
	if (chip == NULL) {
		chg_err("chip is NULL");
		return -EINVAL;
	}

	rc = max77939_cp_vbus(chip->voocphy);
	if (rc < 0) {
		chg_err("can't get cp vin, rc=%d\n", rc);
		return rc;
	}
	*vin = rc;

	return 0;
}

static int max77939_cp_get_iin(struct oplus_chg_ic_dev *ic_dev, int *iin)
{
	struct max77939_device *chip = NULL;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_priv_data(ic_dev);
	if (chip == NULL) {
		chg_err("chip is NULL");
		return -EINVAL;
	}

	rc = max77939_get_cp_ichg(chip->voocphy);
	if (rc < 0) {
		chg_err("can't get cp iin, rc=%d\n", rc);
		return rc;
	}
	*iin = rc;

	return 0;
}

static int max77939_cp_get_vout(struct oplus_chg_ic_dev *ic_dev, int *vout)
{
	struct max77939_device *chip = NULL;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_priv_data(ic_dev);
	if (chip == NULL) {
		chg_err("chip is NULL");
		return -EINVAL;
	}

	rc = max77939_get_cp_vbat(chip->voocphy);
	if (rc < 0) {
		chg_err("can't get cp vout, rc=%d\n", rc);
		return rc;
	}
	*vout = rc;

	return 0;
}

static int max77939_cp_get_iout(struct oplus_chg_ic_dev *ic_dev, int *iout)
{
	struct max77939_device *chip = NULL;
	int iin;
	bool working;
	enum oplus_cp_work_mode work_mode;
	int rc;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	chip = oplus_chg_ic_get_priv_data(ic_dev);
	if (NULL == chip) {
		chg_err("chip is NULL");
		return -EINVAL;
	}

	/*
	 * There is an exception in the iout adc of sc8537a, which is obtained
	 * indirectly through iin
	 */
	rc = oplus_chg_ic_func(ic_dev, OPLUS_IC_FUNC_CP_GET_WORK_STATUS, &working);
	if (rc < 0)
		return rc;
	if (!working) {
		*iout = 0;
		return 0;
	}
	rc = oplus_chg_ic_func(ic_dev, OPLUS_IC_FUNC_CP_GET_IIN, &iin);
	if (rc < 0)
		return rc;
	rc = oplus_chg_ic_func(ic_dev, OPLUS_IC_FUNC_CP_GET_WORK_MODE, &work_mode);
	if (rc < 0)
		return rc;

	switch (work_mode) {
	case CP_WORK_MODE_BYPASS:
		*iout = iin;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int max77939_cp_set_work_start(struct oplus_chg_ic_dev *ic_dev, bool start)
{
	struct max77939_device *chip = NULL;
	int rc = 0;
	u8 data = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	chip = oplus_chg_ic_get_priv_data(ic_dev);
	if (NULL == chip || NULL == chip->voocphy) {
		chg_err("chip or voocphy is NULL");
		return -EINVAL;
	}

	if (start)
		rc = max77939_write_byte(chip->client, MAX77939_REG_04, 0x36); /* WD:1000ms */
	else
		rc = max77939_write_byte(chip->client, MAX77939_REG_04, 0x06); /* dsiable wdt */

	if (rc < 0) {
		chg_err("wdg config failed.");
		return rc;
	}

	max77939_read_byte(chip->voocphy->client, MAX77939_REG_02, &data);
	chg_info("%s work %s, data = 0x%x\n", chip->dev->of_node->name, start ? "start" : "stop", data);
	if (start && (0 == (data & MAX77939_CHG_EN_MASK)))
		rc |= max77939_set_chg_enable(chip->voocphy, start);
	else if (!start)
		rc |= max77939_set_chg_enable(chip->voocphy, start);
	if (rc < 0)
		return rc;

	return 0;
}

static int max77939_cp_get_work_status(struct oplus_chg_ic_dev *ic_dev, bool *start)
{
	struct max77939_device *chip = NULL;
	u8 data = 0;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_priv_data(ic_dev);

	rc = max77939_get_chg_enable(chip->voocphy, &data);
	if (rc < 0) {
		chg_err("read MAX77939_chg_enable error, rc=%d\n", rc);
		return rc;
	}

	*start = data & 1;

	return 0;
}

static void *max77939_cp_get_func(struct oplus_chg_ic_dev *ic_dev, enum oplus_chg_ic_func func_id)
{
	void *func = NULL;

	if (!ic_dev->online && (func_id != OPLUS_IC_FUNC_INIT) &&
	    (func_id != OPLUS_IC_FUNC_EXIT)) {
		chg_err("%s is offline\n", ic_dev->name);
		return NULL;
	}

	if (!oplus_chg_ic_func_is_support(ic_dev, func_id)) {
		chg_info("%s: this func(=%d) is not supported\n", ic_dev->name, func_id);
		return NULL;
	}

	switch (func_id) {
	case OPLUS_IC_FUNC_INIT:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_INIT, max77939_cp_init);
		break;
	case OPLUS_IC_FUNC_EXIT:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_EXIT, max77939_cp_exit);
		break;
	case OPLUS_IC_FUNC_REG_DUMP:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_REG_DUMP, max77939_cp_reg_dump);
		break;
	case OPLUS_IC_FUNC_SMT_TEST:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_SMT_TEST, max77939_cp_smt_test);
		break;
	case OPLUS_IC_FUNC_CP_ENABLE:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_CP_ENABLE, max77939_cp_enable);
		break;
	case OPLUS_IC_FUNC_CP_HW_INTI:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_CP_HW_INTI, max77939_cp_hw_init);
		break;
	case OPLUS_IC_FUNC_CP_SET_WORK_MODE:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_CP_SET_WORK_MODE, max77939_cp_set_work_mode);
		break;
	case OPLUS_IC_FUNC_CP_GET_WORK_MODE:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_CP_GET_WORK_MODE, max77939_cp_get_work_mode);
		break;
	case OPLUS_IC_FUNC_CP_CHECK_WORK_MODE_SUPPORT:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_CP_CHECK_WORK_MODE_SUPPORT,
			max77939_cp_check_work_mode_support);
		break;
	case OPLUS_IC_FUNC_CP_SET_IIN:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_CP_SET_IIN, max77939_cp_set_iin);
		break;
	case OPLUS_IC_FUNC_CP_GET_VIN:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_CP_GET_VIN, max77939_cp_get_vin);
		break;
	case OPLUS_IC_FUNC_CP_GET_IIN:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_CP_GET_IIN, max77939_cp_get_iin);
		break;
	case OPLUS_IC_FUNC_CP_GET_VOUT:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_CP_GET_VOUT, max77939_cp_get_vout);
		break;
	case OPLUS_IC_FUNC_CP_GET_IOUT:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_CP_GET_IOUT, max77939_cp_get_iout);
		break;
	case OPLUS_IC_FUNC_CP_SET_WORK_START:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_CP_SET_WORK_START, max77939_cp_set_work_start);
		break;
	case OPLUS_IC_FUNC_CP_GET_WORK_STATUS:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_CP_GET_WORK_STATUS, max77939_cp_get_work_status);
		break;
	default:
		chg_err("this func(=%d) is not supported\n", func_id);
		func = NULL;
		break;
	}

	return func;
}

struct oplus_chg_ic_virq max77939_cp_virq_table[] = {
	{ .virq_id = OPLUS_IC_VIRQ_ERR },
	{ .virq_id = OPLUS_IC_VIRQ_ONLINE },
	{ .virq_id = OPLUS_IC_VIRQ_OFFLINE },
};

static int max77939_ic_register(struct max77939_device *chip)
{
	enum oplus_chg_ic_type ic_type;
	int ic_index;
	struct device_node *child;
	struct oplus_chg_ic_dev *ic_dev = NULL;
	struct oplus_chg_ic_cfg ic_cfg;
	int rc;

	for_each_child_of_node(chip->dev->of_node, child) {
		rc = of_property_read_u32(child, "oplus,ic_type", &ic_type);
		if (rc < 0)
			continue;
		rc = of_property_read_u32(child, "oplus,ic_index", &ic_index);
		if (rc < 0)
			continue;
		ic_cfg.name = child->name;
		ic_cfg.index = ic_index;
		ic_cfg.type = ic_type;
		ic_cfg.priv_data = chip;
		ic_cfg.of_node = child;
		switch (ic_type) {
		case OPLUS_CHG_IC_CP:
			/* (void)max77939_init_imp_node(chip, child); */
			snprintf(ic_cfg.manu_name, OPLUS_CHG_IC_MANU_NAME_MAX - 1, "cp-max77939:%d", ic_index);
			snprintf(ic_cfg.fw_id, OPLUS_CHG_IC_FW_ID_MAX - 1, "0x00");
			ic_cfg.get_func = max77939_cp_get_func;
			ic_cfg.virq_data = max77939_cp_virq_table;
			ic_cfg.virq_num = ARRAY_SIZE(max77939_cp_virq_table);
			break;
		default:
			chg_err("not support ic_type(=%d)\n", ic_type);
			continue;
		}
		ic_dev = devm_oplus_chg_ic_register(chip->dev, &ic_cfg);
		if (!ic_dev) {
			rc = -ENODEV;
			chg_err("register %s error\n", child->name);
			continue;
		}
		chg_info("register %s\n", child->name);

		switch (ic_dev->type) {
		case OPLUS_CHG_IC_CP:
			chip->cp_work_mode = CP_WORK_MODE_UNKNOWN;
			chip->cp_ic = ic_dev;
			break;
		default:
			chg_err("not support ic_type(=%d)\n", ic_dev->type);
			continue;
		}

		of_platform_populate(child, NULL, NULL, chip->dev);
	}

	return 0;
}

static bool max77939_check_device_is_exist(struct i2c_client *client)
{
	u8 reg[2] = { 0 };
	int max_count = 5;
	int ret = -1;

	if (NULL == client) {
		chg_err("client is null");
		return false;
	}

	/* check if the ic is ok by read register */
	while (max_count--) {
		ret = max77939_read_i2c_nonblock(client, 0x1c, 2, reg);
		if (ret < 0) {
			chg_err("count = %d read REG_1C 2 bytes failed(%d)\n", max_count, ret);
			msleep(10);
			continue;
		} else {
			break;
		}
	}

	if (ret >= 0) {
		chg_info(" CHIP_REV:0x%x, OTP_REV:0x%x", reg[0], reg[1]);
		return true;
	} else {
		chg_err("max77939 device maybe not exist, ret = %d!", ret);
		return false;
	}
}

static int max77939_charger_probe(struct i2c_client *client,
                                const struct i2c_device_id *id)
{
	int ret;
	struct max77939_device *chip;
	struct oplus_voocphy_manager *voocphy;

	chg_info("start\n");
	if (!max77939_check_device_is_exist(client)) {
		chg_err("the device max77939 is not exist, not probe it!");
		return 0;
	}

	chip = devm_kzalloc(&client->dev, sizeof(struct max77939_device), GFP_KERNEL);
	if (!chip) {
		dev_err(&client->dev, "alloc max77939 device buf error\n");
		return -ENOMEM;
	}

	voocphy = devm_kzalloc(&client->dev, sizeof(struct oplus_voocphy_manager), GFP_KERNEL);
	if (voocphy == NULL) {
		chg_err("alloc voocphy buf error\n");
		ret = -ENOMEM;
		goto chg_err;
	}

	chip->client = client;
	chip->dev = &client->dev;
	voocphy->client = client;
	voocphy->dev = &client->dev;
	voocphy->priv_data = chip;
	chip->voocphy = voocphy;
	mutex_init(&chip->i2c_rw_lock);
	mutex_init(&chip->chip_lock);
	i2c_set_clientdata(client, voocphy);

	max77939_create_device_node(&(client->dev));
	voocphy->ops = &oplus_max77939_ops;
	ret = oplus_register_voocphy(voocphy);
	if (ret < 0) {
		chg_err("failed to register voocphy, ret = %d", ret);
		goto reg_voocphy_err;
	}
	ret = max77939_irq_register(chip);
	if (ret < 0) {
		chg_err("irq register error, rc=%d\n", ret);
		goto reg_irq_err;
	}

	ret = max77939_ic_register(chip);
	if (ret < 0) {
		chg_err("cp ic register error\n");
		goto cp_reg_err;
	}

	ret = max77939_cp_init(chip->cp_ic);
	if (ret < 0) {
		chg_err("cp ic init error\n");
		goto cp_init_err;
	}

	max77939_dump_registers(voocphy);
	register_voocphy_devinfo();
	chip->voocphy_enable = false;
	INIT_DELAYED_WORK(&chip->set_automode_work, max77939_set_automode_work);
	oplus_mms_wait_topic("common", oplus_max77939_subscribe_comm_topic, chip);

	chg_info("max77939(%s) probe successfully\n", chip->dev->of_node->name);

	return 0;

cp_init_err:
cp_reg_err:
	free_irq(voocphy->irq, voocphy);
reg_irq_err:
	gpio_free(voocphy->irq_gpio);
reg_voocphy_err:
	devm_kfree(&client->dev, voocphy);
chg_err:
	devm_kfree(&client->dev, chip);
	return ret;
}

static void max77939_charger_shutdown(struct i2c_client *client)
{
	max77939_update_bits(client, MAX77939_REG_06, MAX77939_REG_RESET_MASK,
			   MAX77939_RESET_REG << MAX77939_REG_RESET_SHIFT);
	msleep(10);
	/* disable v1x_scp */
	max77939_write_byte(client, MAX77939_REG_01, 0x5e);
	/* REF_SKIP_R 40mv */
	max77939_write_byte(client, MAX77939_REG_08, 0xA6);
	/* Masked Pulse_filtered, RX_Start,Tx_Done,bit soft intflag */
	max77939_write_byte(client, MAX77939_REG_29, 0x05);

	return;
}

static const struct i2c_device_id max77939_charger_id[] = {
	{"max77939-master", 0},
	{},
};

static struct i2c_driver max77939_charger_driver = {
	.driver		= {
		.name	= "max77939-charger",
		.owner	= THIS_MODULE,
		.of_match_table = max77939_charger_match_table,
	},
	.id_table	= max77939_charger_id,

	.probe		= max77939_charger_probe,
	.shutdown	= max77939_charger_shutdown,
};

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0))
static int __init max77939_subsys_init(void)
{
	int ret = 0;
	chg_debug(" init start\n");

	if (i2c_add_driver(&max77939_charger_driver) != 0)
		chg_err(" failed to register max77939 i2c driver.\n");
	else
		chg_debug(" Success to register max77939 i2c driver.\n");

	return ret;
}

subsys_initcall(max77939_subsys_init);
#else
static int max77939_subsys_init(void)
{
	int ret = 0;
	chg_debug(" init start\n");

	if (i2c_add_driver(&max77939_charger_driver) != 0)
		chg_err(" failed to register max77939 i2c driver.\n");
	else
		chg_debug(" Success to register max77939 i2c driver.\n");

	return ret;
}

static void max77939_subsys_exit(void)
{
	i2c_del_driver(&max77939_charger_driver);
}
oplus_chg_module_register(max77939_subsys);
#endif /*LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0)*/

MODULE_DESCRIPTION("MAX77939 Charge Pump Driver");
MODULE_LICENSE("GPL v2");

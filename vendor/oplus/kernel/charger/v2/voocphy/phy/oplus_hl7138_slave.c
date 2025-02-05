// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-2022 Opus. All rights reserved.
 */

#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/of_irq.h>
#include <linux/module.h>
#include <linux/power_supply.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/err.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/machine.h>
#include <linux/debugfs.h>
#include <linux/bitops.h>
#include <linux/math64.h>
#include <linux/proc_fs.h>
#include <trace/events/sched.h>
#include<linux/ktime.h>
#include <linux/pm_qos.h>

#include <oplus_chg_ic.h>
#include <oplus_chg_module.h>
#include <oplus_chg.h>
#include "../oplus_voocphy.h"
#include "oplus_hl7138.h"
#include <oplus_impedance_check.h>
#include <oplus_mms.h>
#include <oplus_mms_gauge.h>

static struct oplus_voocphy_manager *oplus_voocphy_mg = NULL;
static struct mutex i2c_rw_lock;
static int hl7138_slave_get_chg_enable(struct oplus_voocphy_manager *chip, u8 *data);

#define DEFUALT_VBUS_LOW 100
#define DEFUALT_VBUS_HIGH 200
#define HL7138_DEVICE_ID 0x03
#define HL7138_SVOOC_IBUS_FACTOR 	110/100
#define HL7138_VOOC_IBUS_FACTOR 	215/100
#define HL7138_FACTOR_125_100		125/100
#define HL7138_FACTOR_400_100		400/100
#define HL7138_REG_NUM_2         2
#define HL7138_REG_NUM_12        12
#define HL7138_REG_ADC_H_BIT_VBUS 0
#define HL7138_REG_ADC_L_BIT_VBUS 1
#define HL7138_REG_ADC_BIT_OFFSET_4 4
#define HL7138_REG_ADC_H_BIT_ICHG 0
#define HL7138_REG_ADC_L_BIT_ICHG 1
#define HL7138_SVOOC_STATUS_OK 2
#define HL7138_VOOC_STATUS_OK 3
#define HL7138_CHG_STATUS_EN 1
#define HL7138_CHG_EN (HL7138_CHG_ENABLE << HL7138_CHG_EN_SHIFT)                    /* 1 << 7   0x80 */
#define HL7138_CHG_DIS (HL7138_CHG_DISABLE << HL7138_CHG_EN_SHIFT)                  /* 0 << 7   0x00 */
#define HL7138_IBUS_UCP_EN (HL7138_IBUS_UCP_ENABLE << HL7138_IBUS_UCP_DIS_SHIFT)    /* 1 << 2   0x04 */
#define HL7138_IBUS_UCP_DIS (HL7138_IBUS_UCP_DISABLE << HL7138_IBUS_UCP_DIS_SHIFT)  /* 0 << 2   0x00 */
#define HL7138_IBUS_UCP_DEFAULT  0x01

struct hl7138_slave_device {
	struct device *slave_dev;
	struct i2c_client *slave_client;
	struct oplus_voocphy_manager *voocphy;
	struct oplus_chg_ic_dev *cp_ic;
	struct oplus_impedance_node *input_imp_node;
	struct oplus_impedance_node *output_imp_node;

	enum oplus_cp_work_mode cp_work_mode;
	bool rested;
	bool vac_support;
};

static enum oplus_cp_work_mode g_cp_support_work_mode[] = {
	CP_WORK_MODE_BYPASS,
	CP_WORK_MODE_2_TO_1,
};


static int __hl7138_slave_read_byte(struct i2c_client *client, u8 reg, u8 *data)
{
	s32 ret;

	ret = i2c_smbus_read_byte_data(client, reg);
	if (ret < 0) {
		chg_err("i2c read fail: can't read from reg 0x%02X\n", reg);
		return ret;
	}
	*data = (u8) ret;

	return 0;
}

static int __hl7138_slave_write_byte(struct i2c_client *client, int reg, u8 val)
{
	s32 ret;

	ret = i2c_smbus_write_byte_data(client, reg, val);
	if (ret < 0) {
		chg_err("i2c write fail: can't write 0x%02X to reg 0x%02X: %d\n", val, reg, ret);
		return ret;
	}

	return 0;
}

static int hl7138_slave_update_bits(struct i2c_client *client, u8 reg,
                              u8 mask, u8 data)
{
	int ret;
	u8 tmp;

	mutex_lock(&i2c_rw_lock);
	ret = __hl7138_slave_read_byte(client, reg, &tmp);
	if (ret) {
		chg_err("Failed: reg=%02X, ret=%d\n", reg, ret);
		goto out;
	}

	tmp &= ~mask;
	tmp |= data & mask;

	ret = __hl7138_slave_write_byte(client, reg, tmp);
	if (ret)
		chg_err("Failed: reg=%02X, ret=%d\n", reg, ret);
out:
	mutex_unlock(&i2c_rw_lock);
	return ret;
}

static int hl7138_slave_read_byte(struct i2c_client *client, u8 reg, u8 *data)
{
	int ret;

	mutex_lock(&i2c_rw_lock);
	ret = __hl7138_slave_read_byte(client, reg, data);
	mutex_unlock(&i2c_rw_lock);

	return ret;
}

static int hl7138_slave_write_byte(struct i2c_client *client, u8 reg, u8 data)
{
	int ret;

	mutex_lock(&i2c_rw_lock);
	ret = __hl7138_slave_write_byte(client, reg, data);
	mutex_unlock(&i2c_rw_lock);

	return ret;
}

static void hl7138_slave_update_data(struct oplus_voocphy_manager *chip)
{
	u8 data_block[HL7138_REG_NUM_2] = {0};
	u8 int_flag = 0;
	int data;
	int ret = 0;

	data = hl7138_slave_read_byte(chip->slave_client, HL7138_REG_01,&int_flag);
	if (data < 0) {
		chg_err("hl7138_slave_read_byte faile\n");
		return;
	}

	/* parse data_block for improving time of interrupt
	 * VIN,IIN,VBAT,IBAT,VTS,VOUT,VDIE,;
	 */
	ret = i2c_smbus_read_i2c_block_data(chip->slave_client, HL7138_REG_44,
			HL7138_REG_NUM_2, data_block);
	if (chip->adapter_type == ADAPTER_SVOOC) {
		chip->slave_cp_ichg = ((data_block[HL7138_REG_ADC_H_BIT_ICHG] << HL7138_REG_ADC_BIT_OFFSET_4)
			+ data_block[HL7138_REG_ADC_L_BIT_ICHG]) * HL7138_SVOOC_IBUS_FACTOR;	/* Iin_lbs=1.10mA@CP; */
	} else {
		chip->slave_cp_ichg = ((data_block[HL7138_REG_ADC_H_BIT_ICHG] << HL7138_REG_ADC_BIT_OFFSET_4)
			+ data_block[HL7138_REG_ADC_L_BIT_ICHG]) * HL7138_VOOC_IBUS_FACTOR;	/* Iin_lbs=2.15mA@BP; */
	}

	chg_info("slave_cp_ichg1 = %d int_flag = %d", chip->slave_cp_ichg, int_flag);
}

/*********************************************************************/
static int hl7138_slave_get_chg_enable(struct oplus_voocphy_manager *chip, u8 *data)
{
	int ret = 0;
	if (!chip) {
		chg_err("Failed\n");
		return -1;
	}
	ret = hl7138_slave_read_byte(chip->slave_client, HL7138_REG_12, data);
	if (ret < 0) {
		chg_err("HL7138_REG_12\n");
		return -1;
	}
	*data = *data >> HL7138_CHG_EN_SHIFT;

	return ret;
}

int hl7138_slave_get_ichg(struct oplus_voocphy_manager *chip)
{
	u8 data_block[HL7138_REG_NUM_2] = {0};
	u8 slave_cp_enable = 0;

	hl7138_slave_get_chg_enable(chip, &slave_cp_enable);

	if(slave_cp_enable == 0)
		return 0;
	/*parse data_block for improving time of interrupt*/
	i2c_smbus_read_i2c_block_data(chip->slave_client, HL7138_REG_44, HL7138_REG_NUM_2, data_block);

	if (chip->adapter_type == ADAPTER_SVOOC)
		chip->slave_cp_ichg = ((data_block[0] << HL7138_REG_ADC_BIT_OFFSET_4) | data_block[1]) * HL7138_SVOOC_IBUS_FACTOR;	/* Iin_lbs=1.10mA@CP; */
	else
		chip->slave_cp_ichg = ((data_block[0] << HL7138_REG_ADC_BIT_OFFSET_4) | data_block[1]) * HL7138_VOOC_IBUS_FACTOR;	/* Iin_lbs=2.15mA@BP; */

	chg_info("chip->slave_cp_ichg = %d\n", chip->slave_cp_ichg);

	return chip->slave_cp_ichg;
}

static int hl7138_slave_get_cp_status(struct oplus_voocphy_manager *chip)
{
	u8 data_reg03, data_reg12;
	int ret_reg03, ret_reg12;

	if (!chip) {
		chg_err("Failed\n");
		return 0;
	}

	ret_reg03 = hl7138_slave_read_byte(chip->slave_client, HL7138_REG_03, &data_reg03);
	ret_reg12 = hl7138_slave_read_byte(chip->slave_client, HL7138_REG_12, &data_reg12);
	data_reg03 = data_reg03 >> 6;
	data_reg12 = data_reg12 >> 7;

	if ((data_reg03 == HL7138_SVOOC_STATUS_OK || data_reg03 == HL7138_VOOC_STATUS_OK)
		&& data_reg12 == HL7138_CHG_STATUS_EN) {
		return 1;
	} else {
		chg_err("%s reg03 = %d data_reg12 = %d\n",__func__, data_reg03, data_reg12);
		return 0;
	}
}

static int hl7138_slave_set_chg_enable(struct oplus_voocphy_manager *chip, bool enable)
{
	if (!chip) {
		chg_err("Failed\n");
		return -1;
	}
	chg_info("%s enable %d \n",__func__, enable);
	if (enable) {
		return hl7138_slave_write_byte(chip->slave_client,
			HL7138_REG_12, HL7138_CHG_EN | HL7138_IBUS_UCP_EN | HL7138_IBUS_UCP_DEFAULT);  /* is not pdsvooc adapter: enable ucp */
	} else {
		return hl7138_slave_write_byte(chip->slave_client,
			HL7138_REG_12, HL7138_CHG_DIS | HL7138_IBUS_UCP_EN | HL7138_IBUS_UCP_DEFAULT);      /* chg disable */
	}
}

static int hl7138_slave_init_device(struct oplus_voocphy_manager *chip)
{
	u8 reg_data;

	hl7138_slave_write_byte(chip->slave_client, HL7138_REG_40, 0x05);	/* ADC_CTRL:disable */
	hl7138_slave_write_byte(chip->slave_client, HL7138_REG_0B, 0x88);	/* VBUS_OVP=7V,JL:02->0B; */
	hl7138_slave_write_byte(chip->slave_client, HL7138_REG_0C, 0x0F);	//VBUS_OVP:10.2 2:1 or 1:1V,JL:04-0C; Modify by JL-2023;
	reg_data = chip->reg_ctrl_1;
	hl7138_slave_write_byte(chip->slave_client, HL7138_REG_11, reg_data);	/* ovp:90mV */
	reg_data = chip->ovp_reg;
	hl7138_slave_write_byte(chip->slave_client, HL7138_REG_08, reg_data);	/* VBAT_OVP:4.56	4.56+0.09*/
	reg_data = chip->ocp_reg;
	hl7138_slave_write_byte(chip->slave_client, HL7138_REG_0E, reg_data);	/* IBUS_OCP:3.5A      ocp:100mA */
	hl7138_slave_write_byte(chip->slave_client, HL7138_REG_0F, 0x60);	/* IBUS_OCP:3.5A      ocp:250mA */
	hl7138_slave_write_byte(chip->slave_client, HL7138_REG_02, 0xE1);	/* mask all INT_FLAG */
	hl7138_slave_write_byte(chip->slave_client, HL7138_REG_10, 0xFC);	/* Dis IIN_REG; */
	hl7138_slave_write_byte(chip->slave_client, HL7138_REG_12, 0x05);	/* Fsw=500KHz;07->12; */
	hl7138_slave_write_byte(chip->slave_client, HL7138_REG_14, 0x08);	/* dis WDG; */
	hl7138_slave_write_byte(chip->slave_client, HL7138_REG_16, 0x3C);	/* OV=500, UV=250 */

	return 0;
}

static int hl7138_slave_work_mode_lockcheck(struct oplus_voocphy_manager *chip)
{
	unsigned char reg;

	if (!chip)
		return -1;

	if (!hl7138_slave_read_byte(chip->slave_client, HL7138_REG_A7, &reg) && reg != 0x4) {
		/*test mode unlock & lock avoid burnning out the chip*/
		hl7138_slave_write_byte(chip->slave_client, HL7138_REG_A0, 0xF9);
		hl7138_slave_write_byte(chip->slave_client, HL7138_REG_A0, 0x9F);	/* Unlock test register */
		hl7138_slave_write_byte(chip->slave_client, HL7138_REG_A7, 0x04);
		hl7138_slave_write_byte(chip->slave_client, HL7138_REG_A0, 0x00);	/* Lock test register */
		chg_info("hl7138_work_mode_lockcheck done\n");
	}
	return 0;
}

static bool hl7138_check_slave_hw_ba_version(struct oplus_voocphy_manager *chip)
{
	int ret;
	u8 val;

	ret = hl7138_slave_read_byte(chip->slave_client, HL7138_REG_00, &val);
	if (ret < 0) {
		chg_err("read hl7138 slave reg0 error\n");
		return false;
	}

	if (val == HL7138_BA_VERSION)
		return true;
	else
		return false;
}

/* turn off system clk for BA version only */
static int hl7138_slave_turnoff_sys_clk(struct oplus_voocphy_manager *chip)
{
	u8 reg_data[2] = {0};
	int retry = 0;

	if (!chip) {
		chg_err("slave turn off sys clk failed\n");
		return -1;
	}

	do {
		hl7138_slave_write_byte(chip->slave_client, HL7138_REG_A0, 0xF9);
		hl7138_slave_write_byte(chip->slave_client, HL7138_REG_A0, 0x9F);	/* Unlock register */
		hl7138_slave_write_byte(chip->slave_client, HL7138_REG_A3, 0x01);
		hl7138_slave_write_byte(chip->slave_client, HL7138_REG_A0, 0x00);	/* Lock register */

		hl7138_slave_read_byte(chip->slave_client, HL7138_REG_A3, &reg_data[0]);
		hl7138_slave_read_byte(chip->slave_client, HL7138_REG_A0, &reg_data[1]);
		chg_info("slave 0xA3 = 0x%02x, 0xA0 = 0x%02x\n", reg_data[0], reg_data[1]);

		if ((reg_data[0] == 0x01) && (reg_data[1] == 0x00))	/* Lock register success */
			break;
		mdelay(5);
		retry++;
	} while (retry <= 3);
	chg_info("hl7138_slave_turnoff_sys_clk done\n");

	return 0;
}

/* turn on system clk for BA version only */
static int hl7138_slave_turnon_sys_clk(struct oplus_voocphy_manager *chip)
{
	u8 reg_data[2] = {0};
	int retry = 0;
	int ret = 0;

	if (!chip) {
		chg_err("slave turn on sys clk failed\n");
		return -1;
	}

	do {
		hl7138_slave_write_byte(chip->slave_client, HL7138_REG_A0, 0xF9);
		hl7138_slave_write_byte(chip->slave_client, HL7138_REG_A0, 0x9F);	/* Unlock register */
		hl7138_slave_write_byte(chip->slave_client, HL7138_REG_A3, 0x00);
		hl7138_slave_write_byte(chip->slave_client, HL7138_REG_A0, 0x00);	/* Lock register */

		ret = hl7138_slave_read_byte(chip->slave_client, HL7138_REG_A3, &reg_data[0]);
		ret |= hl7138_slave_read_byte(chip->slave_client, HL7138_REG_A0, &reg_data[1]);
		chg_info("slave 0xA3 = 0x%02x, 0xA0 = 0x%02x\n", reg_data[0], reg_data[1]);

		/* Lock register success */
		if ((reg_data[0] == 0x00) && (reg_data[1] == 0x00) && ret == 0)
			break;
		mdelay(5);
		retry++;
	} while (retry <= 3);

	/* combined operation, let sys_clk return auto mode, current restore to uA level */
	/* force enable adc read average with 4 samples data */
	hl7138_slave_write_byte(chip->slave_client, HL7138_REG_40, 0x0D);
	/* soft reset register and disable watchdog */
	hl7138_slave_write_byte(chip->slave_client, HL7138_REG_14, 0xC8);
	mdelay(2);
	chg_info("hl7138_slave_turnon_sys_clk done\n");

	return 0;
}

int hl7138_slave_init_vooc(struct oplus_voocphy_manager *chip)
{
	chg_info(">>>> slave init vooc\n");

	hl7138_slave_init_device(chip);

	return 0;
}

static int hl7138_slave_svooc_hw_setting(struct oplus_voocphy_manager *chip)
{
	u8 reg_data;

	hl7138_slave_write_byte(chip->slave_client, HL7138_REG_08, 0x3C);	/* VBAT_OVP:4.65V,JL:00-08; */
	hl7138_slave_write_byte(chip->slave_client, HL7138_REG_40, 0x05);	/* ADC_CTRL:ADC_EN,JL:11-40; */
	reg_data = chip->reg_ctrl_1;
	hl7138_slave_write_byte(chip->slave_client, HL7138_REG_11, reg_data);	/* Disable IIN Regulation*/
	reg_data = chip->ovp_reg;
	hl7138_slave_write_byte(chip->slave_client, HL7138_REG_10, reg_data);	/* Disable VBAT Regulation*/
	hl7138_slave_write_byte(chip->slave_client, HL7138_REG_0B, 0x88);	/* VBUS_OVP:12V */
	hl7138_slave_write_byte(chip->slave_client, HL7138_REG_0C, 0x0F);	/* VIN_OVP:10.2V */

	if (chip->high_curr_setting) {
		hl7138_slave_write_byte(chip->slave_client, HL7138_REG_0E, 0xB2);	/* disable OCP */
	} else {
		hl7138_slave_write_byte(chip->slave_client, HL7138_REG_0E, 0x32);	/* IBUS_OCP:3.6A,UCP_DEB=5ms */
		hl7138_slave_write_byte(chip->slave_client, HL7138_REG_0F, 0x60);	/* IBUS_OCP:3.5A      ocp:250mA */
	}

	hl7138_slave_write_byte(chip->slave_client, HL7138_REG_14, 0x08);	/* WD:1000ms */
	hl7138_slave_write_byte(chip->slave_client, HL7138_REG_15, 0x00);	/* enter cp mode */
	hl7138_slave_write_byte(chip->slave_client, HL7138_REG_16, 0x3C);	/* OV=500, UV=250 */

	return 0;
}

static int hl7138_slave_vooc_hw_setting(struct oplus_voocphy_manager *chip)
{
	hl7138_slave_write_byte(chip->slave_client, HL7138_REG_08, 0x3C);	/* VBAT_OVP:4.65V */
	hl7138_slave_write_byte(chip->slave_client, HL7138_REG_40, 0x05);	/* ADC_CTRL:ADC_EN */

	hl7138_slave_write_byte(chip->slave_client, HL7138_REG_0B, 0x88);	/* VBUS_OVP=7V */
	hl7138_slave_write_byte(chip->slave_client, HL7138_REG_0C, 0x0F);	/* VIN_OVP:5.85v */

	hl7138_slave_write_byte(chip->slave_client, HL7138_REG_0E, 0xAF);	/* US_OCP:4.6A,(16+9)*0.1+0.1+2=4.6A; */

	hl7138_slave_write_byte(chip->slave_client, HL7138_REG_14, 0x08);	/* WD:1000ms */
	hl7138_slave_write_byte(chip->slave_client, HL7138_REG_15, 0x80);	/* bp mode; */
	hl7138_slave_write_byte(chip->slave_client, HL7138_REG_16, 0x3C);	/* OV=500, UV=250 */

	return 0;
}

static int hl7138_slave_5v2a_hw_setting(struct oplus_voocphy_manager *chip)
{
	hl7138_slave_write_byte(chip->slave_client, HL7138_REG_08, 0x3c);	/* VBAT_OVP:4.65V */
	hl7138_slave_write_byte(chip->slave_client, HL7138_REG_0B, 0x88);	/* VBUS_OVP=7V */
	hl7138_slave_write_byte(chip->slave_client, HL7138_REG_0C, 0x0F);	/* VIN_OVP:11.7v */
	hl7138_slave_write_byte(chip->slave_client, HL7138_REG_0E, 0xAF);	/* IBUS_OCP:3.6A,UCP_DEB=5ms */
	hl7138_slave_write_byte(chip->slave_client, HL7138_REG_14, 0x08);	/* WD:DIS */
	hl7138_slave_write_byte(chip->slave_client, HL7138_REG_15, 0x80);	/* bp mode; */

	hl7138_slave_write_byte(chip->slave_client, HL7138_REG_40, 0x00);	/* DC_CTRL:disable */

	return 0;
}

static int hl7138_slave_pdqc_hw_setting(struct oplus_voocphy_manager *chip)
{
	hl7138_slave_write_byte(chip->slave_client, HL7138_REG_08, 0x3c);	/* VBAT_OVP:4.45V */
	hl7138_slave_write_byte(chip->slave_client, HL7138_REG_0B, 0x88);	/* VBUS_OVP:12V */
	hl7138_slave_write_byte(chip->slave_client, HL7138_REG_0C, 0x0F);	/* VIN_OVP:11.7V */
	hl7138_slave_write_byte(chip->slave_client, HL7138_REG_0E, 0xAF);	/* IBUS_OCP:3.6A */
	hl7138_slave_write_byte(chip->slave_client, HL7138_REG_14, 0x08);	/* WD:DIS */
	hl7138_slave_write_byte(chip->slave_client, HL7138_REG_15, 0x00);	/* enter cp mode */
	hl7138_slave_write_byte(chip->slave_client, HL7138_REG_40, 0x00);	/* ADC_CTRL:disable */
	return 0;
}

static int hl7138_slave_hw_setting(struct oplus_voocphy_manager *chip, int reason)
{
	if (!chip) {
		chg_err("chip is null exit\n");
		return -1;
	}
	switch (reason) {
		case SETTING_REASON_PROBE:
		case SETTING_REASON_RESET:
			hl7138_slave_init_device(chip);
			chg_info("SETTING_REASON_RESET OR PROBE\n");
			break;
		case SETTING_REASON_SVOOC:
			hl7138_slave_svooc_hw_setting(chip);
			chg_info("SETTING_REASON_SVOOC\n");
			break;
		case SETTING_REASON_VOOC:
			hl7138_slave_vooc_hw_setting(chip);
			chg_info("SETTING_REASON_VOOC\n");
			break;
		case SETTING_REASON_5V2A:
			hl7138_slave_5v2a_hw_setting(chip);
			chg_info("SETTING_REASON_5V2A\n");
			break;
		case SETTING_REASON_PDQC:
			hl7138_slave_pdqc_hw_setting(chip);
			chg_info("SETTING_REASON_PDQC\n");
			break;
		default:
			chg_err("do nothing\n");
			break;
	}
	return 0;
}

static int hl7138_slave_reset_voocphy(struct oplus_voocphy_manager *chip)
{
	hl7138_slave_set_chg_enable(chip, false);
	hl7138_slave_hw_setting(chip, SETTING_REASON_RESET);

	return VOOCPHY_SUCCESS;
}

static int hl7138_slave_reg_reset(struct oplus_voocphy_manager *chip, bool enable)
{
	unsigned char value;
	int ret = 0;
	ret = hl7138_slave_read_byte(chip->slave_client, HL7138_REG_01, &value);	/* clear INTb */

	hl7138_slave_write_byte(chip->slave_client, HL7138_REG_09, 0x00);	/* set default mode; */
	hl7138_slave_write_byte(chip->slave_client, HL7138_REG_0A, 0xAE);	/* set default mode; */
	hl7138_slave_write_byte(chip->slave_client, HL7138_REG_0C, 0x03);	/* set default mode; */
	hl7138_slave_write_byte(chip->slave_client, HL7138_REG_0F, 0x00);	/* set default mode; */
	hl7138_slave_write_byte(chip->slave_client, HL7138_REG_13, 0x40);	/* set 100ms ucp debounce time; */
	hl7138_slave_write_byte(chip->slave_client, HL7138_REG_15, 0x00);	/* set default mode; */
	hl7138_slave_write_byte(chip->slave_client, HL7138_REG_17, 0x00);	/* set default mode; */
	hl7138_slave_read_byte(chip->slave_client, HL7138_REG_05, &value);
	hl7138_slave_read_byte(chip->slave_client, HL7138_REG_06, &value);
	hl7138_slave_write_byte(chip->slave_client, HL7138_REG_37, 0x02);	/* reset VOOC PHY; */
	hl7138_slave_read_byte(chip->slave_client, HL7138_REG_3B, &value);  /* clear flag 2023-jl */
	hl7138_slave_write_byte(chip->slave_client, HL7138_REG_3F, 0xD1);	/* bit7 T6:170us */

	return ret;
}

static int hl7138_get_slave_cp_vbat(struct oplus_voocphy_manager *chip)
{
	u8 data_block[HL7138_REG_NUM_2] = {0};

	/*parse data_block for improving time of interrupt*/
	i2c_smbus_read_i2c_block_data(chip->slave_client, HL7138_REG_46, HL7138_REG_NUM_2, data_block);

	chip->slave_cp_vbat = ((data_block[0] << HL7138_REG_ADC_BIT_OFFSET_4) | data_block[1]) * HL7138_FACTOR_125_100;

	return chip->slave_cp_vbat;
}

static int hl7138_get_slave_cp_vbus(struct oplus_voocphy_manager *chip)
{
	u8 data_block[HL7138_REG_NUM_12] = {0};

	i2c_smbus_read_i2c_block_data(chip->slave_client, HL7138_REG_42, HL7138_REG_NUM_12, data_block);		/* JL:first Reg is 13,need to change to 42; */

	chip->slave_cp_vbus = ((data_block[HL7138_REG_ADC_H_BIT_VBUS] << HL7138_REG_ADC_BIT_OFFSET_4)
			| data_block[HL7138_REG_ADC_L_BIT_VBUS]) * HL7138_FACTOR_400_100;

	return chip->slave_cp_vbus;
}

static int hl7138_slave_get_adc_enable(struct oplus_voocphy_manager *chip, u8 *data)
{
	int ret = 0;

	if (!chip) {
		chg_err("Failed\n");
		return -1;
	}

	ret = hl7138_slave_read_byte(chip->slave_client, HL7138_REG_40, data);
	if (ret < 0) {
		chg_err("HL7138_REG_40\n");
		return -1;
	}

	*data = (*data & HL7138_ADC_ENABLE);

	return ret;
}

static int hl7138_slave_set_adc_enable(struct oplus_voocphy_manager *chip, bool enable)
{
	if (!chip) {
		chg_err("Failed\n");
		return -1;
	}

	if (enable)
		return hl7138_slave_update_bits(chip->slave_client, HL7138_REG_40,
									HL7138_ADC_EN_MASK, HL7138_ADC_ENABLE);
	else
		return hl7138_slave_update_bits(chip->slave_client, HL7138_REG_40,
									HL7138_ADC_EN_MASK, HL7138_ADC_DISABLE);
}

static void hl7138_slave_hardware_init(struct oplus_voocphy_manager *chip)
{
	hl7138_slave_reg_reset(chip, true);
	hl7138_slave_work_mode_lockcheck(chip);
	hl7138_slave_init_device(chip);
}

static bool hl7138_slave_check_work_mode_support(enum oplus_cp_work_mode mode)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(g_cp_support_work_mode); i++) {
		if (g_cp_support_work_mode[i] == mode)
			return true;
	}
	return false;
}

static int hl7138_slave_cp_init(struct oplus_chg_ic_dev *ic_dev)
{
	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	ic_dev->online = true;
	oplus_chg_ic_virq_trigger(ic_dev, OPLUS_IC_VIRQ_ONLINE);

	return 0;
}

static int hl7138_slave_cp_exit(struct oplus_chg_ic_dev *ic_dev)
{
	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	ic_dev->online = false;
	oplus_chg_ic_virq_trigger(ic_dev, OPLUS_IC_VIRQ_OFFLINE);

	return 0;
}

static int hl7138_slave_cp_get_iin(struct oplus_chg_ic_dev *ic_dev, int *iin)
{
	struct hl7138_slave_device *device;
	int rc;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	device = oplus_chg_ic_get_priv_data(ic_dev);

	rc = hl7138_slave_get_ichg(oplus_voocphy_mg);
	if (rc < 0) {
		chg_err("can't get cp iin, rc=%d\n", rc);
		return rc;
	}
	*iin = rc;
	return 0;
}


static int hl7138_slave_cp_reg_dump(struct oplus_chg_ic_dev *ic_dev)
{
	struct hl7138_device *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_priv_data(ic_dev);

//	hl7138_voocphy_dump_reg_in_err_issue(chip->voocphy);
	return 0;
}

static int hl7138_slave_cp_smt_test(struct oplus_chg_ic_dev *ic_dev, char buf[], int len)
{
	return 0;
}


static int hl7138_slave_cp_enable(struct oplus_chg_ic_dev *ic_dev, bool en)
{
	struct hl7138_slave_device *chip;
	int ret = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_priv_data(ic_dev);

	if (en)
		ret = hl7138_slave_write_byte(chip->slave_client, HL7138_REG_14, 0x02); /* WD:1000ms */
	else
		ret = hl7138_slave_write_byte(chip->slave_client, HL7138_REG_14, 0x08); /* dsiable wdt */

	if (ret < 0) {
		chg_err("failed to set hl7138_cp_enable (%d)(%d)\n", en, ret);
		return ret;
	}
	return 0;
}

static int hl7138_slave_cp_hw_init(struct oplus_chg_ic_dev *ic_dev)
{
	struct hl7138_slave_device *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_priv_data(ic_dev);

	if (chip->rested)
		return 0;

	hl7138_slave_hardware_init(chip->voocphy);
	return 0;
}

static int hl7138_slave_cp_set_work_mode(struct oplus_chg_ic_dev *ic_dev, enum oplus_cp_work_mode mode)
{
	struct hl7138_slave_device *chip;
	int rc;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_priv_data(ic_dev);

	if (!hl7138_slave_check_work_mode_support(mode)) {
		chg_err("not supported work mode, mode=%d\n", mode);
		return -EINVAL;
	}

	if (mode == CP_WORK_MODE_BYPASS)
		rc = hl7138_slave_vooc_hw_setting(chip->voocphy);
	else
		rc = hl7138_slave_svooc_hw_setting(chip->voocphy);

	if (rc < 0)
		chg_err("set work mode to %d error\n", mode);

	return rc;
}

static int hl7138_slave_cp_get_work_mode(struct oplus_chg_ic_dev *ic_dev, enum oplus_cp_work_mode *mode)
{
	struct oplus_voocphy_manager *chip;
	u8 data;
	int rc;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_priv_data(ic_dev);

	rc = hl7138_slave_read_byte(chip->slave_client, HL7138_REG_14, &data);
	if (rc < 0) {
		chg_err("read hl7138_REG_14 error, rc=%d\n", rc);
		return rc;
	}

	if (data & BIT(7))
		*mode = CP_WORK_MODE_BYPASS;
	else
		*mode = CP_WORK_MODE_2_TO_1;

	return 0;
}

static int hl7138_slave_cp_check_work_mode_support(struct oplus_chg_ic_dev *ic_dev, enum oplus_cp_work_mode mode)
{
	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	return hl7138_slave_check_work_mode_support(mode);
}

static int hl7138_slave_cp_get_vin(struct oplus_chg_ic_dev *ic_dev, int *vin)
{
	struct hl7138_slave_device *chip;
	int rc;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_priv_data(ic_dev);

	rc = hl7138_get_slave_cp_vbus(chip->voocphy);
	if (rc < 0) {
		chg_err("can't get cp vin, rc=%d\n", rc);
		return rc;
	}
	*vin = rc;

	return 0;
}

static int hl7138_slave_cp_get_vout(struct oplus_chg_ic_dev *ic_dev, int *vout)
{
	struct hl7138_slave_device *chip;
	int rc;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_priv_data(ic_dev);

	rc = hl7138_get_slave_cp_vbat(chip->voocphy);
	if (rc < 0) {
		chg_err("can't get cp vout, rc=%d\n", rc);
		return rc;
	}
	*vout = rc;

	return 0;
}

static int hl7138_slave_cp_get_iout(struct oplus_chg_ic_dev *ic_dev, int *iout)
{
	struct hl7138_slave_device *chip;
	int iin;
	bool working;
	enum oplus_cp_work_mode work_mode;
	int rc;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_priv_data(ic_dev);

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
	case CP_WORK_MODE_2_TO_1:
		*iout = iin * 2;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int hl7138_slave_cp_set_work_start(struct oplus_chg_ic_dev *ic_dev, bool start)
{
	struct hl7138_slave_device *chip;
	int rc;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_priv_data(ic_dev);

	chg_info("%s work %s\n", chip->slave_dev->of_node->name, start ? "start" : "stop");

	rc = hl7138_slave_set_chg_enable(chip->voocphy, start);
	if (rc < 0)
		return rc;
	oplus_imp_node_set_active(chip->input_imp_node, start);
	oplus_imp_node_set_active(chip->output_imp_node, start);

	return 0;
}

static int hl7138_slave_cp_get_work_status(struct oplus_chg_ic_dev *ic_dev, bool *start)
{
	struct hl7138_slave_device *chip;
	u8 data;
	int rc;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_priv_data(ic_dev);

	rc = hl7138_slave_read_byte(chip->slave_client, HL7138_REG_12, &data);
	if (rc < 0) {
		chg_err("read hl7138_REG_07 error, rc=%d\n", rc);
		return rc;
	}

	*start = data & BIT(7);

	return 0;
}

static int hl7138_slave_cp_adc_enable(struct oplus_chg_ic_dev *ic_dev, bool en)
{
	struct hl7138_slave_device *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_priv_data(ic_dev);

	return hl7138_slave_set_adc_enable(chip->voocphy, en);

	return 0;
}

static void *hl7138_slave_cp_get_func(struct oplus_chg_ic_dev *ic_dev, enum oplus_chg_ic_func func_id)
{
	void *func = NULL;

	if (!ic_dev->online && (func_id != OPLUS_IC_FUNC_INIT) &&
	    (func_id != OPLUS_IC_FUNC_EXIT)) {
		chg_err("%s is offline\n", ic_dev->name);
		return NULL;
	}

	switch (func_id) {
	case OPLUS_IC_FUNC_INIT:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_INIT, hl7138_slave_cp_init);
		break;
	case OPLUS_IC_FUNC_EXIT:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_EXIT, hl7138_slave_cp_exit);
		break;
	case OPLUS_IC_FUNC_REG_DUMP:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_REG_DUMP, hl7138_slave_cp_reg_dump);
		break;
	case OPLUS_IC_FUNC_SMT_TEST:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_SMT_TEST, hl7138_slave_cp_smt_test);
		break;
	case OPLUS_IC_FUNC_CP_ENABLE:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_CP_ENABLE, hl7138_slave_cp_enable);
		break;
	case OPLUS_IC_FUNC_CP_HW_INTI:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_CP_HW_INTI, hl7138_slave_cp_hw_init);
		break;
	case OPLUS_IC_FUNC_CP_SET_WORK_MODE:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_CP_SET_WORK_MODE, hl7138_slave_cp_set_work_mode);
		break;
	case OPLUS_IC_FUNC_CP_GET_WORK_MODE:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_CP_GET_WORK_MODE, hl7138_slave_cp_get_work_mode);
		break;
	case OPLUS_IC_FUNC_CP_CHECK_WORK_MODE_SUPPORT:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_CP_CHECK_WORK_MODE_SUPPORT,
			hl7138_slave_cp_check_work_mode_support);
		break;
	case OPLUS_IC_FUNC_CP_GET_VIN:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_CP_GET_VIN, hl7138_slave_cp_get_vin);
		break;
	case OPLUS_IC_FUNC_CP_GET_IIN:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_CP_GET_IIN, hl7138_slave_cp_get_iin);
		break;
	case OPLUS_IC_FUNC_CP_GET_VOUT:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_CP_GET_VOUT, hl7138_slave_cp_get_vout);
		break;
	case OPLUS_IC_FUNC_CP_GET_IOUT:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_CP_GET_IOUT, hl7138_slave_cp_get_iout);
		break;
	case OPLUS_IC_FUNC_CP_SET_WORK_START:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_CP_SET_WORK_START, hl7138_slave_cp_set_work_start);
		break;
	case OPLUS_IC_FUNC_CP_GET_WORK_STATUS:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_CP_GET_WORK_STATUS, hl7138_slave_cp_get_work_status);
		break;
	case OPLUS_IC_FUNC_CP_SET_ADC_ENABLE:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_CP_SET_ADC_ENABLE, hl7138_slave_cp_adc_enable);
		break;
	default:
		chg_err("this func(=%d) is not supported\n", func_id);
		func = NULL;
		break;
	}

	return func;
}

struct oplus_chg_ic_virq hl7138_slave_cp_virq_table[] = {
	{ .virq_id = OPLUS_IC_VIRQ_ERR },
	{ .virq_id = OPLUS_IC_VIRQ_ONLINE },
	{ .virq_id = OPLUS_IC_VIRQ_OFFLINE },
};

static int hl7138_slave_ic_register(struct hl7138_slave_device *device)
{
	enum oplus_chg_ic_type ic_type;
	int ic_index;
	struct device_node *child;
	struct oplus_chg_ic_dev *ic_dev = NULL;
	struct oplus_chg_ic_cfg ic_cfg;
	int rc;

	for_each_child_of_node(device->slave_dev->of_node, child) {
		rc = of_property_read_u32(child, "oplus,ic_type", &ic_type);
		if (rc < 0)
			continue;
		rc = of_property_read_u32(child, "oplus,ic_index", &ic_index);
		if (rc < 0)
			continue;
		ic_cfg.name = child->name;
		ic_cfg.index = ic_index;
		ic_cfg.type = ic_type;
		ic_cfg.priv_data = device;
		ic_cfg.of_node = child;
		switch (ic_type) {
		case OPLUS_CHG_IC_CP:
			snprintf(ic_cfg.manu_name, OPLUS_CHG_IC_MANU_NAME_MAX - 1, "cp-hl7138:%d", ic_index);
			snprintf(ic_cfg.fw_id, OPLUS_CHG_IC_FW_ID_MAX - 1, "0x00");
			ic_cfg.get_func = hl7138_slave_cp_get_func;
			ic_cfg.virq_data = hl7138_slave_cp_virq_table;
			ic_cfg.virq_num = ARRAY_SIZE(hl7138_slave_cp_virq_table);
			break;
		default:
			chg_err("not support ic_type(=%d)\n", ic_type);
			continue;
		}

		ic_dev = devm_oplus_chg_ic_register(device->slave_dev, &ic_cfg);
		if (!ic_dev) {
			rc = -ENODEV;
			chg_err("register %s error\n", child->name);
			continue;
		}
		chg_info("register %s\n", child->name);

		switch (ic_dev->type) {
		case OPLUS_CHG_IC_CP:
			device->cp_work_mode = CP_WORK_MODE_UNKNOWN;
			device->cp_ic = ic_dev;
			break;
		default:
			chg_err("not support ic_type(=%d)\n", ic_dev->type);
			continue;
		}
		of_platform_populate(child, NULL, NULL, device->slave_dev);
	}

	return 0;
}

static ssize_t hl7138_slave_show_registers(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct oplus_voocphy_manager *chip = dev_get_drvdata(dev);
	u8 addr;
	u8 val;
	u8 tmpbuf[300];
	int len;
	int idx = 0;
	int ret;

	idx = snprintf(buf, PAGE_SIZE, "%s:\n", "chip");
	for (addr = 0x0; addr <= 0x4F; addr++) {
		if((addr < 0x18) || (addr > 0x35 && addr < 0x4F)) {
			ret = hl7138_slave_read_byte(chip->slave_client, addr, &val);
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

static ssize_t hl7138_slave_store_register(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct oplus_voocphy_manager *chip = dev_get_drvdata(dev);
	int ret;
	unsigned int reg;
	unsigned int val;

	ret = sscanf(buf, "%x %x", &reg, &val);
	if (ret == 2 && reg <= 0x4F)
		hl7138_slave_write_byte(chip->slave_client, (unsigned char)reg, (unsigned char)val);

	return count;
}
static DEVICE_ATTR(registers, 0660, hl7138_slave_show_registers, hl7138_slave_store_register);

static void hl7138_slave_create_device_node(struct device *dev)
{
	int err;

	err = device_create_file(dev, &dev_attr_registers);
	if (err)
		chg_err("hl7138 create device err!\n");
}

static struct of_device_id hl7138_slave_charger_match_table[] = {
	{
		.compatible = "slave_vphy_hl7138",
	},
	{},
};

static struct oplus_voocphy_operations oplus_hl7138_slave_ops = {
	.hw_setting = hl7138_slave_hw_setting,
	.init_vooc = hl7138_slave_init_vooc,
	.update_data = hl7138_slave_update_data,
	.get_chg_enable = hl7138_slave_get_chg_enable,
	.set_chg_enable = hl7138_slave_set_chg_enable,
	.get_ichg = hl7138_slave_get_ichg,
	.reset_voocphy = hl7138_slave_reset_voocphy,
	.get_cp_status = hl7138_slave_get_cp_status,
	.get_adc_enable = hl7138_slave_get_adc_enable,
	.set_adc_enable = hl7138_slave_set_adc_enable,
};

static int hl7138_slave_parse_dt(struct oplus_voocphy_manager *chip)
{
	int rc;
	struct device_node * node = NULL;

	if (!chip) {
		chg_info("chip null\n");
		return -1;
	}

	node = chip->slave_dev->of_node;

	chip->high_curr_setting = of_property_read_bool(node, "oplus_spec,high_curr_setting");


	rc = of_property_read_u32(node, "ovp_reg", &chip->ovp_reg);
	if (rc)
		chip->ovp_reg = 0x3C;
	chg_err("ovp_reg=0x%2x\n", chip->ovp_reg);

	rc = of_property_read_u32(node, "reg_ctrl_1", &chip->reg_ctrl_1);
	if (rc)
		chip->reg_ctrl_1 = 0xFC;
	chg_err("reg_ctrl_1=0x%2x\n", chip->reg_ctrl_1);

	rc = of_property_read_u32(node, "ocp_reg", &chip->ocp_reg);
	if (rc)
		chip->ocp_reg = 0x32;
	chg_err("ocp_reg=0x%2x\n", chip->ocp_reg);
	return 0;
}

static int hl7138_slave_charger_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct hl7138_slave_device *device;
	struct oplus_voocphy_manager *chip;
	int ret;

	chg_info("hl7138_slave_charger_probe enter!\n");

	device = devm_kzalloc(&client->dev, sizeof(*device), GFP_KERNEL);
	if (device == NULL) {
		chg_err("alloc hl7138 device buf error\n");
		return -ENOMEM;
	}

	chip = devm_kzalloc(&client->dev, sizeof(*chip), GFP_KERNEL);
	if (chip == NULL) {
		dev_err(&client->dev, "Couldn't allocate memory\n");
		ret = -ENOMEM;
		goto device_err;
	}

	device->slave_client = client;
	device->slave_dev = &client->dev;
	chip->slave_client = client;
	chip->slave_dev = &client->dev;
	chip->priv_data = device;
	device->voocphy = chip;
	mutex_init(&i2c_rw_lock);
	i2c_set_clientdata(client, chip);

	if (oplus_voocphy_chip_is_null()) {
		pr_err("oplus_voocphy_chip null, will do after master cp init.\n");
		ret = -EPROBE_DEFER;
		goto chip_err;
	}

	hl7138_slave_create_device_node(&(client->dev));

	hl7138_slave_parse_dt(chip);
	hl7138_slave_init_device(chip);


	chip->slave_ops = &oplus_hl7138_slave_ops;

	oplus_voocphy_slave_init(chip);

	oplus_voocphy_get_chip(&oplus_voocphy_mg);

	/* turn on system clk for BA version only */
	if (hl7138_check_slave_hw_ba_version(chip))
		hl7138_slave_turnon_sys_clk(chip);

	ret = hl7138_slave_ic_register(device);
	if (ret < 0) {
		chg_err("slave cp ic register error\n");
		ret = -ENOMEM;
		goto chip_err;
	}
	hl7138_slave_cp_init(device->cp_ic);

	chg_info("hl7138_slave_charger_probe succesfull\n");
	return 0;

chip_err:
	devm_kfree(&client->dev, chip);
device_err:
	devm_kfree(&client->dev, device);
	return ret;
}

static void hl7138_slave_charger_shutdown(struct i2c_client *client)
{
	hl7138_slave_write_byte(client, HL7138_REG_40, 0x00);	/* disable */
	hl7138_slave_write_byte(client, HL7138_REG_12, 0x25);

	/* turn off system clk for BA version only */
	if (hl7138_check_slave_hw_ba_version(oplus_voocphy_mg))
		hl7138_slave_turnoff_sys_clk(oplus_voocphy_mg);

	chg_err("hl7138_slave_charger_shutdown end\n");

	return;
}

static const struct i2c_device_id hl7138_slave_charger_id[] = {
	{"hl7138-slave", 0},
	{},
};

static struct i2c_driver hl7138_slave_charger_driver = {
	.driver		= {
		.name	= "hl7138-charger-slave",
		.owner	= THIS_MODULE,
		.of_match_table = hl7138_slave_charger_match_table,
	},
	.id_table	= hl7138_slave_charger_id,

	.probe		= hl7138_slave_charger_probe,
	.shutdown	= hl7138_slave_charger_shutdown,
};

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0))
static int __init hl7138_slave_subsys_init(void)
{
	int ret = 0;
	chg_info(" init start\n");

	if (i2c_add_driver(&hl7138_slave_charger_driver) != 0) {
		chg_err(" failed to register hl7138 i2c driver.\n");
	} else {
		chg_info(" Success to register hl7138 i2c driver.\n");
	}

	return ret;
}

subsys_initcall(hl7138_slave_subsys_init);
#else
int hl7138_slave_subsys_init(void)
{
	int ret = 0;
	chg_info(" init start\n");

	if (i2c_add_driver(&hl7138_slave_charger_driver) != 0) {
		chg_err(" failed to register hl7138 i2c driver.\n");
	} else {
		chg_info(" Success to register hl7138 i2c driver.\n");
	}

	return ret;
}

void hl7138_slave_subsys_exit(void)
{
	i2c_del_driver(&hl7138_slave_charger_driver);
}
oplus_chg_module_register(hl7138_slave_subsys);
#endif /*LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0)*/

MODULE_DESCRIPTION("Hl7138 Charge Pump Driver");
MODULE_LICENSE("GPL v2");

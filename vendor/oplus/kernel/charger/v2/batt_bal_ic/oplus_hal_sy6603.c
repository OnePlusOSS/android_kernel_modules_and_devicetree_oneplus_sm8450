// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#define pr_fmt(fmt) "[OPLUS_SY6603]([%s][%d]): " fmt, __func__, __LINE__

#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/bitops.h>
#include <linux/regmap.h>
#include <linux/pinctrl/consumer.h>
#include <linux/list.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/mutex.h>
#include <linux/string.h>
#include <linux/rtc.h>
#include <linux/device.h>
#include <linux/module.h>
#include <oplus_chg_module.h>
#include <oplus_chg_ic.h>
#include <oplus_chg.h>
#include <oplus_batt_bal.h>
#include "oplus_hal_sy6603.h"
#include "test-kit.h"
#include "../monitor/oplus_chg_track.h"
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_CHG_DEBUG_KIT)
#include <debug-kit.h>
#endif

struct chip_sy6603 {
	struct i2c_client *client;
	struct device *dev;
	struct oplus_chg_ic_dev *ic_dev;

	int en_gpio;
	int interrupt_gpio;
	int pmos_gpio;
	int interrupt_irq;
	bool irq_enabled;

	struct pinctrl *pinctrl;
	struct pinctrl_state *int_default;
	struct pinctrl_state *en_active;
	struct pinctrl_state *en_sleep;
	struct pinctrl_state *pmos_active;
	struct pinctrl_state *pmos_sleep;

	struct regmap *regmap;
	atomic_t suspended;
	struct mutex i2c_lock;
	struct mutex data_lock;
	struct mutex pinctrl_lock;

	unsigned long trigger_err_type;
	int iref_sw_step_adjust_max_amp;
	int min_iref;

	atomic_t i2c_err_count;
	struct delayed_work wdg_reset_work;
	bool main_gauge_equal_b1;
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_CHG_DEBUG_KIT)
	struct oplus_device_bus *odb;
#endif
#if IS_ENABLED(CONFIG_OPLUS_CHG_TEST_KIT)
	struct test_feature *bal_ic_test;
#endif
	bool disbale_bal;
	bool hw_init_err;
};

static int sy6603_hardware_init(struct chip_sy6603 *chip);

struct sy6603_err_reason {
	int err_type;
	char err_name[SY6603_DEVICE_ERR_NAME_LEN];
};

struct oplus_chg_ic_virq sy6603_virq_table[] = {
	{ .virq_id = OPLUS_IC_VIRQ_ERR },
};

static struct regmap_config sy6603_regmap_config = {
	.reg_bits	= 8,
	.val_bits	= 8,
	.max_register	= SY6603_REG_ADDR_MAX,
};

#define TRACK_UPLOAD_COUNT_MAX			10
#define TRACK_DEVICE_ABNORMAL_UPLOAD_PERIOD	(24 * 3600)
#define MAX_STR_LEN				256
static void sy6603_upload_track(struct chip_sy6603 *chip, int type, int sub_type, char *str)
{
	static int upload_count = 0;
	static int pre_upload_time = 0;
	int curr_time;

	curr_time = oplus_chg_track_get_local_time_s();
	if (curr_time - pre_upload_time > TRACK_DEVICE_ABNORMAL_UPLOAD_PERIOD)
		upload_count = 0;

	if (upload_count < TRACK_UPLOAD_COUNT_MAX) {
		upload_count++;
		pre_upload_time = oplus_chg_track_get_local_time_s();
		oplus_chg_ic_creat_err_msg(chip->ic_dev, type, sub_type, str);
		oplus_chg_ic_virq_trigger(chip->ic_dev, OPLUS_IC_VIRQ_ERR);
	}
}

static __inline__ void sy6603_i2c_err_inc(struct chip_sy6603 *chip, u8 addr, bool read)
{
	int en_gpio_val;
	char buf[MAX_STR_LEN] = {0};

	if (unlikely(!chip->ic_dev))
		return;

	if (unlikely(!chip->ic_dev->online))
		return;
	en_gpio_val = gpio_get_value(chip->en_gpio);
	if (!en_gpio_val) {
		chg_info("not enable, not handle");
		return;
	}

	if (atomic_read(&chip->suspended) == 1 ||
	    atomic_read(&chip->i2c_err_count) > SY6603_I2C_ERR_MAX)
		return;

	atomic_inc(&chip->i2c_err_count);
	scnprintf(buf, sizeof(buf), "$$err_reason@@i2c_err$$info@@addr[0x%x]%s", addr, read ? "read error" : "write error");
	sy6603_upload_track(chip, OPLUS_IC_ERR_I2C, 0, buf);
}

static __inline__ void sy6603_i2c_err_clr(struct chip_sy6603 *chip)
{
	if (unlikely(!chip->ic_dev))
		return;

	if (unlikely(!chip->ic_dev->online))
		return;

	if (atomic_inc_return(&chip->i2c_err_count) > SY6603_I2C_ERR_MAX)
		atomic_set(&chip->i2c_err_count, 0);
}

static int sy6603_read_byte(struct chip_sy6603 *chip, u8 addr, u8 *data)
{
	int rc;
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_CHG_DEBUG_KIT)
	unsigned int buf;
#endif
	int en_gpio_val;

	if (chip->disbale_bal) {
		en_gpio_val = gpio_get_value(chip->en_gpio);
		if (!en_gpio_val)
			return 0;
	}

	mutex_lock(&chip->i2c_lock);
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_CHG_DEBUG_KIT)
	rc = oplus_dev_bus_read(chip->odb, addr, &buf);
	if (rc >= 0)
		rc = buf;
#else
	rc = i2c_smbus_read_byte_data(chip->client, addr);
#endif
	if (rc < 0) {
		chg_err("read 0x%04x error, rc=%d\n", addr, rc);
		rc = rc < 0 ? rc : -EIO;
		goto error;
	}

	*data = rc;
	mutex_unlock(&chip->i2c_lock);
	sy6603_i2c_err_clr(chip);
	return 0;

error:
	mutex_unlock(&chip->i2c_lock);
	sy6603_i2c_err_inc(chip, addr, true);
	return rc;
}

__maybe_unused static int sy6603_read_data(
	struct chip_sy6603 *chip, u8 addr, u8 *buf, int len)
{
	int rc;
	int en_gpio_val;

	if (chip->disbale_bal) {
		en_gpio_val = gpio_get_value(chip->en_gpio);
		if (!en_gpio_val)
			return 0;
	}

	mutex_lock(&chip->i2c_lock);
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_CHG_DEBUG_KIT)
	rc = oplus_dev_bus_bulk_read(chip->odb, addr, buf, len);
#else
	rc = i2c_smbus_read_i2c_block_data(chip->client, addr, len, buf);
#endif
	if (rc < 0) {
		chg_err("read 0x%04x error, rc=%d\n", addr, rc);
		rc = rc < 0 ? rc : -EIO;
		goto error;
	}

	mutex_unlock(&chip->i2c_lock);
	sy6603_i2c_err_clr(chip);
	return 0;

error:
	mutex_unlock(&chip->i2c_lock);
	sy6603_i2c_err_inc(chip, addr, true);
	return rc;
}

__maybe_unused static int sy6603_write_byte(struct chip_sy6603 *chip, u8 addr, u8 data)
{
	int rc;
	int en_gpio_val;

	if (chip->disbale_bal) {
		en_gpio_val = gpio_get_value(chip->en_gpio);
		if (!en_gpio_val)
			return 0;
	}

	mutex_lock(&chip->i2c_lock);
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_CHG_DEBUG_KIT)
	rc = oplus_dev_bus_write(chip->odb, addr, data);
#else
	rc = i2c_smbus_write_byte_data(chip->client, addr, data);
#endif
	if (rc < 0) {
		chg_err("write 0x%04x error, rc=%d\n", addr, rc);
		mutex_unlock(&chip->i2c_lock);
		sy6603_i2c_err_inc(chip, addr, false);
		rc = rc < 0 ? rc : -EIO;
		return rc;
	}
	mutex_unlock(&chip->i2c_lock);
	sy6603_i2c_err_clr(chip);

	return 0;
}

__maybe_unused static int sy6603_write_data(
	struct chip_sy6603 *chip, u8 addr, u8 *buf, int len)
{
	int rc;
	int en_gpio_val;

	if (chip->disbale_bal) {
		en_gpio_val = gpio_get_value(chip->en_gpio);
		if (!en_gpio_val)
			return 0;
	}

	mutex_lock(&chip->i2c_lock);
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_CHG_DEBUG_KIT)
	rc = oplus_dev_bus_bulk_write(chip->odb, addr, buf, len);
#else
	rc = i2c_smbus_write_i2c_block_data(chip->client, addr, len, buf);
#endif
	if (rc < 0) {
		chg_err("write 0x%04x error, rc=%d\n", addr, rc);
		mutex_unlock(&chip->i2c_lock);
		sy6603_i2c_err_inc(chip, addr, false);
		rc = rc < 0 ? rc : -EIO;
		return rc;
	}
	mutex_unlock(&chip->i2c_lock);
	sy6603_i2c_err_clr(chip);

	return 0;
}

__maybe_unused static int sy6603_read_byte_mask(
	struct chip_sy6603 *chip, u8 addr, u8 mask, u8 *data)
{
	u8 temp;
	int rc;
	int en_gpio_val;

	if (chip->disbale_bal) {
		en_gpio_val = gpio_get_value(chip->en_gpio);
		if (!en_gpio_val)
			return 0;
	}

	mutex_lock(&chip->data_lock);
	rc = sy6603_read_byte(chip, addr, &temp);
	if (rc < 0) {
		mutex_unlock(&chip->data_lock);
		sy6603_i2c_err_inc(chip, addr, true);
		return rc;
	}

	*data = mask & temp;
	mutex_unlock(&chip->data_lock);
	sy6603_i2c_err_clr(chip);

	return 0;
}

__maybe_unused static int sy6603_write_byte_mask(
	struct chip_sy6603 *chip, u8 addr, u8 mask, u8 data)
{
	u8 temp;
	int rc;
	int en_gpio_val;

	if (chip->disbale_bal) {
		en_gpio_val = gpio_get_value(chip->en_gpio);
		if (!en_gpio_val)
			return 0;
	}

	mutex_lock(&chip->data_lock);
	rc = sy6603_read_byte(chip, addr, &temp);
	if (rc < 0) {
		mutex_unlock(&chip->data_lock);
		sy6603_i2c_err_inc(chip, addr, false);
		return rc;
	}
	temp = (data & mask) | (temp & (~mask));
	chg_info("addr=0x%x, temp=0x%x\n", addr, temp);
	rc = sy6603_write_byte(chip, addr, temp);
	mutex_unlock(&chip->data_lock);
	if (rc < 0)
		return rc;

	sy6603_i2c_err_clr(chip);
	return 0;
}

static int oplus_sy6603_init(struct oplus_chg_ic_dev *ic_dev)
{
	struct chip_sy6603 *chip;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);
	if (!chip) {
		chg_err("chip is NULL");
		return -ENODEV;
	}
	if (chip->hw_init_err)
		rc = sy6603_hardware_init(chip);

	if (rc < 0) {
		ic_dev->online = false;
		return -EINVAL;
	} else {
		chip->hw_init_err = false;
		ic_dev->online = true;
	}

	return 0;
}

static int oplus_sy6603_exit(struct oplus_chg_ic_dev *ic_dev)
{
	if (!ic_dev->online)
		return 0;

	ic_dev->online = false;

	return 0;
}

static int oplus_sy6603_get_enable(struct oplus_chg_ic_dev *ic_dev, bool *enable)
{
	int rc;
	u8 data;
	int en_gpio_val;
	struct chip_sy6603 *chip;

	if (ic_dev == NULL) {
		chg_err("ic_dev is NULL");
		return -ENODEV;
	}

	chip = oplus_chg_ic_get_drvdata(ic_dev);
	if (!chip) {
		chg_err("chip not specified!\n");
		return -EINVAL;
	}

	if (atomic_read(&chip->suspended) == 1) {
		chg_err("sy6603 is suspend\n");
		return -EINVAL;
	}

	en_gpio_val = gpio_get_value(chip->en_gpio);
	if (!en_gpio_val) {
		*enable = false;
		return 0;
	}

	rc = sy6603_read_byte_mask(chip,
		SY6603_REG_ADDR_00H, SY6603_CONVER_ENABLE_MASK, &data);
	if (rc < 0) {
		*enable = false;
		return rc;
	}

	if (!!data)
		*enable = true;
	else
		*enable = false;

	return 0;
}

static int oplus_sy6603_get_pmos_enable(struct oplus_chg_ic_dev *ic_dev, bool *enable)
{
	struct chip_sy6603 *chip;

	if (ic_dev == NULL) {
		chg_err("ic_dev is NULL");
		return -ENODEV;
	}

	chip = oplus_chg_ic_get_drvdata(ic_dev);
	if (!chip) {
		chg_err("chip not specified!\n");
		return -EINVAL;
	}

	if (IS_ERR_OR_NULL(chip->pinctrl) ||
	    IS_ERR_OR_NULL(chip->pmos_active) ||
	    IS_ERR_OR_NULL(chip->pmos_sleep)) {
		chg_err("pmos pinctrl error\n");
		return -EINVAL;
	}

	if (gpio_get_value(chip->pmos_gpio))
		*enable = true;
	else
		*enable = false;

	return 0;
}

static int oplus_sy6603_set_pmos_enable(struct oplus_chg_ic_dev *ic_dev, bool enable)
{
	int rc;
	struct chip_sy6603 *chip;
	bool curr_enable_status;
	bool change = false;

	if (ic_dev == NULL) {
		chg_err("ic_dev is NULL");
		return -ENODEV;
	}

	chip = oplus_chg_ic_get_drvdata(ic_dev);
	if (!chip) {
		chg_err("chip not specified!\n");
		return -EINVAL;
	}

	if (IS_ERR_OR_NULL(chip->pinctrl) ||
	    IS_ERR_OR_NULL(chip->pmos_active) ||
	    IS_ERR_OR_NULL(chip->pmos_sleep)) {
		chg_err("pmos pinctrl error\n");
		return -EINVAL;
	}

	if (oplus_is_rf_ftm_mode())
		enable = false;

	curr_enable_status = (bool)(!!gpio_get_value(chip->pmos_gpio));
	if (curr_enable_status != enable)
		change = true;
	mutex_lock(&chip->pinctrl_lock);
	rc = pinctrl_select_state(chip->pinctrl,
			enable ? chip->pmos_active : chip->pmos_sleep);
	mutex_unlock(&chip->pinctrl_lock);
	if (rc < 0) {
		chg_err("can't %s\n", enable ? "enable" : "disable");
	} else {
		rc = 0;
		if (change)
			chg_info("set value:%d, gpio_val:%d\n", enable, gpio_get_value(chip->pmos_gpio));
	}

	return rc;
}

static void __sy6603_enable_irq(struct chip_sy6603 *chip, bool en)
{
	if (chip->irq_enabled && !en) {
		chip->irq_enabled = false;
		disable_irq(chip->interrupt_irq);
		chg_info("irq_disabled");
	} else if (!chip->irq_enabled && en) {
		chip->irq_enabled = true;
		enable_irq(chip->interrupt_irq);
		chg_info("irq_enabled");
	} else {
		chg_info("irq_enabled_status:%s", true_or_false_str(chip->irq_enabled));
	}
}

static int __sy6603_set_hw_enable(struct chip_sy6603 *chip, bool enable)
{
	int rc;
	bool curr_enable_status;
	bool change = false;

	if (IS_ERR_OR_NULL(chip->pinctrl) ||
	    IS_ERR_OR_NULL(chip->en_active) ||
	    IS_ERR_OR_NULL(chip->en_sleep)) {
		chg_err("en pinctrl error\n");
		return -ENODEV;
	}

	if (chip->disbale_bal)
		enable = false;

	if (atomic_read(&chip->suspended) == 1 && enable) {
		chg_err("sy6603 is suspend\n");
		return -EINVAL;
	} else {
		mutex_lock(&chip->pinctrl_lock);
		curr_enable_status = (bool)(!!gpio_get_value(chip->en_gpio));
		if (curr_enable_status != enable)
			change = true;
		if (change)
			__sy6603_enable_irq(chip, false);

		rc = pinctrl_select_state(chip->pinctrl,
			enable ? chip->en_active : chip->en_sleep);
		mutex_unlock(&chip->pinctrl_lock);
	}
	if (rc < 0) {
		chg_err("can't %s\n", enable ? "enable" : "disable");
	} else {
		rc = 0;
		if (change) {
			msleep(2);
			chg_info("set value:%d, gpio_val:%d\n", enable, gpio_get_value(chip->en_gpio));
		}
	}

	return rc;
}

static int oplus_sy6603_set_hw_enable(struct oplus_chg_ic_dev *ic_dev, bool enable)
{
	int rc;
	struct chip_sy6603 *chip;

	if (ic_dev == NULL) {
		chg_err("ic_dev is NULL");
		return -ENODEV;
	}

	chip = oplus_chg_ic_get_drvdata(ic_dev);
	if (!chip) {
		chg_err("chip not specified!\n");
		return -EINVAL;
	}

	rc = __sy6603_set_hw_enable(chip, enable);

	return rc;
}

static int oplus_sy6603_set_wdg_enable(struct chip_sy6603 *chip, bool enable)
{
	int rc;

	if (!chip) {
		chg_err("chip not specified!\n");
		return -EINVAL;
	}

	if (atomic_read(&chip->suspended) == 1) {
		chg_err("sy6603 is suspend\n");
		return -EINVAL;
	}

	if (enable)
		rc = sy6603_write_byte_mask(chip,
			SY6603_REG_ADDR_10H, SY6603_WDG_ENABLE_MASK,
			(0x00 << SY6603_WDG_ENABLE_SHIFT));
	else
		rc = sy6603_write_byte_mask(chip,
			SY6603_REG_ADDR_10H, SY6603_WDG_ENABLE_MASK,
			(0x01 << SY6603_WDG_ENABLE_SHIFT));

	if (enable) {
		schedule_delayed_work(&chip->wdg_reset_work, 0);
	} else {
		if (delayed_work_pending(&chip->wdg_reset_work))
			cancel_delayed_work_sync(&chip->wdg_reset_work);
	}

	return rc;
}

static int oplus_sy6603_set_wdg_time(struct chip_sy6603 *chip, int time)
{
	int rc;

	if (!chip) {
		chg_err("chip not specified!\n");
		return -EINVAL;
	}

	if (atomic_read(&chip->suspended) == 1) {
		chg_err("sy6603 is suspend\n");
		return -EINVAL;
	}

	rc = sy6603_write_byte_mask(chip,
			SY6603_REG_ADDR_10H, SY6603_WDG_TIMER_SET_MASK, time);

	return rc;
}

static int __sy6603_get_iref(struct chip_sy6603 *chip)
{
	int rc;
	u8 val;

	rc = sy6603_read_byte_mask(
		chip, SY6603_REG_ADDR_02H, SY6603_NORMAL_IREF_MASK, &val);
	if (rc < 0)
		return rc;

	if (val < SY6603_NORMAL_IREF_MID_THD)
		rc = val * SY6603_NORMAL_IREF_SM_STEP + SY6603_NORMAL_IREF_SM_STEP;
	else
		rc = (val - SY6603_NORMAL_IREF_MID_THD) * SY6603_NORMAL_IREF_STEP + SY6603_NORMAL_IREF_MID;
	chg_info("curr:%d\n", rc);

	return rc;
}

static int __sy6603_set_iref(struct chip_sy6603 *chip, int curr)
{
	int rc;
	int val;

	if (curr > SY6603_NORMAL_IREF_MAX)
		curr = SY6603_NORMAL_IREF_MAX;

	if (curr < chip->min_iref)
		curr = chip->min_iref;

	if (curr > SY6603_NORMAL_IREF_MID)
		val = (curr  - SY6603_NORMAL_IREF_MID) / SY6603_NORMAL_IREF_STEP +
			SY6603_NORMAL_IREF_MID_THD;
	else
		val = (curr  - SY6603_NORMAL_IREF_SM_STEP) / SY6603_NORMAL_IREF_SM_STEP;
	val <<= SY6603_NORMAL_IREF_SHIFT;
	rc = sy6603_write_byte_mask(
		chip, SY6603_REG_ADDR_02H, SY6603_NORMAL_IREF_MASK, (u8)val);
	chg_info("curr:%d, val:0x%x\n", curr, val);

	return rc;
}

static int __sy6603_step_adjust_iref(
		struct chip_sy6603 *chip, int curr_iref, int target_iref, int step)
{
	int rc;
	int iref_step = step;

	if (curr_iref > target_iref)
		step = -step;

	while (iref_step && abs(curr_iref - target_iref) > iref_step) {
		curr_iref += step;
		rc = __sy6603_set_iref(chip, curr_iref);
		if (rc < 0)
			return rc;
		usleep_range(1000, 1000);
	}

	curr_iref = target_iref;
	rc = __sy6603_set_iref(chip, curr_iref);

	return rc;
}

static int __sy6603_iref_and_conver_enable_init(struct chip_sy6603 *chip)
{
	int rc;
	int target_iref;

	target_iref = __sy6603_get_iref(chip);
	if (target_iref < 0)
		target_iref = SY6603_NORMAL_IREF_STEP;

	rc = __sy6603_set_iref(chip, SY6603_NORMAL_IREF_STEP);
	rc |= sy6603_write_byte_mask(chip,
		SY6603_REG_ADDR_00H, SY6603_CONVER_ENABLE_MASK,
		(0x01 << SY6603_CONVER_ENABLE_SHIFT));
	rc |= __sy6603_step_adjust_iref(chip, SY6603_NORMAL_IREF_STEP,
		target_iref, chip->iref_sw_step_adjust_max_amp);

	return rc;
}

static int __sy6603_set_conver_enable(struct chip_sy6603 *chip, bool enable)
{
	int rc;
	u8 temp;
	bool curr_enable;

	if (atomic_read(&chip->suspended) == 1) {
		chg_err("sy6603 is suspend\n");
		return -EINVAL;
	}

	rc = sy6603_read_byte_mask(chip,
		SY6603_REG_ADDR_00H, SY6603_CONVER_ENABLE_MASK, &temp);
	if (rc < 0)
		return -EINVAL;
	curr_enable = !!temp;
	chg_info("set enable=%d, curr_enable=%d\n", enable, curr_enable);
	if (curr_enable == enable) {
		return 0;
	} else if (enable) {
		rc = sy6603_write_byte_mask(chip, SY6603_REG_ADDR_00H,
			SY6603_FREQ_SET_MASK, (0x01 << SY6603_FREQ_SET_SHIFT));
		rc |= sy6603_write_byte(chip, SY6603_REG_ADDR_01H, 0x55); /* uvp 2V */
		rc |= sy6603_write_byte_mask(chip,
			SY6603_REG_ADDR_03H, SY6603_UVLO_ACTION_MASK,
			(0x03 << SY6603_UVLO_ACTION_SHIFT));
		rc |= sy6603_write_byte_mask(chip,
			SY6603_REG_ADDR_04H, SY6603_PEAK_CURRENT_MASK,
			(0x0a << SY6603_PEAK_CURRENT_SHIFT));
		sy6603_read_byte(chip, SY6603_REG_ADDR_0CH, &temp);
		sy6603_read_byte(chip, SY6603_REG_ADDR_10H, &temp);
		rc |= __sy6603_iref_and_conver_enable_init(chip);
		oplus_sy6603_set_wdg_time(chip, SY6603_WDG_TIMER_SET_10S);
		oplus_sy6603_set_wdg_enable(chip, true);
		chip->trigger_err_type = 0;
		__sy6603_enable_irq(chip, true);
	} else {
		rc = sy6603_write_byte_mask(chip,
			SY6603_REG_ADDR_00H, SY6603_CONVER_ENABLE_MASK,
			(0x00 << SY6603_CONVER_ENABLE_SHIFT));
		oplus_sy6603_set_wdg_enable(chip, false);
		__sy6603_enable_irq(chip, false);
	}

	sy6603_read_byte(chip, SY6603_REG_ADDR_00H, &temp);
	chg_info("update enable=0x%0x\n", temp);

	return rc;
}

static int oplus_sy6603_set_conver_enable(struct oplus_chg_ic_dev *ic_dev, bool enable)
{
	int rc;
	struct chip_sy6603 *chip;

	if (ic_dev == NULL) {
		chg_err("ic_dev is NULL");
		return -ENODEV;
	}

	chip = oplus_chg_ic_get_drvdata(ic_dev);
	if (!chip) {
		chg_err("chip not specified!\n");
		return -EINVAL;
	}

	rc = __sy6603_set_conver_enable(chip, enable);

	return rc;
}

static int __sy6603_set_vout(struct chip_sy6603 *chip, int vout)
{
	int rc;
	int val;

	if (atomic_read(&chip->suspended) == 1) {
		chg_err("sy6603 is suspend\n");
		return -EINVAL;
	}

	if (vout > SY6603_VOUT_MAX)
		vout = SY6603_VOUT_MAX;

	if (vout < SY6603_VOUT_THD)
		vout = SY6603_VOUT_THD;

	val = (vout  - SY6603_VOUT_THD) / SY6603_VOUT_STEP;
	val <<= SY6603_VOUT_SHIFT;
	rc = sy6603_write_byte_mask(
		chip, SY6603_REG_ADDR_04H, SY6603_VOUT_MASK, (u8)val);
	chg_info("vout:%d, val:0x%x\n", vout, val);

	return rc;
}

static int __sy6603_get_vout(struct chip_sy6603 *chip, int *vout)
{
	int rc;
	u8 val = 0;

	if (atomic_read(&chip->suspended) == 1) {
		chg_err("sy6603 is suspend\n");
		return -EINVAL;
	}

	rc = sy6603_read_byte_mask(
		chip, SY6603_REG_ADDR_04H, SY6603_VOUT_MASK, &val);
	if (rc < 0)
		return rc;

	*vout = val * SY6603_VOUT_STEP + SY6603_VOUT_THD;

	return 0;
}

static int oplus_sy6603_set_vout(struct oplus_chg_ic_dev *ic_dev, int vout)
{
	int rc;
	struct chip_sy6603 *chip;

	if (ic_dev == NULL) {
		chg_err("ic_dev is NULL");
		return -ENODEV;
	}

	chip = oplus_chg_ic_get_drvdata(ic_dev);
	if (!chip) {
		chg_err("chip not specified!\n");
		return -EINVAL;
	}

	rc = __sy6603_set_vout(chip, vout);

	return rc;
}

static int oplus_sy6603_set_iterm(struct oplus_chg_ic_dev *ic_dev, int iterm)
{
	int rc;
	int val;
	struct chip_sy6603 *chip;

	if (ic_dev == NULL) {
		chg_err("ic_dev is NULL");
		return -ENODEV;
	}

	chip = oplus_chg_ic_get_drvdata(ic_dev);
	if (!chip) {
		chg_err("chip not specified!\n");
		return -EINVAL;
	}

	if (atomic_read(&chip->suspended) == 1) {
		chg_err("sy6603 is suspend\n");
		return -EINVAL;
	}

	if (iterm > SY6603_ITERM_MAX)
		iterm = SY6603_ITERM_MAX;

	if (iterm < SY6603_ITERM_STEP)
		iterm = SY6603_ITERM_STEP;

	val = iterm / SY6603_ITERM_STEP;
	val -= 1;
	val <<= SY6603_ITERM_SHIFT;
	rc = sy6603_write_byte_mask(chip, SY6603_REG_ADDR_02H,
		SY6603_TERM_CURR_MASK, (u8)val);
	chg_info("iterm:%d, val:0x%x\n", iterm, val);

	return rc;
}

static int oplus_sy6603_set_iref(struct oplus_chg_ic_dev *ic_dev, int curr)
{
	int rc;
	u8 enable;
	int curr_iref;
	struct chip_sy6603 *chip;

	if (ic_dev == NULL) {
		chg_err("ic_dev is NULL");
		return -ENODEV;
	}

	chip = oplus_chg_ic_get_drvdata(ic_dev);
	if (!chip) {
		chg_err("chip not specified!\n");
		return -EINVAL;
	}

	if (atomic_read(&chip->suspended) == 1) {
		chg_err("sy6603 is suspend\n");
		return -EINVAL;
	}

	rc = sy6603_read_byte_mask(chip,
		SY6603_REG_ADDR_00H, SY6603_CONVER_ENABLE_MASK, &enable);
	if (rc < 0)
		return rc;

	curr_iref = __sy6603_get_iref(chip);
	if (enable && curr_iref > 0)
		rc = __sy6603_step_adjust_iref(chip, curr_iref, curr, chip->iref_sw_step_adjust_max_amp);
	else
		rc = __sy6603_set_iref(chip, curr);

	return rc;
}

static int oplus_sy6603_set_flow_dir(struct oplus_chg_ic_dev *ic_dev, int dir)
{
	int rc;
	u8 enable;
	u8 reg_dir;
	struct chip_sy6603 *chip;

	if (ic_dev == NULL) {
		chg_err("ic_dev is NULL");
		return -ENODEV;
	}

	chip = oplus_chg_ic_get_drvdata(ic_dev);
	if (!chip) {
		chg_err("chip not specified!\n");
		return -EINVAL;
	}

	if (atomic_read(&chip->suspended) == 1) {
		chg_err("sy6603 is suspend\n");
		return -EINVAL;
	}

	if (chip->main_gauge_equal_b1) {
		if (dir == B2_TO_B1)
			dir = B1_TO_B2;
		else if (dir == B1_TO_B2)
			dir = B2_TO_B1;
	}

	rc = sy6603_read_byte_mask(chip,
		SY6603_REG_ADDR_00H, SY6603_CONVER_ENABLE_MASK, &enable);
	if (rc < 0)
		return rc;

	rc = sy6603_read_byte_mask(chip,
		SY6603_REG_ADDR_00H, SY6603_ENERGY_FLOW_DIR_MASK, &reg_dir);
	if (rc < 0)
		return rc;
	if (reg_dir)
		reg_dir = B2_TO_B1;
	else
		reg_dir = B1_TO_B2;

	if (enable && dir != reg_dir) {
		chg_info("flow dir change, need turn off bal\n");
		rc = __sy6603_set_conver_enable(chip, false);
		rc |= sy6603_read_byte_mask(chip,
			SY6603_REG_ADDR_00H, SY6603_CONVER_ENABLE_MASK, &enable);
		if (rc < 0 || enable) {
			chg_info("disable batt bal error\n");
			return -EINVAL;
		}
	}

	chg_info("dir:%d\n", dir);
	if (dir == B2_TO_B1)
		rc = sy6603_write_byte_mask(chip, SY6603_REG_ADDR_00H,
			SY6603_ENERGY_FLOW_DIR_MASK, (1 << SY6603_ENERGY_FLOW_DIR_SHIFT));
	else
		rc = sy6603_write_byte_mask(chip, SY6603_REG_ADDR_00H,
			SY6603_ENERGY_FLOW_DIR_MASK, (0 << SY6603_ENERGY_FLOW_DIR_SHIFT));

	return rc;
}

static int __sy6603_reg_dump(struct chip_sy6603 *chip)
{
	int rc;
	int addr;
	char buf[256] = {0};
	char *s = buf;
	int index = 0;
	int en_gpio_val;
	u8 val[SY6603_REG_ADDR_MAX - SY6603_REG_ADDR_00H + 1];

	if (atomic_read(&chip->suspended) == 1) {
		chg_err("sy6603 is suspend\n");
		return -EINVAL;
	}

	chg_info("start\n");
	en_gpio_val = gpio_get_value(chip->en_gpio);
	if (!en_gpio_val) {
		if (chip->disbale_bal)
			return 0;
		else
			return -EINVAL;
	}

	rc = sy6603_read_data(chip, SY6603_REG_ADDR_00H, val,
		(SY6603_REG_ADDR_MAX- SY6603_REG_ADDR_00H));
	if (rc < 0)
		return rc;

	s += sprintf(s, "sy6603_regs:");
	for (addr = SY6603_REG_ADDR_00H; addr < SY6603_REG_ADDR_MAX; addr++) {
		if (addr == SY6603_REG_ADDR_0CH)
			continue;
		s += sprintf(s, "[0x%.2x,0x%.2x]", addr, val[index]);
		index++;
		if (index == sizeof(val))
			break;
	}
	s += sprintf(s, "\n");
	chg_info("%s \n", buf);

	return 0;
}

static int oplus_sy6603_reg_dump(struct oplus_chg_ic_dev *ic_dev)
{
	int rc;
	struct chip_sy6603 *chip;

	if (ic_dev == NULL) {
		chg_err("ic_dev is NULL");
		return -ENODEV;
	}

	chip = oplus_chg_ic_get_drvdata(ic_dev);
	if (!chip) {
		chg_err("chip not specified!\n");
		return -EINVAL;
	}

	rc = __sy6603_reg_dump(chip);

	return rc;
}

static void *oplus_chg_get_func(struct oplus_chg_ic_dev *ic_dev,
				enum oplus_chg_ic_func func_id)
{
	void *func = NULL;

	if (!ic_dev->online && (func_id != OPLUS_IC_FUNC_INIT) &&
	    (func_id != OPLUS_IC_FUNC_EXIT) &&
	    func_id != OPLUS_IC_FUNC_BAL_GET_PMOS_ENABLE &&
	    func_id != OPLUS_IC_FUNC_BAL_SET_PMOS_ENABLE) {
		chg_err("%s is offline\n", ic_dev->name);
		return NULL;
	}

	switch (func_id) {
	case OPLUS_IC_FUNC_INIT:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_INIT,
					       oplus_sy6603_init);
		break;
	case OPLUS_IC_FUNC_EXIT:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_EXIT,
					       oplus_sy6603_exit);
		break;
	case OPLUS_IC_FUNC_BAL_GET_ENABLE:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BAL_GET_ENABLE,
					       oplus_sy6603_get_enable);
		break;
	case OPLUS_IC_FUNC_BAL_GET_PMOS_ENABLE:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BAL_GET_PMOS_ENABLE,
					       oplus_sy6603_get_pmos_enable);
		break;
	case OPLUS_IC_FUNC_BAL_SET_PMOS_ENABLE:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BAL_SET_PMOS_ENABLE,
					       oplus_sy6603_set_pmos_enable);
		break;
	case OPLUS_IC_FUNC_BAL_SET_HW_ENABLE:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BAL_SET_HW_ENABLE,
					       oplus_sy6603_set_hw_enable);
		break;
	case OPLUS_IC_FUNC_BAL_SET_CONVER_ENABLE:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BAL_SET_CONVER_ENABLE,
					       oplus_sy6603_set_conver_enable);
		break;
	case OPLUS_IC_FUNC_BAL_SET_VOUT:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BAL_SET_VOUT,
					       oplus_sy6603_set_vout);
		break;
	case OPLUS_IC_FUNC_BAL_SET_ITERM:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BAL_SET_ITERM,
					       oplus_sy6603_set_iterm);
		break;
	case OPLUS_IC_FUNC_BAL_SET_IREF:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BAL_SET_IREF,
					       oplus_sy6603_set_iref);
		break;
	case OPLUS_IC_FUNC_BAL_SET_FLOW_DIR:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BAL_SET_FLOW_DIR,
					       oplus_sy6603_set_flow_dir);
		break;
	case OPLUS_IC_FUNC_REG_DUMP:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_REG_DUMP,
					       oplus_sy6603_reg_dump);
		break;
	default:
		chg_err("this func(=%d) is not supported\n", func_id);
		func = NULL;
		break;
	}

	return func;
}

static int sy6603_gpio_init(struct chip_sy6603 *chip)
{
	int rc = 0;
	struct device_node *node = chip->dev->of_node;

	chip->pinctrl = devm_pinctrl_get(chip->dev);
	if (IS_ERR_OR_NULL(chip->pinctrl)) {
		chg_err("get pinctrl fail\n");
		return -ENODEV;
	}

	chip->interrupt_gpio = of_get_named_gpio(node, "sy6603,int-gpio", 0);
	if (!gpio_is_valid(chip->interrupt_gpio)) {
		chg_err("int_gpio not specified\n");
		return -ENODEV;
	}
	rc = gpio_request(chip->interrupt_gpio, "int-gpio");
	if (rc < 0) {
		chg_err("int_gpio request error, rc=%d\n", rc);
		return rc;
	}
	chip->int_default = pinctrl_lookup_state(chip->pinctrl, "sy6603_int_gpio_default");
	if (IS_ERR_OR_NULL(chip->int_default)) {
		chg_err("get int_default fail\n");
		goto free_int_gpio;
	}
	gpio_direction_input(chip->interrupt_gpio);
	mutex_lock(&chip->pinctrl_lock);
	pinctrl_select_state(chip->pinctrl, chip->int_default);
	mutex_unlock(&chip->pinctrl_lock);

	chip->en_gpio = of_get_named_gpio(node, "sy6603,en-gpio", 0);
	if (!gpio_is_valid(chip->en_gpio)) {
		chg_err("en_gpio not specified\n");
		goto free_int_gpio;
	}
	rc = gpio_request(chip->en_gpio, "en-gpio");
	if (rc < 0) {
		chg_err("en_gpio request error, rc=%d\n", rc);
		goto free_en_gpio;
	}
	chip->en_active = pinctrl_lookup_state(chip->pinctrl, "sy6603_en_gpio_active");
	if (IS_ERR_OR_NULL(chip->en_active)) {
		chg_err("get sy6603_en_gpio_active fail\n");
		goto free_en_gpio;
	}
	chip->en_sleep = pinctrl_lookup_state(chip->pinctrl, "sy6603_en_gpio_sleep");
	if (IS_ERR_OR_NULL(chip->en_sleep)) {
		chg_err("get sy6603_en_gpio_sleep fail\n");
		goto free_en_gpio;
	}
	gpio_direction_output(chip->en_gpio, 0);
	mutex_lock(&chip->pinctrl_lock);
	pinctrl_select_state(chip->pinctrl, chip->en_sleep);
	mutex_unlock(&chip->pinctrl_lock);

	chip->pmos_gpio = of_get_named_gpio(node, "sy6603,pmos-en-gpio", 0);
	if (!gpio_is_valid(chip->pmos_gpio)) {
		chg_err("pmos_en_gpio not specified\n");
		goto free_en_gpio;
	}
	rc = gpio_request(chip->pmos_gpio, "pmos-en-gpio");
	if (rc < 0) {
		chg_err("pmos_en_gpio request error, rc=%d\n", rc);
		goto free_pmos_gpio;
	}
	chip->pmos_active = pinctrl_lookup_state(chip->pinctrl, "sy6603_pmos_en_gpio_active");
	if (IS_ERR_OR_NULL(chip->pmos_active)) {
		chg_err("get sy6603_pmos_en_gpio_active fail\n");
		goto free_pmos_gpio;
	}
	chip->pmos_sleep = pinctrl_lookup_state(chip->pinctrl, "sy6603_pmos_en_gpio_sleep");
	if (IS_ERR_OR_NULL(chip->pmos_sleep)) {
		chg_err("get sy6603_pmos_en_gpio_sleep fail\n");
		goto free_pmos_gpio;
	}
	gpio_direction_output(chip->pmos_gpio, 0);
	mutex_lock(&chip->pinctrl_lock);
	pinctrl_select_state(chip->pinctrl, chip->pmos_sleep);
	mutex_unlock(&chip->pinctrl_lock);

	return 0;

free_pmos_gpio:
	if (!gpio_is_valid(chip->pmos_gpio))
		gpio_free(chip->pmos_gpio);
free_en_gpio:
	if (!gpio_is_valid(chip->en_gpio))
		gpio_free(chip->en_gpio);
free_int_gpio:
	if (!gpio_is_valid(chip->interrupt_gpio))
		gpio_free(chip->interrupt_gpio);
	return -1;
}

static void oplus_sy6603_wdg_reset_work(struct work_struct *work)
{
	bool enable = false;
	struct delayed_work *dwork = to_delayed_work(work);
	struct chip_sy6603 *chip = container_of(dwork, struct chip_sy6603, wdg_reset_work);

	oplus_sy6603_get_enable(chip->ic_dev, &enable);
	chg_info("enable=%d\n", enable);
	if (!enable) {
		if (gpio_get_value(chip->en_gpio))
			oplus_sy6603_set_wdg_enable(chip, false);
	} else {
		oplus_sy6603_set_wdg_time(chip, SY6603_WDG_TIMER_SET_10S);
		schedule_delayed_work(&chip->wdg_reset_work, msecs_to_jiffies(6000));
	}
}

static int sy6603_hardware_init(struct chip_sy6603 *chip)
{
	int rc = 0;

	if (atomic_read(&chip->suspended) == 1) {
		chg_err("sy6603 ic is suspend\n");
		return -EINVAL;
	}

	INIT_DELAYED_WORK(&chip->wdg_reset_work, oplus_sy6603_wdg_reset_work);

	__sy6603_set_hw_enable(chip, true);
	rc = sy6603_write_byte_mask(chip, SY6603_REG_ADDR_02H,
		SY6603_TERM_CURR_MASK, (SY6603_TERM_CURR_25MA << SY6603_ITERM_SHIFT));
	rc |= sy6603_write_byte_mask(chip, SY6603_REG_ADDR_00H,
		SY6603_FREQ_SET_MASK, (0x01 << SY6603_FREQ_SET_SHIFT));
	rc |= sy6603_write_byte_mask(chip, SY6603_REG_ADDR_10H,
		SY6603_WDG_ENABLE_MASK, (0x01 << SY6603_WDG_ENABLE_SHIFT));
	rc |= sy6603_write_byte_mask(chip,
		SY6603_REG_ADDR_03H, SY6603_EOC_MASK, (0x01 << SY6603_EOC_SHIFT));
	rc |= __sy6603_set_conver_enable(chip, false);
	rc |= __sy6603_set_vout(chip, SY6603_SET_VOUT_MV(4700));
	rc |= __sy6603_reg_dump(chip);
	__sy6603_set_hw_enable(chip, false);

	return rc;
}

static bool sy6603_need_handle_handler(struct chip_sy6603 *chip, int err_type)
{
	if ((err_type == BATT_BAL_ERR_L1_PEAK_CURR_LIMIT && test_bit(BATT_BAL_ERR_L1_PEAK_CURR_LIMIT, &chip->trigger_err_type)) ||
	    (err_type == BATT_BAL_ERR_L2_PEAK_CURR_LIMIT && test_bit(BATT_BAL_ERR_L2_PEAK_CURR_LIMIT, &chip->trigger_err_type)) ||
	    (err_type == BATT_BAL_ERR_WDG_TIMEOUT && test_bit(BATT_BAL_ERR_WDG_TIMEOUT, &chip->trigger_err_type)) ||
	    (err_type == BATT_BAL_ERR_B1_OVP && test_bit(BATT_BAL_ERR_B1_OVP, &chip->trigger_err_type)) ||
	    (err_type == BATT_BAL_ERR_B2_OVP && test_bit(BATT_BAL_ERR_B2_OVP, &chip->trigger_err_type)) ||
	    (err_type == BATT_BAL_ERR_OT_WARM && test_bit(BATT_BAL_ERR_OT_WARM, &chip->trigger_err_type)) ||
	    (err_type == BATT_BAL_ERR_OT && test_bit(BATT_BAL_ERR_OT, &chip->trigger_err_type)) ||
	    (err_type == BATT_BAL_ERR_B1_UV && test_bit(BATT_BAL_ERR_B1_UV, &chip->trigger_err_type)) ||
	    (err_type == BATT_BAL_ERR_B2_UV && test_bit(BATT_BAL_ERR_B2_UV, &chip->trigger_err_type)))
		return false;

	return true;
}

static irqreturn_t sy6603_int_handler(int irq, void *dev_id)
{
	int vout = 0;
	int en_gpio_val;
	struct chip_sy6603 *chip = dev_id;
	u8 fault_data[2] = {0};
	int err_type = BATT_BAL_ERR_UNKNOW;
	char buf[MAX_STR_LEN] = {0};

	en_gpio_val = gpio_get_value(chip->en_gpio);
	if (!en_gpio_val) {
		chg_info("not enable, not handle");
		return IRQ_HANDLED;
	}

	__sy6603_get_vout(chip, &vout);
	if (vout <= SY6603_VOUT_LEGAL_MIN) {
		chg_info("vout default[%d], not handle\n", vout);
		return IRQ_HANDLED;
	}

	sy6603_read_byte(chip, SY6603_REG_ADDR_0CH, &fault_data[0]);
	sy6603_read_byte(chip, SY6603_REG_ADDR_10H, &fault_data[1]);

	if (fault_data[1] & SY6603_WDG_TIMEOUT_MASK)
		err_type = BATT_BAL_ERR_WDG_TIMEOUT;
	else if (fault_data[0] & SY6603_B1_OVP_MASK)
		err_type = BATT_BAL_ERR_B1_OVP;
	else if (fault_data[0] & SY6603_B2_OVP_MASK)
		err_type = BATT_BAL_ERR_B2_OVP;
	else if (fault_data[0] & SY6603_OT_WARM_MASK)
		err_type = BATT_BAL_ERR_OT_WARM;
	else if (fault_data[0] & SY6603_OT_MASK)
		err_type = BATT_BAL_ERR_OT;
	else if (fault_data[0] & SY6603_B1_UV_MASK)
		err_type = BATT_BAL_ERR_B1_UV;
	else if (fault_data[0] & SY6603_B2_UV_MASK)
		err_type = BATT_BAL_ERR_B2_UV;
	else if (fault_data[1] & SY6603_L1_PEAK_CURRENT_MASK)
		err_type = BATT_BAL_ERR_L1_PEAK_CURR_LIMIT;
	else if (fault_data[1] & SY6603_L2_PEAK_CURRENT_MASK)
		err_type = BATT_BAL_ERR_L2_PEAK_CURR_LIMIT;

	chg_info("err_type=%d, reg[0x%x]=0x%x, reg[0x%x]=0x%x\n", err_type,
		SY6603_REG_ADDR_0CH, fault_data[0], SY6603_REG_ADDR_10H, fault_data[1]);
	if (err_type != BATT_BAL_ERR_UNKNOW) {
		if (!sy6603_need_handle_handler(chip, err_type))
			return IRQ_HANDLED;
		scnprintf(buf, sizeof(buf),
			"$$err_reason@@%s$$reg_info@@reg[0x%x]=0x%x, reg[0x%x]=0x%x", batt_bal_ic_exit_reason_str(err_type),
			SY6603_REG_ADDR_0CH, fault_data[0], SY6603_REG_ADDR_10H, fault_data[1]);
		sy6603_upload_track(chip, OPLUS_IC_ERR_BATT_BAL, err_type, buf);
		set_bit(err_type, &chip->trigger_err_type);
	}

	return IRQ_HANDLED;
}

static int sy6603_irq_init(struct chip_sy6603 *chip)
{
	int rc;

	chip->interrupt_irq = gpio_to_irq(chip->interrupt_gpio);

	rc = devm_request_threaded_irq(
		chip->dev, chip->interrupt_irq, NULL, sy6603_int_handler,
		IRQF_TRIGGER_RISING | IRQF_ONESHOT, "sy6603_int_irq", chip);
	if (rc < 0) {
		chg_err("int_irq request error, rc=%d\n", rc);
		return rc;
	}

	enable_irq_wake(chip->interrupt_irq);
	chip->irq_enabled = true;
	__sy6603_enable_irq(chip, false);
	return rc;
}

static int sy6603_parse_dt(struct chip_sy6603 *chip)
{
	int rc;
	struct device_node *node;

	if (!chip || !chip->dev) {
		chg_err("sy6603_dev null!\n");
		return-ENODEV;
	}

	node = chip->dev->of_node;

	chip->disbale_bal = of_property_read_bool(node, "oplus,disbale_bal");

	rc = of_property_read_u32(node, "sy6603,min-iref", &chip->min_iref);
	if (rc || chip->min_iref < SY6603_NORMAL_IREF_STEP)
		chip->min_iref = SY6603_NORMAL_IREF_STEP;

	chip->main_gauge_equal_b1 = of_property_read_bool(node, "oplus,main_gauge_equal_b1");

	rc = of_property_read_u32(node, "sy6603,iref_sw_step_adjust_max_amp", &chip->iref_sw_step_adjust_max_amp);
	if (rc)
		chip->iref_sw_step_adjust_max_amp = 200;

	return rc;
}

#if IS_ENABLED(CONFIG_OPLUS_CHG_TEST_KIT)
static bool test_kit_bal_ic_test(struct test_feature *feature, char *buf, size_t len)
{
	struct chip_sy6603 *chip;
	int index = 0;
	int rc = 0;
	u8 data;
	int rc_gpio = 0;

	if (buf == NULL) {
		chg_err("buf is NULL\n");
		return false;
	}
	if (feature == NULL) {
		chg_err("feature is NULL\n");
		index += snprintf(buf + index, len - index, "feature is NULL");
		return false;
	}

	chip = feature->private_data;

	if (atomic_read(&chip->suspended) == 1) {
		chg_err("sy6603 is suspend\n");
		return false;
	}

	if (IS_ERR_OR_NULL(chip->pinctrl) ||
	    IS_ERR_OR_NULL(chip->en_active) ||
	    IS_ERR_OR_NULL(chip->en_sleep)) {
		chg_err("en pinctrl error\n");
		return false;
	}

	mutex_lock(&chip->pinctrl_lock);
	rc_gpio = pinctrl_select_state(chip->pinctrl, chip->en_active);
	mutex_unlock(&chip->pinctrl_lock);
	if (rc_gpio < 0) {
		chg_err("can't enable\n");
		return false;
	} else {
		rc_gpio = 0;
		msleep(2);
		chg_err("set value:%d, gpio_val:%d\n", 1, gpio_get_value(chip->en_gpio));
	}

	rc = sy6603_read_byte(chip, SY6603_REG_ADDR_00H, &data);

	mutex_lock(&chip->pinctrl_lock);
	rc_gpio = pinctrl_select_state(chip->pinctrl, chip->en_sleep);
	mutex_unlock(&chip->pinctrl_lock);
	if (rc_gpio < 0) {
		chg_err("can't disable\n");
		return false;
	} else {
		rc_gpio = 0;
		msleep(2);
		chg_err("set value:%d, gpio_val:%d\n", 0, gpio_get_value(chip->en_gpio));
	}

	if (rc < 0)
		return false;
	else
		return true;
}

static const struct test_feature_cfg bal_ic_test_cfg = {
	.name = "bal_ic_test",
	.test_func = test_kit_bal_ic_test,
};
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 3, 0))
static int chip_sy6603_probe(struct i2c_client *client)
#else
static int chip_sy6603_probe(struct i2c_client *client, const struct i2c_device_id *id)
#endif
{
	struct oplus_chg_ic_cfg ic_cfg = { 0 };
	struct chip_sy6603 *chip;
	enum oplus_chg_ic_type ic_type;
	int ic_index;
	int rc;

	chip = devm_kzalloc(&client->dev, sizeof(struct chip_sy6603), GFP_KERNEL);
	if (!chip) {
		chg_err("failed to allocate memory\n");
		return -ENOMEM;
	}

	chip->regmap = devm_regmap_init_i2c(client, &sy6603_regmap_config);
	if (!chip->regmap) {
		rc = -ENODEV;
		goto regmap_init_err;
	}

	chip->dev = &client->dev;
	chip->client = client;
	i2c_set_clientdata(client, chip);
	mutex_init(&chip->i2c_lock);
	mutex_init(&chip->data_lock);
	mutex_init(&chip->pinctrl_lock);

	rc = sy6603_gpio_init(chip);
	if (rc < 0) {
		chg_err("gpio init error, rc=%d\n", rc);
		goto gpio_init_err;
	}

	sy6603_parse_dt(chip);

	rc = sy6603_hardware_init(chip);
	if (rc < 0) {
		chip->hw_init_err = true;
		chg_err("hardware init error, rc=%d\n", rc);
	}

	rc = sy6603_irq_init(chip);
	if (rc < 0)
		goto hw_init_err;

	rc = of_property_read_u32(chip->dev->of_node, "oplus,ic_type",
				  &ic_type);
	if (rc < 0) {
		chg_err("can't get ic type, rc=%d\n", rc);
		goto error;
	}
	rc = of_property_read_u32(chip->dev->of_node, "oplus,ic_index",
				  &ic_index);
	if (rc < 0) {
		chg_err("can't get ic index, rc=%d\n", rc);
		goto error;
	}
	ic_cfg.name = chip->dev->of_node->name;
	ic_cfg.index = ic_index;
	snprintf(ic_cfg.manu_name, OPLUS_CHG_IC_MANU_NAME_MAX - 1, "bal-sy6603:%d", ic_index);

#if IS_ENABLED(CONFIG_OPLUS_FEATURE_CHG_DEBUG_KIT)
	chip->odb = devm_oplus_device_bus_register(chip->dev, &sy6603_regmap_config, ic_cfg.manu_name);
	if (IS_ERR_OR_NULL(chip->odb)) {
		chg_err("register odb error\n");
		rc = -EFAULT;
		goto error;
	}
#endif /* CONFIG_OPLUS_FEATURE_CHG_DEBUG_KIT */

	snprintf(ic_cfg.fw_id, OPLUS_CHG_IC_FW_ID_MAX - 1, "0x00");
	ic_cfg.type = ic_type;
	ic_cfg.get_func = oplus_chg_get_func;
	ic_cfg.virq_data = sy6603_virq_table;
	ic_cfg.virq_num = ARRAY_SIZE(sy6603_virq_table);
	ic_cfg.of_node = chip->dev->of_node;
	chip->ic_dev = devm_oplus_chg_ic_register(chip->dev, &ic_cfg);
	if (!chip->ic_dev) {
		rc = -ENODEV;
		chg_err("register %s error\n", chip->dev->of_node->name);
		goto ic_reg_error;
	}
	oplus_sy6603_init(chip->ic_dev);
#if IS_ENABLED(CONFIG_OPLUS_CHG_TEST_KIT)
	chip->bal_ic_test = test_feature_register(&bal_ic_test_cfg, chip);
	if (IS_ERR_OR_NULL(chip->bal_ic_test))
		chg_err("bal ic test register error");
#endif
	chg_info("register %s\n", chip->dev->of_node->name);

	return 0;

ic_reg_error:
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_CHG_DEBUG_KIT)
	devm_oplus_device_bus_unregister(chip->odb);
#endif
error:
hw_init_err:
gpio_init_err:
regmap_init_err:
	devm_kfree(&client->dev, chip);
	return rc;
}

static int sy6603_pm_resume(struct device *dev_chip)
{
	struct i2c_client *client  = container_of(dev_chip, struct i2c_client, dev);
	struct chip_sy6603 *chip = i2c_get_clientdata(client);

	if(chip == NULL)
		return 0;

	atomic_set(&chip->suspended, 0);
	return 0;
}

static int sy6603_pm_suspend(struct device *dev_chip)
{
	struct i2c_client *client  = container_of(dev_chip, struct i2c_client, dev);
	struct chip_sy6603 *chip = i2c_get_clientdata(client);

	if(chip == NULL)
		return 0;

	atomic_set(&chip->suspended, 1);
	oplus_sy6603_set_hw_enable(chip->ic_dev, false);

	return 0;
}

static const struct dev_pm_ops sy6603_pm_ops = {
	.resume = sy6603_pm_resume,
	.suspend = sy6603_pm_suspend,
};

#if (LINUX_VERSION_CODE < KERNEL_VERSION(6, 0, 0))
static int chip_sy6603_remove(struct i2c_client *client)
#else
static void chip_sy6603_remove(struct i2c_client *client)
#endif
{
	struct chip_sy6603 *chip = i2c_get_clientdata(client);

	if(chip == NULL)
#if (LINUX_VERSION_CODE < KERNEL_VERSION(6, 0, 0))
		return -ENODEV;
#else
		return;
#endif

	if (!gpio_is_valid(chip->pmos_gpio))
		gpio_free(chip->pmos_gpio);
	if (!gpio_is_valid(chip->en_gpio))
		gpio_free(chip->en_gpio);
	disable_irq(chip->interrupt_irq);
	if (!gpio_is_valid(chip->interrupt_gpio))
		gpio_free(chip->interrupt_gpio);
	devm_oplus_chg_ic_unregister(&client->dev, chip->ic_dev);
	devm_kfree(&client->dev, chip);

#if (LINUX_VERSION_CODE < KERNEL_VERSION(6, 0, 0))
	return 0;
#endif
}

static void chip_sy6603_shutdown(struct i2c_client *chip_client)
{
}

static const struct of_device_id chip_sy6603_match_table[] = {
	{ .compatible = "oplus,sy6603-bal" },
	{},
};

static const struct i2c_device_id sy6603_id[] = {
	{"oplus,sy6603-bal", 0},
	{},
};

MODULE_DEVICE_TABLE(i2c, sy6603_id);

static struct i2c_driver sy6603_i2c_driver = {
	.driver		= {
		.name = "sy6603-bal",
		.owner	= THIS_MODULE,
		.of_match_table = chip_sy6603_match_table,
		.pm = &sy6603_pm_ops,
	},
	.probe		= chip_sy6603_probe,
	.remove		= chip_sy6603_remove,
	.id_table	= sy6603_id,
	.shutdown	= chip_sy6603_shutdown,
};

static __init int sy6603_driver_init(void)
{
	return i2c_add_driver(&sy6603_i2c_driver);
}

static __exit void sy6603_driver_exit(void)
{
	i2c_del_driver(&sy6603_i2c_driver);
}

oplus_chg_module_register(sy6603_driver);
MODULE_DESCRIPTION("Oplus sy6603 driver");
MODULE_LICENSE("GPL v2");

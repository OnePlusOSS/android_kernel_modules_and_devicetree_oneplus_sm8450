// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#define pr_fmt(fmt) "[OPLUS_SC7637]([%s][%d]): " fmt, __func__, __LINE__

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
#include <linux/mutex.h>
#include <linux/string.h>
#include <linux/rtc.h>
#include <linux/device.h>
#include <linux/module.h>
#include <oplus_chg_module.h>
#include <oplus_chg_ic.h>
#include <oplus_batt_bal.h>
#include "oplus_hal_sc7637.h"
#include "test-kit.h"
#include "../monitor/oplus_chg_track.h"
#include <oplus_chg_monitor.h>
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_CHG_DEBUG_KIT)
#include <debug-kit.h>
#endif

#define SC7637_I2C_ERR_MAX			2
#define REG_LENGTH				7
#define TRACK_UPLOAD_COUNT_MAX			10
#define TRACK_DEVICE_ABNORMAL_UPLOAD_PERIOD	(24 * 3600)

struct chip_sc7637 {
	struct i2c_client *client;
	struct device *dev;
	struct oplus_chg_ic_dev *ic_dev;

	int en_gpio;
	struct pinctrl *pinctrl;
	struct pinctrl_state *int_default;
	struct pinctrl_state *en_active;
	struct pinctrl_state *en_sleep;


	struct regmap *regmap;
	atomic_t suspended;
	struct mutex i2c_lock;
	struct mutex data_lock;

	atomic_t i2c_err_count;
	bool fpga_support;
	int reg_config[REG_LENGTH];
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_CHG_DEBUG_KIT)
	struct oplus_device_bus *odb;
#endif
#if IS_ENABLED(CONFIG_OPLUS_CHG_TEST_KIT)
	struct test_feature *fpga_level_shift_test;
#endif
	bool probe_err;
	unsigned long rst_ing;
};

enum rst_type {
	FPGA_RST = 1,
	I2C_RST,
};

static int sc7637_hardware_init(struct chip_sc7637 *chip);

struct oplus_chg_ic_virq sc7637_virq_table[] = {
	{ .virq_id = OPLUS_IC_VIRQ_ERR },
	{ .virq_id = OPLUS_IC_VIRQ_FPGA_RST },
};

static struct regmap_config sc7637_regmap_config = {
	.reg_bits	= 8,
	.val_bits	= 8,
	.max_register	= SC7637_REG_ADDR_MAX,
};

static __inline__ void sc7637_i2c_err_inc(struct chip_sc7637 *chip, u8 addr, bool read)
{
	static int upload_count = 0;
	static int pre_upload_time = 0;
	int curr_time;

	if (unlikely(!chip->ic_dev))
		return;

	if (unlikely(!chip->ic_dev->online))
		return;

	if (atomic_read(&chip->i2c_err_count) > SC7637_I2C_ERR_MAX)
		return;

	atomic_inc(&chip->i2c_err_count);
	curr_time = oplus_chg_track_get_local_time_s();
	if (curr_time - pre_upload_time > TRACK_DEVICE_ABNORMAL_UPLOAD_PERIOD)
		upload_count = 0;
	if (upload_count < TRACK_UPLOAD_COUNT_MAX) {
		upload_count++;
		pre_upload_time = oplus_chg_track_get_local_time_s();
		oplus_chg_ic_creat_err_msg(chip->ic_dev, OPLUS_IC_ERR_I2C, 0,
			"addr[0x%x] %s$$err_reason@@i2c_err", addr, read ? "read error" : "write error");
		oplus_chg_ic_virq_trigger(chip->ic_dev, OPLUS_IC_VIRQ_ERR);
	}
}

static __inline__ void sc7637_i2c_err_clr(struct chip_sc7637 *chip)
{
	if (unlikely(!chip->ic_dev))
		return;

	if (unlikely(!chip->ic_dev->online))
		return;

	if (atomic_inc_return(&chip->i2c_err_count) > SC7637_I2C_ERR_MAX)
		atomic_set(&chip->i2c_err_count, 0);
}

static int sc7637_read_byte(struct chip_sc7637 *chip, u8 addr, u8 *data)
{
	int rc;
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_CHG_DEBUG_KIT)
	unsigned int buf;
#endif

	if (chip->fpga_support && chip->rst_ing)
		return -EBUSY;
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
	sc7637_i2c_err_clr(chip);
	return 0;

error:
	mutex_unlock(&chip->i2c_lock);
	sc7637_i2c_err_inc(chip, addr, true);
	return rc;
}

__maybe_unused static int sc7637_read_data(
	struct chip_sc7637 *chip, u8 addr, u8 *buf, int len)
{
	int rc;

	if (chip->fpga_support && chip->rst_ing)
		return -EBUSY;
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
	sc7637_i2c_err_clr(chip);
	return 0;

error:
	mutex_unlock(&chip->i2c_lock);
	sc7637_i2c_err_inc(chip, addr, true);
	return rc;
}

__maybe_unused static int sc7637_write_byte(struct chip_sc7637 *chip, u8 addr, u8 data)
{
	int rc;

	if (chip->fpga_support && chip->rst_ing)
		return -EBUSY;
	mutex_lock(&chip->i2c_lock);
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_CHG_DEBUG_KIT)
	rc = oplus_dev_bus_write(chip->odb, addr, data);
#else
	rc = i2c_smbus_write_byte_data(chip->client, addr, data);
#endif
	if (rc < 0) {
		chg_err("write 0x%04x error, rc=%d\n", addr, rc);
		mutex_unlock(&chip->i2c_lock);
		sc7637_i2c_err_inc(chip, addr, false);
		rc = rc < 0 ? rc : -EIO;
		return rc;
	}
	mutex_unlock(&chip->i2c_lock);
	sc7637_i2c_err_clr(chip);

	return 0;
}

__maybe_unused static int sc7637_write_data(
	struct chip_sc7637 *chip, u8 addr, u8 *buf, int len)
{
	int rc;

	if (chip->fpga_support && chip->rst_ing)
		return -EBUSY;
	mutex_lock(&chip->i2c_lock);
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_CHG_DEBUG_KIT)
	rc = oplus_dev_bus_bulk_write(chip->odb, addr, buf, len);
#else
	rc = i2c_smbus_write_i2c_block_data(chip->client, addr, len, buf);
#endif
	if (rc < 0) {
		chg_err("write 0x%04x error, rc=%d\n", addr, rc);
		mutex_unlock(&chip->i2c_lock);
		sc7637_i2c_err_inc(chip, addr, false);
		rc = rc < 0 ? rc : -EIO;
		return rc;
	}
	mutex_unlock(&chip->i2c_lock);
	sc7637_i2c_err_clr(chip);

	return 0;
}

__maybe_unused static int sc7637_read_byte_mask(
	struct chip_sc7637 *chip, u8 addr, u8 mask, u8 *data)
{
	u8 temp;
	int rc;

	if (chip->fpga_support && chip->rst_ing)
		return -EBUSY;
	mutex_lock(&chip->data_lock);
	rc = sc7637_read_byte(chip, addr, &temp);
	if (rc < 0) {
		mutex_unlock(&chip->data_lock);
		sc7637_i2c_err_inc(chip, addr, true);
		return rc;
	}

	*data = mask & temp;
	mutex_unlock(&chip->data_lock);
	sc7637_i2c_err_clr(chip);

	return 0;
}

__maybe_unused static int sc7637_write_byte_mask(
	struct chip_sc7637 *chip, u8 addr, u8 mask, u8 data)
{
	u8 temp;
	int rc;

	if (chip->fpga_support && chip->rst_ing)
		return -EBUSY;
	mutex_lock(&chip->data_lock);
	rc = sc7637_read_byte(chip, addr, &temp);
	if (rc < 0) {
		mutex_unlock(&chip->data_lock);
		sc7637_i2c_err_inc(chip, addr, false);
		return rc;
	}
	temp = (data & mask) | (temp & (~mask));
	chg_info("addr=0x%x, temp=0x%x\n", addr, temp);
	rc = sc7637_write_byte(chip, addr, temp);
	mutex_unlock(&chip->data_lock);
	if (rc < 0)
		return rc;

	sc7637_i2c_err_clr(chip);
	return 0;
}

static int oplus_sc7637_init(struct oplus_chg_ic_dev *ic_dev)
{
	struct chip_sc7637 *chip;
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
	if (chip->probe_err)
		rc = sc7637_hardware_init(chip);

	if (rc < 0) {
		ic_dev->online = false;
		return -EINVAL;
	} else {
		chip->probe_err = false;
		ic_dev->online = true;
	}

	return 0;
}

static int oplus_sc7637_exit(struct oplus_chg_ic_dev *ic_dev)
{
	if (!ic_dev->online)
		return 0;

	ic_dev->online = false;

	return 0;
}

static int oplus_sc7637_get_enable(struct oplus_chg_ic_dev *ic_dev, bool *enable)
{
	u8 data;
	int en_gpio_val;
	struct chip_sc7637 *chip;

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
		chg_err("sc7637 is suspend\n");
		return -EINVAL;
	}

	en_gpio_val = gpio_get_value(chip->en_gpio);
	if (!en_gpio_val) {
		*enable = false;
		return 0;
	}

	sc7637_read_byte_mask(chip,
		SC7637_REG_ADDR_00H, SC7637_CONVER_ENABLE_MASK, &data);
	if (!!data)
		*enable = true;
	else
		*enable = false;

	return 0;
}

static int __sc7637_set_hw_enable(struct chip_sc7637 *chip, bool enable)
{
	int rc;

	if (atomic_read(&chip->suspended) == 1) {
		chg_err("sc7637 is suspend\n");
		return -EINVAL;
	}

	if (IS_ERR_OR_NULL(chip->pinctrl) ||
	    IS_ERR_OR_NULL(chip->en_active) ||
	    IS_ERR_OR_NULL(chip->en_sleep)) {
		chg_err("en pinctrl error\n");
		return -ENODEV;
	}

	rc = pinctrl_select_state(chip->pinctrl, chip->en_sleep);
	msleep(1);
	rc |= pinctrl_select_state(chip->pinctrl,
		enable ? chip->en_active : chip->en_sleep);
	if (rc < 0) {
		chg_err("can't %s\n", enable ? "enable" : "disable");
	} else {
		rc = 0;
		chg_err("set value:%d, gpio_val:%d\n", enable, gpio_get_value(chip->en_gpio));
		msleep(1);
	}

	return rc;
}

static int oplus_sc7637_set_hw_enable(struct oplus_chg_ic_dev *ic_dev, bool enable)
{
	int rc;
	struct chip_sc7637 *chip;

	if (ic_dev == NULL) {
		chg_err("ic_dev is NULL");
		return -ENODEV;
	}

	chip = oplus_chg_ic_get_drvdata(ic_dev);
	if (!chip) {
		chg_err("chip not specified!\n");
		return -EINVAL;
	}

	rc = __sc7637_set_hw_enable(chip, enable);

	return rc;
}

static int __sc7637_set_conver_enable(struct chip_sc7637 *chip, bool enable)
{
	int rc;
	u8 temp;

	if (atomic_read(&chip->suspended) == 1) {
		chg_err("sc7637 is suspend\n");
		return -EINVAL;
	}

	if (enable)
		rc = sc7637_write_byte(chip, SC7637_REG_ADDR_00H, 0x9f);
	else
		rc = sc7637_write_byte_mask(chip,
			SC7637_REG_ADDR_00H, SC7637_CONVER_ENABLE_MASK,
			(0x00 << SC7637_CONVER_ENABLE_SHIFT));

	sc7637_read_byte(chip, SC7637_REG_ADDR_00H, &temp);
	if (chip->fpga_support && enable && temp != 0x9f)
		rc = -EINVAL;
	chg_info("update enable=0x%0x, rc=%d\n", temp, rc);

	return rc;
}

static int oplus_sc7637_set_conver_enable(struct oplus_chg_ic_dev *ic_dev, bool enable)
{
	int rc;
	struct chip_sc7637 *chip;

	if (ic_dev == NULL) {
		chg_err("ic_dev is NULL");
		return -ENODEV;
	}

	chip = oplus_chg_ic_get_drvdata(ic_dev);
	if (!chip) {
		chg_err("chip not specified!\n");
		return -EINVAL;
	}

	rc = __sc7637_set_conver_enable(chip, enable);

	return rc;
}

static int __sc7637_reg_dump(struct chip_sc7637 *chip)
{
	int rc;
	int addr;
	char buf[256] = {0};
	char *s = buf;
	int index = 0;
	int en_gpio_val;
	u8 val[SC7637_REG_ADDR_MAX - SC7637_REG_ADDR_00H + 1];

	if (atomic_read(&chip->suspended) == 1) {
		chg_err("sc7637 is suspend\n");
		return -EINVAL;
	}

	chg_info("start\n");
	en_gpio_val = gpio_get_value(chip->en_gpio);
	if (!en_gpio_val)
		return -EINVAL;

	rc = sc7637_read_data(chip, SC7637_REG_ADDR_00H, val,
		(SC7637_REG_ADDR_MAX- SC7637_REG_ADDR_00H));
	if (rc < 0)
		return rc;

	s += sprintf(s, "sc7637_regs:");
	for (addr = SC7637_REG_ADDR_00H; addr < SC7637_REG_ADDR_MAX; addr++) {
		s += sprintf(s, "[0x%.2x,0x%.2x]", addr, val[index]);
		index++;
		if (index == sizeof(val))
			break;
	}
	s += sprintf(s, "\n");
	chg_info("%s \n", buf);

	return 0;
}

static int oplus_sc7637_reg_dump(struct oplus_chg_ic_dev *ic_dev)
{
	int rc;
	struct chip_sc7637 *chip;

	if (ic_dev == NULL) {
		chg_err("ic_dev is NULL");
		return -ENODEV;
	}

	chip = oplus_chg_ic_get_drvdata(ic_dev);
	if (!chip) {
		chg_err("chip not specified!\n");
		return -EINVAL;
	}

	rc = __sc7637_reg_dump(chip);

	return rc;
}

static int oplus_sc7637_rst_notify(struct oplus_chg_ic_dev *ic_dev, int type)
{
	int rc = 0;
	struct chip_sc7637 *chip;

	if (ic_dev == NULL) {
		chg_err("ic_dev is NULL");
		return -ENODEV;
	}

	chip = oplus_chg_ic_get_drvdata(ic_dev);
	if (!chip) {
		chg_err("chip not specified!\n");
		return -EINVAL;
	}
	switch (type) {
	case FPGA_RESET_START:
		set_bit(FPGA_RST, &chip->rst_ing);
		break;
	case FPGA_RESET_END:
		clear_bit(FPGA_RST, &chip->rst_ing);
		if (!chip->rst_ing) {
			rc = sc7637_hardware_init(chip);
			oplus_chg_ic_virq_trigger(chip->ic_dev, OPLUS_IC_VIRQ_FPGA_RST);
		}
		break;
	case GUAGE_I2C_RST_START:
		set_bit(I2C_RST, &chip->rst_ing);
		break;
	case GUAGE_I2C_RST_END:
		clear_bit(I2C_RST, &chip->rst_ing);
		if (!chip->rst_ing) {
			rc = sc7637_hardware_init(chip);
			oplus_chg_ic_virq_trigger(chip->ic_dev, OPLUS_IC_VIRQ_FPGA_RST);
		}
		break;
	default:
		break;
	}

	return rc;
}

#if IS_ENABLED(CONFIG_OPLUS_CHG_TEST_KIT)
static bool test_kit_fpga_level_shift_test(struct test_feature *feature, char *buf, size_t len)
{
	struct chip_sc7637 *chip;
	int index = 0;
	bool test_result = 0;
	int rc = 0;
	u8 data;

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

	rc = sc7637_read_byte_mask(chip,
		SC7637_REG_ADDR_00H, SC7637_CONVER_ENABLE_MASK, &data);
	if (!!data)
		test_result = true;
	else
		test_result = false;
	if (rc < 0)
		return false;
	else
		return test_result;
}

static const struct test_feature_cfg fpga_level_shift_test_cfg = {
	.name = "fpga_level_shift_test",
	.test_func = test_kit_fpga_level_shift_test,
};
#endif

static void *oplus_chg_get_func(struct oplus_chg_ic_dev *ic_dev,
				enum oplus_chg_ic_func func_id)
{
	void *func = NULL;

	if (!ic_dev->online && (func_id != OPLUS_IC_FUNC_INIT) &&
	    (func_id != OPLUS_IC_FUNC_EXIT)) {
		chg_err("%s is offline\n", ic_dev->name);
		return NULL;
	}

	switch (func_id) {
	case OPLUS_IC_FUNC_INIT:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_INIT,
					       oplus_sc7637_init);
		break;
	case OPLUS_IC_FUNC_EXIT:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_EXIT,
					       oplus_sc7637_exit);
		break;
	case OPLUS_IC_FUNC_BAL_GET_ENABLE:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BAL_GET_ENABLE,
					       oplus_sc7637_get_enable);
		break;
	case OPLUS_IC_FUNC_BAL_SET_HW_ENABLE:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BAL_SET_HW_ENABLE,
					       oplus_sc7637_set_hw_enable);
		break;
	case OPLUS_IC_FUNC_BAL_SET_CONVER_ENABLE:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BAL_SET_CONVER_ENABLE,
					       oplus_sc7637_set_conver_enable);
		break;
	case OPLUS_IC_FUNC_REG_DUMP:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_REG_DUMP,
					       oplus_sc7637_reg_dump);
		break;
	case OPLUS_IC_FUNC_BAL_HW_INIT:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BAL_HW_INIT,
					       oplus_sc7637_rst_notify);
		break;
	default:
		chg_err("this func(=%d) is not supported\n", func_id);
		func = NULL;
		break;
	}

	return func;
}

static int sc7637_gpio_init(struct chip_sc7637 *chip)
{
	int rc = 0;
	struct device_node *node = chip->dev->of_node;

	chip->pinctrl = devm_pinctrl_get(chip->dev);
	if (IS_ERR_OR_NULL(chip->pinctrl)) {
		chg_err("get pinctrl fail\n");
		return -ENODEV;
	}

	chip->en_gpio = of_get_named_gpio(node, "sc7637,en-gpio", 0);
	if (!gpio_is_valid(chip->en_gpio)) {
		chg_err("en_gpio not specified\n");
		return -ENODEV;
	}
	rc = gpio_request(chip->en_gpio, "sc7637-en-gpio");
	if (rc < 0) {
		chg_err("en_gpio request error, rc=%d\n", rc);
		return rc;
	}
	chip->en_active = pinctrl_lookup_state(chip->pinctrl, "sc7637_en_gpio_active");
	if (IS_ERR_OR_NULL(chip->en_active)) {
		chg_err("get sc7637_en_gpio_active fail\n");
		goto free_en_gpio;
	}
	chip->en_sleep = pinctrl_lookup_state(chip->pinctrl, "sc7637_en_gpio_sleep");
	if (IS_ERR_OR_NULL(chip->en_sleep)) {
		chg_err("get sc7637_en_gpio_sleep fail\n");
		goto free_en_gpio;
	}
	gpio_direction_output(chip->en_gpio, 0);
	pinctrl_select_state(chip->pinctrl, chip->en_sleep);

	return 0;

free_en_gpio:
	if (!gpio_is_valid(chip->en_gpio))
		gpio_free(chip->en_gpio);
	return -1;
}

static int sc7637_parse_dt(struct chip_sc7637 *chip)
{
	struct device_node *node = chip->dev->of_node;
	int rc;
	int i;
	int length;
	struct device_node *parent_node;

	parent_node = of_get_parent(node);
	if (parent_node) {
		chip->fpga_support = of_property_read_bool(parent_node, "oplus,fpga_support");
		if (chip->fpga_support)
			chg_info("fpga_support=%d\n", chip->fpga_support);
	} else {
		chip->fpga_support = 0;
	}

	rc = of_property_count_elems_of_size(node, "oplus,reg", sizeof(u32));
	if (rc < 0) {
		goto error;
	} else {
		if (rc != REG_LENGTH)
			goto error;
		length = rc;
		rc = of_property_read_u32_array(node, "oplus,reg", (u32 *)chip->reg_config,
						length);
		for (i = 0; i < length; i++)
			chg_info("oplus,reg %d\n", chip->reg_config[i]);
	}

	return 0;
error:
	chg_err("Count get oplus,reg, rc=%d\n", rc);
	chip->reg_config[2] = 0;
	chip->reg_config[3] = 0;
	chip->reg_config[4] = 0;
	chip->reg_config[5] = -1;
	chip->reg_config[6] = 0x14;

	return 0;
}


static int sc7637_hardware_init(struct chip_sc7637 *chip)
{
	int rc = 0;
	u8 temp;
	bool check_ok = true;
	int i;

	if (atomic_read(&chip->suspended) == 1) {
		chg_err("sc7637 ic is suspend\n");
		return -EINVAL;
	}

	__sc7637_set_hw_enable(chip, true);
	mdelay(5);

	for (i = SC7637_REG_ADDR_02H; i <= SC7637_REG_ADDR_06H; i++) {
		if (chip->reg_config[i] < 0)
			continue;
		rc |= sc7637_write_byte(chip, i, chip->reg_config[i]);
		if (chip->fpga_support) {
			rc |= sc7637_read_byte(chip, i, &temp);
			if (temp != chip->reg_config[i])
				check_ok &= false;
		}
	}
	rc |= __sc7637_set_conver_enable(chip, true);
	rc |= __sc7637_reg_dump(chip);

	if (!check_ok)
		return -EINVAL;

	return rc;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 3, 0))
static int chip_sc7637_probe(struct i2c_client *client)
#else
static int chip_sc7637_probe(struct i2c_client *client, const struct i2c_device_id *id)
#endif
{
	struct oplus_chg_ic_cfg ic_cfg = { 0 };
	struct chip_sc7637 *chip;
	enum oplus_chg_ic_type ic_type;
	int ic_index;
	int rc;

	chip = devm_kzalloc(&client->dev, sizeof(struct chip_sc7637), GFP_KERNEL);
	if (!chip) {
		chg_err("failed to allocate memory\n");
		return -ENOMEM;
	}

	chip->regmap = devm_regmap_init_i2c(client, &sc7637_regmap_config);
	if (!chip->regmap) {
		rc = -ENODEV;
		goto regmap_init_err;
	}

	chip->dev = &client->dev;
	chip->client = client;
	i2c_set_clientdata(client, chip);
	mutex_init(&chip->i2c_lock);
	mutex_init(&chip->data_lock);

	rc = sc7637_gpio_init(chip);
	if (rc < 0) {
		chg_err("gpio init error, rc=%d\n", rc);
		goto gpio_init_err;
	}
	sc7637_parse_dt(chip);
	chip->probe_err = false;
	rc = sc7637_hardware_init(chip);
	if (rc < 0) {
		chip->probe_err = true;
		chg_err("hardware init error, rc=%d\n", rc);
	}
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
	snprintf(ic_cfg.manu_name, OPLUS_CHG_IC_MANU_NAME_MAX - 1, "level_shift-sc7637:%d", ic_index);
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_CHG_DEBUG_KIT)
	chip->odb = devm_oplus_device_bus_register(chip->dev, &sc7637_regmap_config, ic_cfg.manu_name);
	if (IS_ERR_OR_NULL(chip->odb)) {
		chg_err("register odb error\n");
		rc = -EFAULT;
		goto error;
	}
#endif /* CONFIG_OPLUS_FEATURE_CHG_DEBUG_KIT */
	snprintf(ic_cfg.fw_id, OPLUS_CHG_IC_FW_ID_MAX - 1, "0x00");
	ic_cfg.type = ic_type;
	ic_cfg.get_func = oplus_chg_get_func;
	ic_cfg.virq_data = sc7637_virq_table;
	ic_cfg.virq_num = ARRAY_SIZE(sc7637_virq_table);
	ic_cfg.of_node = chip->dev->of_node;
	chip->ic_dev = devm_oplus_chg_ic_register(chip->dev, &ic_cfg);
	if (!chip->ic_dev) {
		rc = -ENODEV;
		chg_err("register %s error\n", chip->dev->of_node->name);
		goto ic_reg_error;
	}

	oplus_sc7637_init(chip->ic_dev);
	chg_info("register %s\n", chip->dev->of_node->name);
#if IS_ENABLED(CONFIG_OPLUS_CHG_TEST_KIT)
	if (chip->fpga_support) {
		chip->fpga_level_shift_test = test_feature_register(&fpga_level_shift_test_cfg, chip);
		if (IS_ERR_OR_NULL(chip->fpga_level_shift_test))
			chg_err("fpga level shift register error");
	}
#endif

	return 0;

ic_reg_error:
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_CHG_DEBUG_KIT)
	devm_oplus_device_bus_unregister(chip->odb);
#endif
error:
gpio_init_err:
regmap_init_err:
	devm_kfree(&client->dev, chip);
	return rc;
}

static int sc7637_pm_resume(struct device *dev_chip)
{
	struct i2c_client *client  = container_of(dev_chip, struct i2c_client, dev);
	struct chip_sc7637 *chip = i2c_get_clientdata(client);

	if(chip == NULL)
		return 0;

	atomic_set(&chip->suspended, 0);
	return 0;
}

static int sc7637_pm_suspend(struct device *dev_chip)
{
	struct i2c_client *client  = container_of(dev_chip, struct i2c_client, dev);
	struct chip_sc7637 *chip = i2c_get_clientdata(client);

	if(chip == NULL)
		return 0;

	atomic_set(&chip->suspended, 1);

	return 0;
}

static const struct dev_pm_ops sc7637_pm_ops = {
	.resume = sc7637_pm_resume,
	.suspend = sc7637_pm_suspend,
};

#if (LINUX_VERSION_CODE < KERNEL_VERSION(6, 0, 0))
static int chip_sc7637_remove(struct i2c_client *client)
#else
static void chip_sc7637_remove(struct i2c_client *client)
#endif
{
	struct chip_sc7637 *chip = i2c_get_clientdata(client);

	if(chip == NULL)
#if (LINUX_VERSION_CODE < KERNEL_VERSION(6, 0, 0))
		return -ENODEV;
#else
		return;
#endif

	if (!gpio_is_valid(chip->en_gpio))
		gpio_free(chip->en_gpio);
	devm_oplus_chg_ic_unregister(&client->dev, chip->ic_dev);
	devm_kfree(&client->dev, chip);

#if (LINUX_VERSION_CODE < KERNEL_VERSION(6, 0, 0))
	return 0;
#endif
}

static void chip_sc7637_shutdown(struct i2c_client *chip_client)
{
}

static const struct of_device_id chip_sc7637_match_table[] = {
	{ .compatible = "oplus,sc7637-l-shift" },
	{},
};

static const struct i2c_device_id sc7637_id[] = {
	{"oplus,sc7637-l-shift", 0},
	{},
};

MODULE_DEVICE_TABLE(i2c, sc7637_id);

static struct i2c_driver sc7637_i2c_driver = {
	.driver		= {
		.name = "sc7637-l-shift",
		.owner	= THIS_MODULE,
		.of_match_table = chip_sc7637_match_table,
		.pm = &sc7637_pm_ops,
	},
	.probe		= chip_sc7637_probe,
	.remove		= chip_sc7637_remove,
	.id_table	= sc7637_id,
	.shutdown	= chip_sc7637_shutdown,
};

static __init int sc7637_driver_init(void)
{
	return i2c_add_driver(&sc7637_i2c_driver);
}

static __exit void sc7637_driver_exit(void)
{
	i2c_del_driver(&sc7637_i2c_driver);
}

oplus_chg_module_early_register(sc7637_driver);
MODULE_DESCRIPTION("Oplus sc7637 driver");
MODULE_LICENSE("GPL v2");

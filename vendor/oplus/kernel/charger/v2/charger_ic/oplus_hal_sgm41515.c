// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021-2025 Oplus. All rights reserved.
 */

#define pr_fmt(fmt) "[SGM41515]([%s][%d]): " fmt, __func__, __LINE__

#include <linux/i2c.h>
#include <linux/version.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/power_supply.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/bitops.h>
#include <linux/mutex.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/pinctrl/consumer.h>
#include <soc/oplus/device_info.h>
#include <soc/oplus/system/boot_mode.h>

#include <oplus_chg_module.h>
#include <oplus_chg_ic.h>
#include <oplus_mms.h>
#include <oplus_mms_wired.h>
#include "oplus_hal_sgm41515.h"

#ifdef CONFIG_OPLUS_CHARGER_MTK
#include <mtk_boot_common.h>
#include "charger_class.h"
#endif

#ifndef I2C_ERR_MAX
#define I2C_ERR_MAX 2
#endif

#define BC12_TIMEOUT_MS		msecs_to_jiffies(5000)

#define REG_MAX 0x0b

static atomic_t i2c_err_count;

struct sgm41515_chip {
	struct i2c_client *client;
	struct device *dev;
	struct oplus_chg_ic_dev *ic_dev;
#ifdef CONFIG_OPLUS_CHARGER_MTK
	const char *chg_dev_name;
	struct charger_device *chg_dev;
	struct power_supply *psy;
	struct power_supply *chg_psy;
	struct power_supply_desc psy_desc;
#endif
	struct mutex pinctrl_lock;
	struct pinctrl *pinctrl;
	struct pinctrl_state *event_default;
	struct pinctrl_state *dis_vbus_active;
	struct pinctrl_state *dis_vbus_sleep;
	struct mutex i2c_lock;
	struct regmap *regmap;

	struct mutex dpdm_lock;
	struct regulator *dpdm_reg;
	struct wakeup_source *suspend_ws;
	struct wakeup_source *keep_resume_ws;
	wait_queue_head_t wait;

	struct delayed_work event_work;
	struct delayed_work bc12_timeout_work;
	struct delayed_work bc12_retry_work;
	struct oplus_mms *wired_topic;
	struct mms_subscribe *wired_subs;

	int event_gpio;
	int event_irq;
	int dis_vbus_gpio;

	atomic_t charger_suspended;
	atomic_t is_suspended;

	bool vbus_present;
	bool bc12_retry;
	bool otg_mode;
	bool auto_bc12;
	bool bc12_complete;
	bool event_irq_enabled;
	bool power_good;
	int charge_type;
	int bc12_delay_cnt;
	int hw_aicl_point;
	int sw_aicl_point;
	int part_id;
	int bc12_retried;
};

enum {
	CHARGE_TYPE_NO_INPUT = 0,
	CHARGE_TYPE_SDP,
	CHARGE_TYPE_CDP,
	CHARGE_TYPE_DCP,
	CHARGE_TYPE_VBUS_TYPE_UNKNOWN = 5,
	CHARGE_TYPE_OCP,
	CHARGE_TYPE_OTG,
};

static int sgm41515_hw_init(struct sgm41515_chip *chip);
static void sgm41515_get_bc12(struct sgm41515_chip *chip);
static int sgm41515_set_wdt_timer(struct sgm41515_chip *chip, int reg);
#ifdef CONFIG_OPLUS_CHARGER_MTK
extern void Charger_Detect_Init(void);
extern void Charger_Detect_Release(void);
extern void oplus_chg_pullup_dp_set(bool is_on);
#endif

static struct regmap_config sgm41515_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = REG_MAX,
};

static int sgm41515_reg_dump(struct oplus_chg_ic_dev *ic_dev);

static __inline__ void sgm41515_i2c_err_inc(struct sgm41515_chip *chip)
{
	if (atomic_inc_return(&i2c_err_count) > I2C_ERR_MAX) {
		atomic_set(&i2c_err_count, 0);
		oplus_chg_ic_creat_err_msg(chip->ic_dev, OPLUS_IC_ERR_I2C, 0,
					   "continuous error");
		oplus_chg_ic_virq_trigger(chip->ic_dev, OPLUS_IC_VIRQ_ERR);
	}
}

static __inline__ void sgm41515_i2c_err_clr(void)
{
	atomic_set(&i2c_err_count, 0);
}

static void sgm41515_enable_irq(struct sgm41515_chip *chip, bool en)
{
	if (chip->event_irq_enabled && !en) {
		chip->event_irq_enabled = false;
		disable_irq(chip->event_irq);
	} else if (!chip->event_irq_enabled && en) {
		chip->event_irq_enabled = true;
		enable_irq(chip->event_irq);
	} else {
		chg_info("event_irq_enabled:%s, en:%s\n",
			 true_or_false_str(chip->event_irq_enabled),
			 true_or_false_str(chip->event_irq));
	}
}

static int sgm41515_read_byte(struct sgm41515_chip *chip, u8 addr, u8 *data)
{
	int rc;
	bool is_err = false;
	int retry = 3;

	mutex_lock(&chip->i2c_lock);
	do {
		if (is_err)
			usleep_range(5000, 5000);

		rc = i2c_master_send(chip->client, &addr, 1);
		if (rc < 1) {
			chg_err("read 0x%02x error, rc=%d\n", addr, rc);
			rc = rc < 0 ? rc : -EIO;
			is_err = true;
			continue;
		}

		rc = i2c_master_recv(chip->client, data, 1);
		if (rc < 1) {
			chg_err("read 0x%02x error, rc=%d\n", addr, rc);
			rc = rc < 0 ? rc : -EIO;
			is_err = true;
			continue;
		}
		is_err = false;
	} while (is_err && retry--);

	if (is_err)
		goto error;

	mutex_unlock(&chip->i2c_lock);
	sgm41515_i2c_err_clr();
	return 0;

error:
	mutex_unlock(&chip->i2c_lock);
	sgm41515_i2c_err_inc(chip);
	return rc;
}

__maybe_unused static int sgm41515_read_data(struct sgm41515_chip *chip,
					     u8 addr, u8 *buf, int len)
{
	int rc;
	bool is_err = false;
	int retry = 3;

	mutex_lock(&chip->i2c_lock);
	do {
		if (is_err)
			usleep_range(5000, 5000);

		rc = i2c_master_send(chip->client, &addr, 1);
		if (rc < 1) {
			chg_err("read 0x%02x error, rc=%d\n", addr, rc);
			rc = rc < 0 ? rc : -EIO;
			is_err = true;
			continue;
		}

		rc = i2c_master_recv(chip->client, buf, len);
		if (rc < len) {
			chg_err("read 0x%02x error, rc=%d\n", addr, rc);
			rc = rc < 0 ? rc : -EIO;
			is_err = true;
			continue;
		}
		is_err = false;
	} while (is_err && retry--);

	if (is_err)
		goto error;

	mutex_unlock(&chip->i2c_lock);
	sgm41515_i2c_err_clr();
	return 0;

error:
	mutex_unlock(&chip->i2c_lock);
	sgm41515_i2c_err_inc(chip);
	return rc;
}

static int sgm41515_write_byte(struct sgm41515_chip *chip, u8 addr, u8 data)
{
	u8 buf_temp[2] = { addr, data };
	int rc;
	bool is_err = false;
	int retry = 3;

	mutex_lock(&chip->i2c_lock);
	do {
		if (is_err)
			usleep_range(5000, 5000);

		rc = i2c_master_send(chip->client, buf_temp, 2);
		if (rc < 2) {
			chg_err("write 0x%02x error, rc=%d\n", addr, rc);
			rc = rc < 0 ? rc : -EIO;
			is_err = true;
			continue;
		}
		is_err = false;
	} while (is_err && retry--);

	if (is_err)
		goto error;

	mutex_unlock(&chip->i2c_lock);
	sgm41515_i2c_err_clr();
	return 0;

error:
	mutex_unlock(&chip->i2c_lock);
	sgm41515_i2c_err_inc(chip);
	return rc;
}

__maybe_unused static int sgm41515_write_data(struct sgm41515_chip *chip,
					      u8 addr, u8 *buf, int len)
{
	u8 *buf_temp;
	int i;
	int rc;
	bool is_err = false;
	int retry = 3;

	buf_temp = kzalloc(len + 1, GFP_KERNEL);
	if (!buf_temp) {
		chg_err("alloc memary error\n");
		return -ENOMEM;
	}

	buf_temp[0] = addr;
	for (i = 0; i < len; i++)
		buf_temp[i + 1] = buf[i];

	mutex_lock(&chip->i2c_lock);
	do {
		if (is_err)
			usleep_range(5000, 5000);

		rc = i2c_master_send(chip->client, buf_temp, len + 1);
		if (rc < (len + 1)) {
			chg_err("write 0x%02x error, rc=%d\n", addr, rc);
			rc = rc < 0 ? rc : -EIO;
			is_err = true;
			continue;
		}
		is_err = false;
	} while (is_err && retry--);

	if (is_err)
		goto error;

	mutex_unlock(&chip->i2c_lock);
	kfree(buf_temp);
	sgm41515_i2c_err_clr();
	return 0;

error:
	mutex_unlock(&chip->i2c_lock);
	kfree(buf_temp);
	sgm41515_i2c_err_inc(chip);
	return rc;
}

__maybe_unused static int sgm41515_read_byte_mask(struct sgm41515_chip *chip,
						  u8 addr, u8 mask, u8 *data)
{
	u8 temp;
	int rc;

	rc = sgm41515_read_byte(chip, addr, &temp);
	if (rc < 0)
		return rc;

	*data = mask & temp;

	return 0;
}

__maybe_unused static int sgm41515_write_byte_mask(struct sgm41515_chip *chip,
						   u8 addr, u8 mask, u8 data)
{
	u8 temp;
	int rc;

	rc = sgm41515_read_byte(chip, addr, &temp);
	if (rc < 0)
		return rc;
	temp = (data & mask) | (temp & (~mask));
	rc = sgm41515_write_byte(chip, addr, temp);
	if (rc < 0)
		return rc;

	return 0;
}

static int sgm41515_request_dpdm(struct sgm41515_chip *chip, bool enable)
{
	int rc = 0;

#ifdef CONFIG_OPLUS_CHARGER_MTK
	if (enable) {
		if (get_boot_mode() != META_BOOT)
			Charger_Detect_Init();
	} else {
		Charger_Detect_Release();
		oplus_chg_pullup_dp_set(false);
	}
#else
	/* fetch the DPDM regulator */
	if (!chip->dpdm_reg &&
	    of_get_property(chip->dev->of_node, "dpdm-supply", NULL)) {
		chip->dpdm_reg = devm_regulator_get_optional(chip->dev, "dpdm");
		if (IS_ERR(chip->dpdm_reg)) {
			rc = PTR_ERR(chip->dpdm_reg);
			chg_err("Couldn't get dpdm regulator, rc=%d\n", rc);
			chip->dpdm_reg = NULL;
			return rc;
		}
	}

	mutex_lock(&chip->dpdm_lock);
	if (enable) {
		if (chip->dpdm_reg) {
			chg_info("enabling DPDM regulator\n");
			rc = regulator_enable(chip->dpdm_reg);
			if (rc < 0)
				chg_err("Couldn't enable dpdm regulator rc=%d\n", rc);
		}
	} else {
		if (chip->dpdm_reg) {
			chg_err("disabling DPDM regulator\n");
			rc = regulator_disable(chip->dpdm_reg);
			if (rc < 0)
				chg_err("Couldn't disable dpdm regulator rc=%d\n", rc);
		}
	}
	mutex_unlock(&chip->dpdm_lock);
#endif
	return rc;
}

static int sgm41515_enable_hiz_mode(struct sgm41515_chip *chip, bool en)
{
	int rc;

	rc = sgm41515_write_byte_mask(chip, REG00_SGM41515_ADDRESS, REG00_SGM41515_HIZ_MODE_MASK,
				      en ? REG00_SGM41515_HIZ_MODE_ENABLE : REG00_SGM41515_HIZ_MODE_DISABLE);
	if (rc < 0) {
		chg_err("can't %s hiz mode, rc=%d\n",
			en ? "enable" : "disable", rc);
		return rc;
	}

	cancel_delayed_work_sync(&chip->bc12_timeout_work);
	if (!en)
		schedule_delayed_work(&chip->bc12_timeout_work,
				      BC12_TIMEOUT_MS);

	return 0;
}

static void sgm41515_bc12_boot_check(struct sgm41515_chip *chip)
{
	u8 data;
	int rc;

	/* set vindpm thr to 4V */
	rc = sgm41515_write_byte_mask(chip, REG06_SGM41515_ADDRESS,
						REG06_SGM41515_VINDPM_MASK, 0x01);
	if (rc < 0)
		chg_err("set vindpm thr error, rc=%d\n", rc);
	rc = sgm41515_write_byte_mask(
		chip, REG0A_SGM41515_ADDRESS, REG0A_SGM41515_VINDPM_INT_MASK | REG0A_SGM41515_IINDPM_INT_MASK,
		REG0A_SGM41515_VINDPM_INT_NOT_ALLOW | REG0A_SGM41515_IINDPM_INT_NOT_ALLOW);
	if (rc < 0)
		chg_err("disable vindpm & iindpm interrupt error, rc=%d\n", rc);

	rc = sgm41515_write_byte_mask(chip, REG05_SGM41515_ADDRESS,
				      REG05_SGM41515_WATCHDOG_TIMER_MASK, 0x00);
	if (rc < 0)
		chg_err("disable watchdog error, rc=%d\n", rc);

	rc = sgm41515_write_byte_mask(chip, REG01_SGM41515_ADDRESS, REG01_SGM41515_CHARGING_MASK,
				      REG01_SGM41515_CHARGING_DISABLE);
	if (rc < 0)
		chg_err("disable charge error, rc=%d\n", rc);

	rc = sgm41515_read_byte_mask(chip, REG08_SGM41515_ADDRESS, REG08_SGM41515_VBUS_STAT_MASK,
				     &data);
	if (rc < 0) {
		chg_err("can't read charge type, rc=%d\n", rc);
		data = 0;
	}
	sgm41515_enable_irq(chip, true);

	/* BC1.2 result bit is bit5-7*/
	data = data >> 5;
	chg_info("chg_type=%u\n", data);
	if (data == CHARGE_TYPE_CDP) {
		chg_info("bc1.2 result is no input\n");
		chip->charge_type = CHARGE_TYPE_CDP;
		chip->bc12_complete = true;
		oplus_chg_ic_virq_trigger(chip->ic_dev,
					  OPLUS_IC_VIRQ_BC12_COMPLETED);
		return;
	}

	chip->bc12_retry = false;
	WRITE_ONCE(chip->auto_bc12, false);
	rc = sgm41515_write_byte_mask(chip, REG07_SGM41515_ADDRESS, REG07_SGM41515_IINDET_EN_MASK,
				      REG07_SGM41515_IINDET_EN_FORCE_DET);
	if (rc < 0)
		chg_err("can't rerun bc1.2, rc=%d", rc);
}

static void sgm41515_bc12_timeout_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct sgm41515_chip *chip =
		container_of(dwork, struct sgm41515_chip, bc12_timeout_work);
	int rc;

	chg_err("BC1.2 check timeout\n");
	rc = sgm41515_write_byte_mask(chip, REG00_SGM41515_ADDRESS, REG00_SGM41515_HIZ_MODE_MASK,
				      REG00_SGM41515_HIZ_MODE_ENABLE);
	if (rc < 0)
		chg_err("can't enable hiz mode, rc=%d\n", rc);
}

static bool sgm41515_get_bus_gd(struct sgm41515_chip *chip)
{
	int rc = 0;
	u8 reg_val = 0;
	bool bus_gd = false;

	if (!chip)
		return 0;

	if (atomic_read(&chip->charger_suspended) == 1)
		return 0;

	rc = sgm41515_read_byte(chip, REG0A_SGM41515_ADDRESS, &reg_val);
	if (rc) {
		chg_err("Couldn't read regeister, rc = %d\n", rc);
		return false;
	}

	bus_gd = ((reg_val & REG0A_SGM41515_BUS_GD_MASK) == REG0A_SGM41515_BUS_GD_YES) ? 1 : 0;
	return bus_gd;
}

static __maybe_unused bool sgm41515_get_power_gd(struct sgm41515_chip *chip)
{
	int rc = 0;
	u8 reg_val = 0;
	bool power_gd = false;

	if (!chip)
		return 0;

	if(atomic_read(&chip->charger_suspended) == 1)
		return 0;

	rc = sgm41515_read_byte(chip, REG08_SGM41515_ADDRESS, &reg_val);
	if (rc) {
		chg_err("Couldn't get_power_gd rc = %d\n", rc);
		return false;
	}

	power_gd = ((reg_val & REG08_SGM41515_POWER_GOOD_STAT_MASK) == REG08_SGM41515_POWER_GOOD_STAT_GOOD) ? 1 : 0;
	return power_gd;
}

static void sgm41515_dump_registers(struct sgm41515_chip *chip)
{
	int ret = 0;
	int addr = 0;
	u8 val_buf[SGM41515_REG_NUMBER] = {0x0};

	if (!chip)
		return;

	if (atomic_read(&chip->charger_suspended) == 1)
		return;

	for(addr = SGM41515_FIRST_REG; addr <= SGM41515_LAST_REG; addr++) {
		ret = sgm41515_read_byte(chip, addr, &val_buf[addr]);
		if (ret)
			chg_err("Couldn't read 0x%02x ret = %d\n", addr, ret);
	}

	chg_err("[0x%02x, 0x%02x, 0x%02x, 0x%02x], [0x%02x, 0x%02x, 0x%02x, 0x%02x], "
			"[0x%02x, 0x%02x, 0x%02x, 0x%02x], [0x%02x, 0x%02x, 0x%02x, 0x%02x]\n",
			val_buf[0], val_buf[1], val_buf[2], val_buf[3],
			val_buf[4], val_buf[5], val_buf[6], val_buf[7],
			val_buf[8], val_buf[9], val_buf[10], val_buf[11],
			val_buf[12], val_buf[13], val_buf[14], val_buf[15]);
}

static u8 sgm41515_get_vbus_stat(struct sgm41515_chip *chip)
{
	int rc = 0;
	u8 vbus_stat = 0;

	if (!chip)
		return 0;

	if(atomic_read(&chip->charger_suspended) == 1)
		return 0;

	rc = sgm41515_read_byte(chip, REG08_SGM41515_ADDRESS, &vbus_stat);
	if (rc) {
		chg_err("Couldn't read REG08_SGM41515_ADDRESS rc = %d\n", rc);
		return 0;
	}

	vbus_stat = vbus_stat & REG08_SGM41515_VBUS_STAT_MASK;

	return vbus_stat;
}

static int sgm41515_set_iindet(struct sgm41515_chip *chip)
{
	int rc;

	if (!chip)
		return 0;

	if(atomic_read(&chip->charger_suspended) == 1)
		return 0;

	rc = sgm41515_write_byte_mask(chip, REG07_SGM41515_ADDRESS,
			REG07_SGM41515_IINDET_EN_MASK,
			REG07_SGM41515_IINDET_EN_FORCE_DET);
	if (rc < 0)
		chg_err("Couldn't set REG07_SGM41515_IINDET_EN_MASK rc = %d\n", rc);

	return rc;
}

static bool sgm41515_get_iindet(struct sgm41515_chip *chip)
{
	int rc = 0;
	u8 reg_val = 0;
	bool is_complete = false;

	if (!chip)
		return 0;

	if(atomic_read(&chip->charger_suspended) == 1)
		return 0;

	rc = sgm41515_read_byte(chip, REG07_SGM41515_ADDRESS, &reg_val);
	if (rc) {
		chg_err("Couldn't read REG07_SGM41515_ADDRESS rc = %d\n", rc);
		return false;
	}

	is_complete = ((reg_val & REG07_SGM41515_IINDET_EN_MASK) == REG07_SGM41515_IINDET_EN_DET_COMPLETE) ? 1 : 0;
	return is_complete;
}

#ifdef CONFIG_OPLUS_CHARGER_MTK
static int sgm41515_inform_charger_type(struct sgm41515_chip *chip)
{
	int ret = 0;
	union power_supply_propval propval = {0};

	if (!chip->psy) {
		chip->psy = power_supply_get_by_name("mtk-master-charger");
		if (IS_ERR_OR_NULL(chip->psy)) {
			chg_err("Couldn't get chip->psy");
			return -EINVAL;
		}
	}

	if (chip->power_good)
		propval.intval = 1;
	else
		propval.intval = 0;

	if (chip->psy) {
		chg_info("inform power supply online status:%d", propval.intval);
		ret = power_supply_set_property(chip->psy,
				POWER_SUPPLY_PROP_ONLINE, &propval);
		if (ret < 0)
			chg_err("inform power supply online failed:%d", ret);

		power_supply_changed(chip->psy);
	}

	if (chip->chg_psy)
		power_supply_changed(chip->chg_psy);
	return ret;
}
#endif

#define OPLUS_BC12_DELAY_CNT 	18
static void sgm41515_bc12_retry_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct sgm41515_chip *chip = container_of(dwork, struct sgm41515_chip, bc12_retry_work);

	if (!sgm41515_get_bus_gd(chip)) {
		chg_err("plugout during BC1.2, delay_cnt=%d,return\n", chip->bc12_delay_cnt);
		chip->bc12_delay_cnt = 0;
		return;
	}

	if (chip->bc12_delay_cnt >= OPLUS_BC12_DELAY_CNT) {
		chg_err("BC1.2 not complete delay_cnt to max\n");
		return;
	}
	chip->bc12_delay_cnt++;

	if (sgm41515_get_iindet(chip)) {
		chg_err("BC1.2 complete, delay_cnt=%d\n", chip->bc12_delay_cnt);
		sgm41515_get_bc12(chip);
		sgm41515_request_dpdm(chip, false);
	} else {
		chg_err("BC1.2 not complete delay 50ms,delay_cnt=%d\n", chip->bc12_delay_cnt);
		schedule_delayed_work(&chip->bc12_retry_work, round_jiffies_relative(msecs_to_jiffies(50)));
	}
}

static void sgm41515_start_bc12_retry(struct sgm41515_chip *chip)
{
	if (!chip)
		return;

	sgm41515_request_dpdm(chip, true);
	msleep(10);
	sgm41515_set_iindet(chip);

	schedule_delayed_work(&chip->bc12_retry_work, round_jiffies_relative(msecs_to_jiffies(100)));
}

#define OPLUS_BC12_RETRY_CNT 	1
static void sgm41515_get_bc12(struct sgm41515_chip *chip)
{
	u8 vbus_stat = 0;
	int charger_type = 0;

	if (!chip)
		return;

	if (!chip->bc12_complete) {
		vbus_stat = sgm41515_get_vbus_stat(chip);
		chg_err("vbus_stat=0x%x\n", vbus_stat);
		switch (vbus_stat) {
		case REG08_SGM41515_VBUS_STAT_SDP:
			if (chip->bc12_retried < OPLUS_BC12_RETRY_CNT) {
				chip->bc12_retried++;
				chg_err("bc1.2 sdp retry cnt=%d\n", chip->bc12_retried);
				sgm41515_start_bc12_retry(chip);
				break;
			}
			chip->bc12_complete = true;
			charger_type = POWER_SUPPLY_TYPE_USB;
			break;
		case REG08_SGM41515_VBUS_STAT_CDP:
			if (chip->bc12_retried < OPLUS_BC12_RETRY_CNT) {
				chip->bc12_retried++;
				chg_err("bc1.2 cdp retry cnt=%d\n", chip->bc12_retried);
				sgm41515_start_bc12_retry(chip);
				break;
			}
			chip->bc12_complete = true;
			charger_type = POWER_SUPPLY_TYPE_USB_CDP;
			break;
		case REG08_SGM41515_VBUS_STAT_DCP:
		case REG08_SGM41515_VBUS_STAT_OCP:
		case REG08_SGM41515_VBUS_STAT_FLOAT:
			if (chip->bc12_retried < OPLUS_BC12_RETRY_CNT) {
				chip->bc12_retried++;
				chg_err("bc1.2 dcp retry cnt=%d\n", chip->bc12_retried);
				sgm41515_start_bc12_retry(chip);
				break;
			}
			chip->bc12_complete = true;
			charger_type = POWER_SUPPLY_TYPE_USB_DCP;
			break;
		case REG08_SGM41515_VBUS_STAT_UNKNOWN:
			if (chip->bc12_retried < OPLUS_BC12_RETRY_CNT) {
				chip->bc12_retried++;
				chg_err("bc1.2 unknown retry cnt=%d\n", chip->bc12_retried);
				sgm41515_start_bc12_retry(chip);
				break;
			}
			break;
		case REG08_SGM41515_VBUS_STAT_OTG_MODE:
		default:
			chg_err("bc1.2 unknown\n");
			break;
		}

		if (chip->charge_type != charger_type) {
			chip->charge_type = charger_type;
			oplus_chg_ic_virq_trigger(chip->ic_dev, OPLUS_IC_VIRQ_CHG_TYPE_CHANGE);
		}

		if (chip->bc12_complete) {
#ifdef CONFIG_OPLUS_CHARGER_MTK
			sgm41515_inform_charger_type(chip);
#endif
			oplus_chg_ic_virq_trigger(chip->ic_dev, OPLUS_IC_VIRQ_BC12_COMPLETED);
		}
	}
}

static void oplus_chg_awake_init(struct sgm41515_chip *chip)
{
	if (!chip) {
		chg_err("chip is null\n");
		return;
	}
	chip->suspend_ws = NULL;
	chip->suspend_ws = wakeup_source_register(NULL, "split_chg_wakelock");
	return;
}

static void oplus_chg_wakelock(struct sgm41515_chip *chip, bool awake)
{
	static bool pm_flag = false;

	if (!chip || !chip->suspend_ws)
		return;

	if (awake && !pm_flag) {
		pm_flag = true;
		__pm_stay_awake(chip->suspend_ws);
		chg_info("true\n");
	} else if (!awake && pm_flag) {
		__pm_relax(chip->suspend_ws);
		pm_flag = false;
		chg_info("false\n");
	}
	return;
}

static void oplus_keep_resume_awake_init(struct sgm41515_chip *chip)
{
	if (!chip) {
		chg_err("chip is null\n");
		return;
	}
	chip->keep_resume_ws = NULL;
	chip->keep_resume_ws = wakeup_source_register(NULL, "split_chg_keep_resume");
	return;
}

static void oplus_keep_resume_wakelock(struct sgm41515_chip *chip, bool awake)
{
	static bool pm_flag = false;

	if (!chip || !chip->keep_resume_ws)
		return;

	if (awake && !pm_flag) {
		pm_flag = true;
		__pm_stay_awake(chip->keep_resume_ws);
		chg_info("true\n");
	} else if (!awake && pm_flag) {
		__pm_relax(chip->keep_resume_ws);
		pm_flag = false;
		chg_info("false\n");
	}
	return;
}

#define OPLUS_WAIT_RESUME_TIME	200
static void sgm41515_event_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct sgm41515_chip *chip =
		container_of(dwork, struct sgm41515_chip, event_work);
	bool prev_pg = false, curr_pg = false, bus_gd = false;
	u8 reg_val = 0;
	int ret = 0;

	if (chip->otg_mode) {
		chg_info("is otg mode\n");
		return;
	}

	chg_err(" sgm41515_irq_handler:enter improve irq time\n");
	oplus_keep_resume_wakelock(chip, true);

	/*for check bus i2c/spi is ready or not*/
	if (atomic_read(&chip->charger_suspended) == 1) {
		chg_err(" sgm41515_irq_handler:suspended and wait_event_interruptible %d\n", OPLUS_WAIT_RESUME_TIME);
		wait_event_interruptible_timeout(chip->wait, atomic_read(&chip->charger_suspended) == 0, msecs_to_jiffies(OPLUS_WAIT_RESUME_TIME));
	}
	prev_pg = chip->power_good;
	ret = sgm41515_read_byte(chip, REG0A_SGM41515_ADDRESS, &reg_val);
	if (ret) {
		chg_err("SGM41515_REG_0A read failed ret[%d]\n", ret);
		oplus_keep_resume_wakelock(chip, false);
		return;
	}
	bus_gd = sgm41515_get_bus_gd(chip);
	curr_pg = bus_gd;
	if(curr_pg) {
		oplus_chg_wakelock(chip, true);
	}
	sgm41515_dump_registers(chip);

	chip->vbus_present = curr_pg;
	chip->power_good = curr_pg;

	chg_info("(%d,%d, %d, %d)\n", prev_pg, chip->power_good, curr_pg, bus_gd);

	if (!prev_pg && chip->power_good) {
		oplus_chg_ic_virq_trigger(chip->ic_dev, OPLUS_IC_VIRQ_PLUGIN);
		oplus_chg_wakelock(chip, true);
		sgm41515_request_dpdm(chip, true);
		chip->bc12_complete = false;
		chip->bc12_retry = 0;
		chip->bc12_delay_cnt = 0;

		sgm41515_set_wdt_timer(chip, REG05_SGM41515_WATCHDOG_TIMER_40S);

		if (chip->charge_type == POWER_SUPPLY_TYPE_UNKNOWN)
			sgm41515_get_bc12(chip);

		goto POWER_CHANGE;
	} else if (prev_pg && !chip->power_good) {
		oplus_chg_ic_virq_trigger(chip->ic_dev, OPLUS_IC_VIRQ_PLUGIN);
		chip->bc12_complete = false;
		chip->bc12_retried = 0;
		chip->bc12_delay_cnt = 0;
		chip->charge_type = POWER_SUPPLY_TYPE_UNKNOWN;
		sgm41515_request_dpdm(chip, false);
		oplus_chg_wakelock(chip, false);
		goto POWER_CHANGE;
	} else if (!prev_pg && !chip->power_good) {
		chg_err("prev_pg & now_pg is false\n");
		chip->bc12_complete = false;
		chip->bc12_retried = 0;
		chip->bc12_delay_cnt = 0;
		goto POWER_CHANGE;
	}
POWER_CHANGE:

	oplus_keep_resume_wakelock(chip, false);
}

static irqreturn_t sgm41515_event_handler(int irq, void *dev_id)
{
	struct sgm41515_chip *chip = dev_id;

	chg_info("sgm41515 event irq\n");
	schedule_delayed_work(&chip->event_work, 0);
	return IRQ_HANDLED;
}

struct oplus_chg_ic_virq sgm41515_virq_table[] = {
	{ .virq_id = OPLUS_IC_VIRQ_ERR },
	{ .virq_id = OPLUS_IC_VIRQ_PLUGIN },
	{ .virq_id = OPLUS_IC_VIRQ_CHG_TYPE_CHANGE },
	{ .virq_id = OPLUS_IC_VIRQ_BC12_COMPLETED },
};

static int sgm41515_init(struct oplus_chg_ic_dev *ic_dev)
{
	ic_dev->online = true;
	return 0;
}

static int sgm41515_exit(struct oplus_chg_ic_dev *ic_dev)
{
	if (!ic_dev->online)
		return 0;

	ic_dev->online = false;
	return 0;
}

static int sgm41515_reg_dump(struct oplus_chg_ic_dev *ic_dev)
{
	struct sgm41515_chip *chip;
	u8 buf[REG_MAX + 1];
	int rc;

	if (ic_dev == NULL) {
		chg_err("ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	rc = regmap_bulk_read(chip->regmap, 0x00, buf, ARRAY_SIZE(buf));
	if (rc < 0) {
		chg_err("can't dump register, rc=%d", rc);
		return rc;
	}
	print_hex_dump(KERN_ERR, "OPLUS_CHG[SGM41515]: ", DUMP_PREFIX_OFFSET,
		       32, 1, buf, ARRAY_SIZE(buf), false);
	return 0;
}

static int sgm41515_smt_test(struct oplus_chg_ic_dev *ic_dev, char buf[], int len)
{
	return 0;
}

static int sgm41515_input_present(struct oplus_chg_ic_dev *ic_dev, bool *present)
{
	struct sgm41515_chip *chip;

	if (ic_dev == NULL) {
		chg_err("ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	*present = chip->vbus_present;

	return 0;
}

static int sgm41515_set_wdt_timer(struct sgm41515_chip *chip, int reg)
{
	int rc = 0;

	if (!chip)
		return 0;

	if (atomic_read(&chip->charger_suspended) == 1)
		return 0;

	rc = sgm41515_write_byte_mask(chip, REG05_SGM41515_ADDRESS,
			REG05_SGM41515_WATCHDOG_TIMER_MASK,
			reg);
	if (rc)
		chg_err("Couldn't set recharging threshold rc = %d\n", rc);

	return 0;
}

static int sgm41515_otg_ilim_set(struct sgm41515_chip *chip, int ilim)
{
	int rc;
	u8 reg_val;

	if (!chip)
		return 0;

	if (atomic_read(&chip->charger_suspended) == 1)
		return 0;

	if (ilim < REG02_SGM41515_BOOSTI_1200MA)
		reg_val = REG02_SGM41515_OTG_CURRENT_LIMIT_500MA;
	else
		reg_val = REG02_SGM41515_OTG_CURRENT_LIMIT_1200MA;

	rc = sgm41515_write_byte_mask(chip, REG02_SGM41515_ADDRESS,
			REG02_SGM41515_OTG_CURRENT_LIMIT_MASK,
			reg_val);
	if (rc < 0)
		chg_err("Couldn't sgm41515_otg_ilim_set  rc = %d\n", rc);

	return rc;
}

static int sgm41515_otg_enable(struct sgm41515_chip *chip)
{
	int rc = 0;

	if (!chip)
		return 0;

	if (atomic_read(&chip->charger_suspended) == 1)
		return 0;

	sgm41515_set_wdt_timer(chip, REG05_SGM41515_WATCHDOG_TIMER_DISABLE);

	rc = sgm41515_otg_ilim_set(chip, REG02_SGM41515_OTG_CURRENT_LIMIT_1200MA);
	if (rc < 0)
		chg_err("Couldn't sgm41515_otg_ilim_set rc = %d\n", rc);

	rc = sgm41515_write_byte_mask(chip, REG01_SGM41515_ADDRESS,
			REG01_SGM41515_OTG_MASK,
			REG01_SGM41515_OTG_ENABLE);
	if (rc < 0)
		chg_err("Couldn't sgm41515_otg_enable  rc = %d\n", rc);

	return rc;
}

static int sgm41515_otg_disable(struct sgm41515_chip *chip)
{
	int rc = 0;

	if (!chip)
		return 0;

	if (atomic_read(&chip->charger_suspended) == 1)
		return 0;

	rc = sgm41515_write_byte_mask(chip, REG01_SGM41515_ADDRESS,
			REG01_SGM41515_OTG_MASK,
			REG01_SGM41515_OTG_DISABLE);
	if (rc < 0)
		chg_err("Couldn't sgm41515_otg_disable rc = %d\n", rc);

	sgm41515_set_wdt_timer(chip, REG05_SGM41515_WATCHDOG_TIMER_DISABLE);

	return rc;
}

static int sgm41515_enable_gpio(struct sgm41515_chip *chip, bool enable)
{
	if (enable) {
		if (gpio_is_valid(chip->dis_vbus_gpio))
			gpio_direction_output(chip->dis_vbus_gpio, 0);
	} else {
		if (gpio_is_valid(chip->dis_vbus_gpio))
			gpio_direction_output(chip->dis_vbus_gpio, 1);
	}
	return 0;
}

static int sgm41515_enable_charging(struct sgm41515_chip *chip)
{
	int rc = 0;

	if (!chip)
		return 0;

	if (atomic_read(&chip->charger_suspended) == 1)
		return 0;

	sgm41515_enable_gpio(chip, true);
	sgm41515_otg_disable(chip);
	rc = sgm41515_write_byte_mask(chip, REG01_SGM41515_ADDRESS,
			REG01_SGM41515_CHARGING_MASK,
			REG01_SGM41515_CHARGING_ENABLE);
	if (rc < 0)
		chg_err("Couldn't sgm41515_enable_charging rc = %d\n", rc);

	chg_info("sgm41515_enable_charging \n");
	return rc;
}

static int sgm41515_disable_charging(struct sgm41515_chip *chip)
{
	int rc = 0;

	if (!chip)
		return 0;

	if (atomic_read(&chip->charger_suspended) == 1)
		return 0;

	sgm41515_enable_gpio(chip, false);
	sgm41515_otg_disable(chip);
	rc = sgm41515_write_byte_mask(chip, REG01_SGM41515_ADDRESS,
			REG01_SGM41515_CHARGING_MASK,
			REG01_SGM41515_CHARGING_DISABLE);
	if (rc < 0)
		chg_err("Couldn't sgm41515_disable_charging rc = %d\n", rc);

	chg_info("sgm41515_disable_charging \n");
	return rc;
}

static int sgm41515_get_usb_icl(struct sgm41515_chip *chip)
{
	int rc = 0;
	int icl_ma = 0;
	u8 reg_val = 0;

	if (!chip)
		return 0;

	rc = sgm41515_read_byte(chip, REG00_SGM41515_ADDRESS, &reg_val);
	if (rc) {
		chg_err("Couldn't read REG00_SGM41515_ADDRESS rc = %d\n", rc);
		return 0;
	}
	icl_ma = (reg_val & REG00_SGM41515_INPUT_CURRENT_LIMIT_MASK) >> REG00_SGM41515_INPUT_CURRENT_LIMIT_SHIFT;
	icl_ma = (icl_ma * REG00_SGM41515_INPUT_CURRENT_LIMIT_STEP + REG00_SGM41515_INPUT_CURRENT_LIMIT_OFFSET);
	return icl_ma;
}

static int sgm41515_input_current_limit_without_aicl(struct sgm41515_chip *chip, int current_ma)
{
	int rc = 0;
	int tmp = 0;

	if (!chip)
		return 0;

	if (atomic_read(&chip->charger_suspended) == 1) {
		chg_err("in suspend\n");
		return 0;
	}

	if (current_ma > REG00_SGM41515_INPUT_CURRENT_LIMIT_MAX)
		current_ma = REG00_SGM41515_INPUT_CURRENT_LIMIT_MAX;

	if (current_ma < REG00_SGM41515_INPUT_CURRENT_LIMIT_OFFSET)
		current_ma = REG00_SGM41515_INPUT_CURRENT_LIMIT_OFFSET;

	tmp = (current_ma - REG00_SGM41515_INPUT_CURRENT_LIMIT_OFFSET) / REG00_SGM41515_INPUT_CURRENT_LIMIT_STEP;
	chg_err("tmp current [%d]ma\n", current_ma);
	rc = sgm41515_write_byte_mask(chip, REG00_SGM41515_ADDRESS,
			REG00_SGM41515_INPUT_CURRENT_LIMIT_MASK,
			tmp << REG00_SGM41515_INPUT_CURRENT_LIMIT_SHIFT);

	if (rc < 0)
		chg_err("Couldn't set aicl rc = %d\n", rc);

	return rc;
}

static int sgm41515_set_vindpm_vol(struct sgm41515_chip *chip)
{
	int rc = 0;
	int tmp = 0;
	int vindpm = 0;
	int offset = 0, offset_val = 0;

	if (!chip)
		return 0;

	if (atomic_read(&chip->charger_suspended) == 1)
		return 0;

	chg_info("vindpm = %d\n", vindpm);

	vindpm = chip->hw_aicl_point;
	if(vindpm < SGM41515_VINDPM_THRESHOLD_5900MV) {
		offset = VINDPM_OS_3900mV;
		offset_val = SGM41515_VINDPM_THRESHOLD_3900MV;
	} else if (vindpm < SGM41515_VINDPM_THRESHOLD_7500MV) {
		offset = VINDPM_OS_5900mV;
		offset_val = SGM41515_VINDPM_THRESHOLD_5900MV;
	} else if (vindpm < SGM41515_VINDPM_THRESHOLD_10500MV) {
		offset = VINDPM_OS_7500mV;
		offset_val = SGM41515_VINDPM_THRESHOLD_7500MV;
	} else if (vindpm <= SGM41515_VINDPM_THRESHOLD_MAX) {
		offset = VINDPM_OS_10500mV;
		offset_val = SGM41515_VINDPM_THRESHOLD_10500MV;
	}

	/*set input offset*/
	rc = sgm41515_write_byte_mask(chip, REG0F_SGM41515_ADDRESS,
			REG0F_SGM41515_VINDPM_THRESHOLD_OFFSET_MASK,
			offset);

	/*set input vindpm*/
	tmp = (vindpm - offset_val) / REG06_SGM41515_VINDPM_STEP_MV;
	rc = sgm41515_write_byte_mask(chip, REG06_SGM41515_ADDRESS,
			REG06_SGM41515_VINDPM_MASK,
			tmp << REG06_SGM41515_VINDPM_SHIFT);

	return rc;
}

static int sgm41515_get_charger_vol(struct sgm41515_chip *chip)
{
	/* Not support vbus only sgm41515 */
	return chip->sw_aicl_point;
}

static int sgm41515_usb_icl[] = {
	300, 500, 900, 1200, 1350, 1500, 1750, 2000, 3000,
};
static int sgm41515_input_current_limit_write(struct sgm41515_chip *chip, int current_ma)
{
	int i = 0, rc = 0;
	int chg_vol = 0;
	int sw_aicl_point = 0;
	int pre_icl_index = 0, pre_icl = 0;

	if (!chip)
		return 0;

	if (atomic_read(&chip->charger_suspended) == 1)
		return 0;

	if (atomic_read(&chip->is_suspended) == 1) {
		chg_err("suspend,ignore set current=%dmA\n", current_ma);
		return 0;
	}

	/* first: icl down to 500mA, step from pre icl */
	pre_icl = sgm41515_get_usb_icl(chip);
	for (pre_icl_index = ARRAY_SIZE(sgm41515_usb_icl) - 1; pre_icl_index >= 0; pre_icl_index--) {
		if (sgm41515_usb_icl[pre_icl_index] < pre_icl)
			break;
	}
	chg_err("icl_set: %d, pre_icl: %d, pre_icl_index: %d\n", current_ma, pre_icl, pre_icl_index);

	for (i = pre_icl_index; i > 1; i--) {
		rc = sgm41515_input_current_limit_without_aicl(chip, sgm41515_usb_icl[i]);
		if (rc)
			chg_err("icl_down: set icl to %d mA fail, rc=%d\n", sgm41515_usb_icl[i], rc);
		else
			chg_err("icl_down: set icl to %d mA\n", sgm41515_usb_icl[i]);
		msleep(50);
	}

	/*second: aicl process, step from 500ma*/
	if (current_ma < 500) {
		i = 0;
		goto aicl_end;
	}

	sw_aicl_point = chip->sw_aicl_point;

	i = 1; /* 500 */
	rc = sgm41515_input_current_limit_without_aicl(chip, sgm41515_usb_icl[i]);
	usleep_range(90000, 91000);
	chg_vol = sgm41515_get_charger_vol(chip);
	if (chg_vol < sw_aicl_point) {
		chg_debug("use 500 here\n");
		goto aicl_end;
	} else if (current_ma < 900)
		goto aicl_end;
	i = 2; /* 900 */
	rc = sgm41515_input_current_limit_without_aicl(chip, sgm41515_usb_icl[i]);
	usleep_range(90000, 91000);
	chg_vol = sgm41515_get_charger_vol(chip);
	if (chg_vol < sw_aicl_point) {
		i = i - 1;
		goto aicl_pre_step;
	} else if (current_ma < 1200)
		goto aicl_end;
	i = 3; /* 1200 */
	rc = sgm41515_input_current_limit_without_aicl(chip, sgm41515_usb_icl[i]);
	usleep_range(90000, 91000);
	chg_vol = sgm41515_get_charger_vol(chip);
	if (chg_vol < sw_aicl_point) {
		i = i - 1;
		goto aicl_pre_step;
	}
	i = 4; /* 1350 */
	rc = sgm41515_input_current_limit_without_aicl(chip, sgm41515_usb_icl[i]);
	usleep_range(90000, 91000);
	chg_vol = sgm41515_get_charger_vol(chip);
	if (chg_vol < sw_aicl_point) {
		i = i - 2; /*We DO NOT use 1.2A here*/
		goto aicl_pre_step;
	} else if (current_ma < 1350) {
		i = i - 1; /*We use 1.2A here*/
		goto aicl_end;
	}
	i = 5; /* 1500 */
	rc = sgm41515_input_current_limit_without_aicl(chip, sgm41515_usb_icl[i]);
	usleep_range(90000, 91000);
	chg_vol = sgm41515_get_charger_vol(chip);
	if (chg_vol < sw_aicl_point) {
		i = i - 3; /*We DO NOT use 1.2A here*/
		goto aicl_pre_step;
	} else if (current_ma < 1500) {
		i = i - 2; /*We use 1.2A here*/
		goto aicl_end;
	} else if (current_ma < 2000) {
		goto aicl_end;
	}
	i = 6; /* 1750 */
	rc = sgm41515_input_current_limit_without_aicl(chip, sgm41515_usb_icl[i]);
	usleep_range(90000, 91000);
	chg_vol = sgm41515_get_charger_vol(chip);
	if (chg_vol < sw_aicl_point) {
		i = i - 3; /*1.2*/
		goto aicl_pre_step;
	}
	i = 7; /* 2000 */
	rc = sgm41515_input_current_limit_without_aicl(chip, sgm41515_usb_icl[i]);
	usleep_range(90000, 91000);
	chg_vol = sgm41515_get_charger_vol(chip);
	if (chg_vol < sw_aicl_point) {
		i = i - 2; /*1.5*/
		goto aicl_pre_step;
	} else if (current_ma < 3000) {
		goto aicl_end;
	}
	i = 8; /* 3000 */
	rc = sgm41515_input_current_limit_without_aicl(chip, sgm41515_usb_icl[i]);
	usleep_range(90000, 91000);
	chg_vol = sgm41515_get_charger_vol(chip);
	if (chg_vol < sw_aicl_point) {
		i = i -1;
		goto aicl_pre_step;
	} else if (current_ma >= 3000) {
		goto aicl_end;
	}
aicl_pre_step:
	chg_debug("usb input max current limit aicl chg_vol=%d j[%d]=%d sw_aicl_point:%d aicl_pre_step\n", chg_vol, i, sgm41515_usb_icl[i], sw_aicl_point);
	goto aicl_rerun;
aicl_end:
	chg_debug("usb input max current limit aicl chg_vol=%d j[%d]=%d sw_aicl_point:%d aicl_end\n", chg_vol, i, sgm41515_usb_icl[i], sw_aicl_point);
	goto aicl_rerun;
aicl_rerun:
	/* aicl_result = sgm41515_usb_icl[i]; */
	rc = sgm41515_input_current_limit_without_aicl(chip, sgm41515_usb_icl[i]);
	rc = sgm41515_set_vindpm_vol(chip);
	return rc;
}

static int sgm41515_set_prechg_voltage_threshold(struct sgm41515_chip *chip)
{
	int rc = 0;

	if (!chip)
		return 0;

	if (atomic_read(&chip->charger_suspended) == 1)
		return 0;

	rc = sgm41515_write_byte_mask(chip, REG01_SGM41515_ADDRESS,
			REG01_SGM41515_SYS_VOL_LIMIT_MASK,
			REG01_SGM41515_SYS_VOL_LIMIT_3400MV);

	return rc;
}

static const unsigned int SGM41515D_IPRECHG_CURRENT_STABLE[] = {
	5, 10, 15, 20, 30, 40, 50, 60,
	80, 100, 120, 140, 160, 180, 200, 240
};

int sgm41515_set_prechg_current(struct sgm41515_chip *chip, int ipre_mA)
{
	int tmp = 0;
	int rc = 0;

	if (!chip)
		return 0;

	if (atomic_read(&chip->charger_suspended) == 1)
		return 0;

	if (chip->part_id == SGM41515D_PART_ID) {
		chg_err("sgm41515d prechg_current = %d\n", ipre_mA);
		for(tmp = 0; tmp < 16; tmp++) {
			if (ipre_mA >= SGM41515D_IPRECHG_CURRENT_STABLE[tmp])
				break;
		}
		tmp--;
	} else {
		chg_err("prechg_current = %d\n", ipre_mA);
		tmp = ipre_mA - REG03_SGM41515_PRE_CHG_CURRENT_LIMIT_OFFSET;
		tmp = tmp / REG03_SGM41515_PRE_CHG_CURRENT_LIMIT_STEP;
	}

	rc = sgm41515_write_byte_mask(chip, REG03_SGM41515_ADDRESS,
			REG03_SGM41515_PRE_CHG_CURRENT_LIMIT_MASK,
			(tmp + 1) << REG03_SGM41515_PRE_CHG_CURRENT_LIMIT_SHIFT);

	return 0;
}

static int sgm41515_set_chging_term_disable(struct sgm41515_chip *chip)
{
	int rc = 0;

	if (!chip)
		return 0;

	if(atomic_read(&chip->charger_suspended) == 1)
		return 0;

	rc = sgm41515_write_byte_mask(chip, REG05_SGM41515_ADDRESS,
			REG05_SGM41515_TERMINATION_MASK,
			REG05_SGM41515_TERMINATION_DISABLE);
	if (rc)
		chg_err("Couldn't set chging term disable rc = %d\n", rc);

	return rc;
}

static int sgm41515_suspend_charger(struct sgm41515_chip *chip)
{
	int rc = 0;

	if (!chip)
		return 0;

	if (atomic_read(&chip->charger_suspended) == 1)
		return 0;

	atomic_set(&chip->is_suspended, 1);

	rc = sgm41515_disable_charging(chip);
	rc = sgm41515_input_current_limit_without_aicl(chip, 100);

	return rc;
}

static int sgm41515_unsuspend_charger(struct sgm41515_chip *chip)
{
	int rc = 0;

	if (!chip)
		return 0;

	if (atomic_read(&chip->charger_suspended) == 1)
		return 0;

	atomic_set(&chip->is_suspended, 0);
	rc = sgm41515_enable_charging(chip);

	return rc;
}

static int sgm41515_input_suspend(struct oplus_chg_ic_dev *ic_dev, bool suspend)
{
	int rc = 0;
	struct sgm41515_chip *chip;

	if (ic_dev == NULL) {
		chg_err("ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	if (!chip)
		return 0;

	if (atomic_read(&chip->charger_suspended) == 1)
		return 0;

	if (suspend) {
		rc = sgm41515_suspend_charger(chip);
	} else {
		rc = sgm41515_unsuspend_charger(chip);
	}

	return rc;
}

static int sgm41515_input_is_suspend(struct oplus_chg_ic_dev *ic_dev, bool *suspend)
{
	struct sgm41515_chip *chip;

	chip = oplus_chg_ic_get_drvdata(ic_dev);
	if (!chip)
		return 0;

	if (atomic_read(&chip->charger_suspended) == 1)
		return 0;

	return atomic_read(&chip->is_suspended);
}

static int sgm41515_output_suspend(struct oplus_chg_ic_dev *ic_dev, bool suspend)
{
	int rc = 0;
	struct sgm41515_chip *chip;

	if (ic_dev == NULL) {
		chg_err("ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	if (suspend)
		rc = sgm41515_disable_charging(chip);
	else
		rc = sgm41515_enable_charging(chip);

	chg_info("charger out %s, rc = %d", suspend ? "suspend" : "unsuspend", rc);

	return rc;
}

static int sgm41515_output_is_suspend(struct oplus_chg_ic_dev *ic_dev, bool *suspend)
{
	int rc = 0;
	u8 reg_val = 0;
	struct sgm41515_chip *chip;

	if (ic_dev == NULL) {
		chg_err("ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	if (atomic_read(&chip->charger_suspended) == 1)
		return 0;

	rc = sgm41515_read_byte(chip, REG01_SGM41515_ADDRESS, &reg_val);
	if (rc) {
		chg_err("Couldn't read REG01_SGM41515_ADDRESS rc = %d\n", rc);
		return 0;
	}

	*suspend = !!(reg_val & REG01_SGM41515_CHARGING_MASK);

	return 0;
}

static int sgm41515_set_icl(struct oplus_chg_ic_dev *ic_dev, bool vooc_mode, bool step, int icl_ma)
{
	int rc = 0;
	struct sgm41515_chip *chip;

	if (ic_dev == NULL) {
		chg_err("ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	if (step)
		rc = sgm41515_input_current_limit_write(chip, icl_ma);
	else
		rc = sgm41515_input_current_limit_without_aicl(chip, icl_ma);
	return rc;
}

static int sgm41515_get_icl(struct oplus_chg_ic_dev *ic_dev, int *icl_ma)
{
	int icl = 0;
	struct sgm41515_chip *chip;

	if (ic_dev == NULL) {
		chg_err("ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	icl = sgm41515_get_usb_icl(chip);
	*icl_ma = icl;

	return 0;
}

static int sgm41515_charging_current_write_fast(struct sgm41515_chip *chip, int fcc_ma)
{
	int rc = 0;
	int tmp = 0;

	if (!chip)
		return 0;

	if(atomic_read(&chip->charger_suspended) == 1)
		return 0;

	chg_info("sgm41515d set charge current = %d\n", fcc_ma);
	if (fcc_ma <= 40)
		tmp = fcc_ma / 5;
	else if (fcc_ma <= 110)
		tmp = 0x08 + (fcc_ma - 40) / 10;
	else if (fcc_ma <= 270)
		tmp = 0x0F + (fcc_ma - 110) / 20;
	else if (fcc_ma <= 540)
		tmp = 0x17 + (fcc_ma - 270) / 30;
	else if (fcc_ma <= 1500)
		tmp = 0x20 + (fcc_ma - 540) / 60;
	else if (fcc_ma <= 2940)
		tmp = 0x30 + (fcc_ma - 1500) / 120;
	else
		tmp = 0x3d;

	rc = sgm41515_write_byte_mask(chip, REG02_SGM41515_ADDRESS,
			REG02_SGM41515_FAST_CHG_CURRENT_LIMIT_MASK,
			tmp << REG02_SGM41515_FAST_CHG_CURRENT_LIMIT_SHIFT);

	return rc;
}

static int sgm41515_set_fcc(struct oplus_chg_ic_dev *ic_dev, int fcc_ma)
{
	struct sgm41515_chip *chip;

	if (ic_dev == NULL) {
		chg_err("ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	return sgm41515_charging_current_write_fast(chip, fcc_ma);
}

static int sgm41515_float_voltage_write(struct sgm41515_chip *chip, int vfloat_mv)
{
	int rc = 0;
	int tmp = 0;

	if (!chip)
		return 0;

	if(atomic_read(&chip->charger_suspended) == 1)
		return 0;

	chg_err("vfloat_mv = %d\n", vfloat_mv);

	tmp = vfloat_mv - REG04_SGM41515_CHG_VOL_LIMIT_OFFSET;

	tmp = tmp / REG04_SGM41515_CHG_VOL_LIMIT_STEP;

	rc = sgm41515_write_byte_mask(chip, REG04_SGM41515_ADDRESS,
			REG04_SGM41515_CHG_VOL_LIMIT_MASK,
			tmp << REG04_SGM41515_CHG_VOL_LIMIT_SHIFT);

	return rc;
}

static int sgm41515_set_fv(struct oplus_chg_ic_dev *ic_dev, int fv_mv)
{
	struct sgm41515_chip *chip;

	if (ic_dev == NULL) {
		chg_err("ic_dev is NULL");
		return -ENODEV;
	}

	if (fv_mv <= 0) {
		chg_err("invalid value ignore");
		return 0;
	}

	chip = oplus_chg_ic_get_drvdata(ic_dev);

	return sgm41515_float_voltage_write(chip, fv_mv);
}

static const unsigned int SGM41515D_ITERM_CURRENT_STABLE[] = {
	5, 10, 15, 20, 30, 40, 50, 60,
	80, 100, 120, 140, 160, 180, 200, 240
};

static int sgm41515_set_termchg_current(struct sgm41515_chip *chip, int term_curr)
{
	int rc = 0;
	int tmp = 0;

	if (!chip)
		return 0;

	if(atomic_read(&chip->charger_suspended) == 1)
		return 0;

	chg_err("sgm41515d term_current = %d\n", term_curr);
	for(tmp = 0; tmp < 16; tmp++) {
		if(term_curr >= SGM41515D_ITERM_CURRENT_STABLE[tmp])
			break;
	}
	tmp--;

	rc = sgm41515_write_byte_mask(chip, REG03_SGM41515_ADDRESS,
			REG03_SGM41515_TERM_CHG_CURRENT_LIMIT_MASK,
			tmp << REG03_SGM41515_TERM_CHG_CURRENT_LIMIT_SHIFT);

	return 0;
}

static int sgm41515_set_iterm(struct oplus_chg_ic_dev *ic_dev, int iterm_ma)
{
	struct sgm41515_chip *chip;

	if (ic_dev == NULL) {
		chg_err("ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	return sgm41515_set_termchg_current(chip, iterm_ma);
}

static int sgm41515_set_rechg_voltage(struct sgm41515_chip *chip, int recharge_mv)
{
	int rc = 0;
	u8 reg_val;

	if (!chip)
		return 0;

	if (atomic_read(&chip->charger_suspended) == 1)
		return 0;

	if (recharge_mv < REG04_SGM41515_RECHG_VOLTAGE_200MV)
		reg_val = REG04_SGM41515_RECHG_THRESHOLD_VOL_100MV;
	else
		reg_val = REG04_SGM41515_RECHG_THRESHOLD_VOL_200MV;

	rc = sgm41515_write_byte_mask(chip, REG04_SGM41515_ADDRESS,
			REG04_SGM41515_RECHG_THRESHOLD_VOL_MASK,
			reg_val);

	if (rc)
		chg_err("Couldn't set recharging threshold rc = %d\n", rc);

	return rc;
}

static int sgm41515_set_rechg_vol(struct oplus_chg_ic_dev *ic_dev, int vol_mv)
{
	struct sgm41515_chip *chip;

	if (ic_dev == NULL) {
		chg_err("ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	return sgm41515_set_rechg_voltage(chip, vol_mv);
}

static int sgm41515_get_input_curr(struct oplus_chg_ic_dev *ic_dev, int *curr_ma)
{
	/* Not support ibus only sgm41515 */
	*curr_ma = 0;
	return 0;
}

static int sgm41515_get_input_vol(struct oplus_chg_ic_dev *ic_dev, int *vol_mv)
{
	/* Not support vbus only sgm41515 */
	struct sgm41515_chip *chip;

	if (ic_dev == NULL) {
		chg_err("ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	*vol_mv = chip->sw_aicl_point;
	return 0;
}

static int sgm41515_set_aicl_point(struct oplus_chg_ic_dev *ic_dev, int vbatt)
{
	int rc = 0;
	struct sgm41515_chip *chip;

	if (ic_dev == NULL) {
		chg_err("ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	if (chip->hw_aicl_point == SGM41515_INP_VOL_4V44 && vbatt > SGM41515_BATT_VOL_4V14) {
		chip->hw_aicl_point = SGM41515_INP_VOL_4V52;
		chip->sw_aicl_point = SGM41515_INP_VOL_4V535;
		rc = sgm41515_set_vindpm_vol(chip);
	} else if (chip->hw_aicl_point == SGM41515_INP_VOL_4V52 && vbatt < SGM41515_BATT_VOL_4V14) {
		chip->hw_aicl_point = SGM41515_INP_VOL_4V44;
		chip->sw_aicl_point = SGM41515_INP_VOL_4V5;
		rc = sgm41515_set_vindpm_vol(chip);
	}
	return rc;
}

static int sgm41515_otg_boost_enable(struct oplus_chg_ic_dev *ic_dev, bool en)
{
	int rc;
	struct sgm41515_chip *chip;

	if (ic_dev == NULL) {
		chg_err("ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	chip->otg_mode = en;

	if (en)
		rc = sgm41515_otg_enable(chip);
	else
		rc = sgm41515_otg_disable(chip);

	if (rc < 0)
		chg_err("can't %s otg boost, rc=%d\n", en ? "enable" : "disable", rc);
	return rc;
}

static int sgm41515_set_otg_voltage(struct sgm41515_chip *chip, int vol_mv)
{
	int rc = 0;
	u8 reg_val = 0;

	if (!chip)
		return 0;

	if (atomic_read(&chip->charger_suspended) == 1)
		return 0;

	if (vol_mv < REG06_SGM41515_OTG_VLIM_OFFSET)
		vol_mv = REG06_SGM41515_OTG_VLIM_OFFSET;
	else if (vol_mv > REG06_SGM41515_OTG_VLIM_MAXMV)
		vol_mv = REG06_SGM41515_OTG_VLIM_MAXMV;

	reg_val = (vol_mv - REG06_SGM41515_OTG_VLIM_OFFSET) / REG06_SGM41515_OTG_VLIM_STEP;

	rc = sgm41515_write_byte_mask(chip, REG06_SGM41515_ADDRESS,
			REG06_SGM41515_OTG_VLIM_MASK,
			reg_val);

	return rc;
}

static int sgm41515_set_otg_boost_vol(struct oplus_chg_ic_dev *ic_dev, int vol_mv)
{
	struct sgm41515_chip *chip;

	if (ic_dev == NULL) {
		chg_err("ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	return sgm41515_set_otg_voltage(chip, vol_mv);
}

static int sgm41515_set_otg_boost_curr_limit(struct oplus_chg_ic_dev *ic_dev, int curr_mA)
{
	int rc;
	struct sgm41515_chip *chip;

	if (ic_dev == NULL) {
		chg_err("ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);


	rc = sgm41515_otg_ilim_set(chip, curr_mA);
	if (rc)
		chg_err("failed to set boost current, ret = %d\n", rc);
	return 0;
}

static int sgm41515_aicl_enable(struct oplus_chg_ic_dev *ic_dev, bool en)
{
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	return rc;
}

static int sgm41515_aicl_rerun(struct oplus_chg_ic_dev *ic_dev)
{
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	return rc;
}

static int sgm41515_aicl_reset(struct oplus_chg_ic_dev *ic_dev)
{
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	return rc;
}

static int sgm41515_hardware_init(struct oplus_chg_ic_dev *ic_dev)
{
	struct sgm41515_chip *chip;

	if (ic_dev == NULL) {
		chg_err("ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	sgm41515_hw_init(chip);
	return 0;
}

static int sgm41515_get_charger_type(struct oplus_chg_ic_dev *ic_dev, int *type)
{
	struct sgm41515_chip *chip;

	if (ic_dev == NULL) {
		chg_err("ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	switch (chip->charge_type) {
	case POWER_SUPPLY_TYPE_USB:
		*type = OPLUS_CHG_USB_TYPE_SDP;
		break;
	case POWER_SUPPLY_TYPE_USB_CDP:
		*type = OPLUS_CHG_USB_TYPE_CDP;
		break;
	case POWER_SUPPLY_TYPE_USB_DCP:
		*type = OPLUS_CHG_USB_TYPE_DCP;
		break;
	default:
		*type = OPLUS_CHG_USB_TYPE_UNKNOWN;
		break;
	}

	return 0;
}

static int sgm41515_rerun_bc12(struct oplus_chg_ic_dev *ic_dev)
{
	struct sgm41515_chip *chip;
	int rc;

	if (ic_dev == NULL) {
		chg_err("ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	chg_info("rerun bc1.2\n");
	sgm41515_request_dpdm(chip, true);
	/* no need to retry */
	chip->bc12_retry = true;
	chip->auto_bc12 = false;
	chip->bc12_complete = false;
	rc = sgm41515_enable_hiz_mode(chip, false);
	if (rc < 0) {
		chg_err("can't disable hiz mode, rc=%d\n", rc);
		goto err;
	}
	rc = sgm41515_write_byte_mask(chip, REG07_SGM41515_ADDRESS, REG07_SGM41515_IINDET_EN_MASK,
				      REG07_SGM41515_IINDET_EN_FORCE_DET);
	if (rc < 0) {
		chg_err("can't rerun bc1.2, rc=%d", rc);
		goto err;
	}

	return 0;

err:
	chip->bc12_complete = true;
	return rc;
}

static int sgm41515_disable_vbus(struct oplus_chg_ic_dev *ic_dev, bool en,
				 bool delay)
{
	struct sgm41515_chip *chip;
	int rc;

	if (ic_dev == NULL) {
		chg_err("ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	chip->otg_mode = en;

	if (!gpio_is_valid(chip->dis_vbus_gpio)) {
		chg_info("Not support dis_vbus_gpio");
		return 0;
	}

	mutex_lock(&chip->pinctrl_lock);
	if (en) {
		rc = pinctrl_select_state(chip->pinctrl, chip->dis_vbus_active);
	} else {
		/* Wait for VBUS to be completely powered down, usually 20ms */
		if (delay)
			msleep(20);
		rc = pinctrl_select_state(chip->pinctrl, chip->dis_vbus_sleep);
	}
	mutex_unlock(&chip->pinctrl_lock);
	if (rc < 0)
		chg_err("can't set disable vbus gpio to %s, rc=%d\n",
			en ? "active" : "sleep", rc);
	else
		chg_err("set disable vbus gpio to %s\n",
			en ? "active" : "sleep");

	return rc;
}

static int sgm41515_kick_watchdog(struct sgm41515_chip *chip)
{
	int rc = 0;

	if (!chip)
		return 0;

	if(atomic_read(&chip->charger_suspended) == 1)
		return 0;

	rc = sgm41515_write_byte_mask(chip, REG01_SGM41515_ADDRESS,
			REG01_SGM41515_WDT_TIMER_RESET_MASK,
			REG01_SGM41515_WDT_TIMER_RESET);
	if (rc)
		chg_err("Couldn't sgm41515 kick wdt rc = %d\n", rc);

	return rc;
}

static int sgm41515_kick_wdt(struct oplus_chg_ic_dev *ic_dev)
{
	struct sgm41515_chip *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	return sgm41515_kick_watchdog(chip);
}

static int sgm41515_set_shipmode(struct sgm41515_chip *chip, bool enable)
{
	int rc = 0;

	if (chip == NULL)
		return rc;

	if (enable) {
		rc = sgm41515_write_byte_mask(chip, REG07_SGM41515_ADDRESS,
			REG07_SGM41515_BATFET_DIS_MASK,
			REG07_SGM41515_BATFET_DIS_ON);
		if (rc < 0)
			chg_err("Couldn't set REG07_SGM41515_BATFET_DIS_ON rc = %d\n", rc);
	} else {
		rc = sgm41515_write_byte_mask(chip, REG07_SGM41515_ADDRESS,
			REG07_SGM41515_BATFET_DIS_MASK,
			REG07_SGM41515_BATFET_DIS_OFF);
		if (rc < 0)
			chg_err("Couldn't set REG07_SGM41515_BATFET_DIS_OFF rc = %d\n", rc);
	}

	return rc;
}

static int sgm41515_shipmode_enable(struct oplus_chg_ic_dev *ic_dev, bool en)
{
	struct sgm41515_chip *chip;

	if (ic_dev == NULL) {
		chg_err("ic_dev is NULL");
		return -ENODEV;
	}

	chip = oplus_chg_ic_get_drvdata(ic_dev);

	sgm41515_set_shipmode(chip, en);

	return 0;
}

static void sgm41515_wired_subs_callback(struct mms_subscribe *subs,
					 enum mms_msg_type type, u32 id, bool sync)
{
	switch (type) {
	case MSG_TYPE_ITEM:
		switch (id) {
		case WIRED_ITEM_PRESENT:
			chg_info("wired present!");
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}
}

static void sgm41515_subscribe_wired_topic(struct oplus_mms *topic,
					   void *prv_data)
{
	struct sgm41515_chip *chip = prv_data;
	union mms_msg_data data = { 0 };

	chip->wired_topic = topic;
	chip->wired_subs =
		oplus_mms_subscribe(chip->wired_topic, chip,
				    sgm41515_wired_subs_callback, chip->ic_dev->manu_name);
	if (IS_ERR_OR_NULL(chip->wired_subs)) {
		chg_err("subscribe wired topic error, rc=%ld\n", PTR_ERR(chip->wired_subs));
		return;
	}

	oplus_mms_get_item_data(chip->wired_topic, WIRED_ITEM_PRESENT, &data,
				true);
	chip->vbus_present = !!data.intval;
	if (chip->vbus_present && !chip->otg_mode) {
		sgm41515_request_dpdm(chip, true);
		sgm41515_bc12_boot_check(chip);
	}

	if (sgm41515_get_bus_gd(chip) && (chip->charge_type == POWER_SUPPLY_TYPE_UNKNOWN))
		schedule_delayed_work(&chip->event_work, msecs_to_jiffies(INIT_WORK_OTHER_DELAY));
}

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
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_INIT, sgm41515_init);
		break;
	case OPLUS_IC_FUNC_EXIT:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_EXIT, sgm41515_exit);
		break;
	case OPLUS_IC_FUNC_REG_DUMP:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_REG_DUMP, sgm41515_reg_dump);
		break;
	case OPLUS_IC_FUNC_SMT_TEST:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_SMT_TEST, sgm41515_smt_test);
		break;
	case OPLUS_IC_FUNC_BUCK_INPUT_PRESENT:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BUCK_INPUT_PRESENT, sgm41515_input_present);
		break;
	case OPLUS_IC_FUNC_BUCK_INPUT_SUSPEND:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BUCK_INPUT_SUSPEND, sgm41515_input_suspend);
		break;
	case OPLUS_IC_FUNC_BUCK_INPUT_IS_SUSPEND:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BUCK_INPUT_IS_SUSPEND, sgm41515_input_is_suspend);
		break;
	case OPLUS_IC_FUNC_BUCK_OUTPUT_SUSPEND:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BUCK_OUTPUT_SUSPEND, sgm41515_output_suspend);
		break;
	case OPLUS_IC_FUNC_BUCK_OUTPUT_IS_SUSPEND:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BUCK_OUTPUT_IS_SUSPEND, sgm41515_output_is_suspend);
		break;
	case OPLUS_IC_FUNC_BUCK_SET_ICL:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BUCK_SET_ICL, sgm41515_set_icl);
		break;
	case OPLUS_IC_FUNC_BUCK_GET_ICL:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BUCK_GET_ICL, sgm41515_get_icl);
		break;
	case OPLUS_IC_FUNC_BUCK_SET_FCC:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BUCK_SET_FCC, sgm41515_set_fcc);
		break;
	case OPLUS_IC_FUNC_BUCK_SET_FV:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BUCK_SET_FV, sgm41515_set_fv);
		break;
	case OPLUS_IC_FUNC_BUCK_SET_ITERM:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BUCK_SET_ITERM, sgm41515_set_iterm);
		break;
	case OPLUS_IC_FUNC_BUCK_SET_RECHG_VOL:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BUCK_SET_RECHG_VOL, sgm41515_set_rechg_vol);
		break;
	case OPLUS_IC_FUNC_BUCK_GET_INPUT_CURR:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BUCK_GET_INPUT_CURR, sgm41515_get_input_curr);
		break;
	case OPLUS_IC_FUNC_BUCK_GET_INPUT_VOL:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BUCK_GET_INPUT_VOL, sgm41515_get_input_vol);
		break;
	case OPLUS_IC_FUNC_OTG_BOOST_ENABLE:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_OTG_BOOST_ENABLE, sgm41515_otg_boost_enable);
		break;
	case OPLUS_IC_FUNC_SET_OTG_BOOST_VOL:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_SET_OTG_BOOST_VOL, sgm41515_set_otg_boost_vol);
		break;
	case OPLUS_IC_FUNC_SET_OTG_BOOST_CURR_LIMIT:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_SET_OTG_BOOST_CURR_LIMIT, sgm41515_set_otg_boost_curr_limit);
		break;
	case OPLUS_IC_FUNC_BUCK_AICL_ENABLE:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BUCK_AICL_ENABLE, sgm41515_aicl_enable);
		break;
	case OPLUS_IC_FUNC_BUCK_AICL_RERUN:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BUCK_AICL_RERUN, sgm41515_aicl_rerun);
		break;
	case OPLUS_IC_FUNC_BUCK_AICL_RESET:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BUCK_AICL_RESET, sgm41515_aicl_reset);
		break;
	case OPLUS_IC_FUNC_BUCK_SET_AICL_POINT:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BUCK_SET_AICL_POINT, sgm41515_set_aicl_point);
		break;
	case OPLUS_IC_FUNC_BUCK_HARDWARE_INIT:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BUCK_HARDWARE_INIT, sgm41515_hardware_init);
		break;
	case OPLUS_IC_FUNC_BUCK_GET_CHARGER_TYPE:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BUCK_GET_CHARGER_TYPE, sgm41515_get_charger_type);
		break;
	case OPLUS_IC_FUNC_BUCK_RERUN_BC12:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BUCK_RERUN_BC12, sgm41515_rerun_bc12);
		break;
	case OPLUS_IC_FUNC_DISABLE_VBUS:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_DISABLE_VBUS, sgm41515_disable_vbus);
		break;
	case OPLUS_IC_FUNC_BUCK_SHIPMODE_ENABLE:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BUCK_SHIPMODE_ENABLE, sgm41515_shipmode_enable);
		break;
	case OPLUS_IC_FUNC_BUCK_KICK_WDT:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_BUCK_KICK_WDT, sgm41515_kick_wdt);
		break;
	default:
		chg_err("this func(=%d) is not supported\n", func_id);
		func = NULL;
		break;
	}

	return func;
}

static int sgm41515_gpio_init(struct sgm41515_chip *chip)
{
	int rc = 0;
	struct device_node *node = chip->dev->of_node;

	chip->pinctrl = devm_pinctrl_get(chip->dev);
	if (IS_ERR_OR_NULL(chip->pinctrl)) {
		chg_err("get pinctrl fail\n");
		return -ENODEV;
	}

	chip->dis_vbus_gpio = of_get_named_gpio(node, "oplus,dis_vbus-gpio", 0);
	if (gpio_is_valid(chip->dis_vbus_gpio)) {
		rc = gpio_request(chip->dis_vbus_gpio, "sgm41515_dis_vbus-gpio");
		if (rc < 0) {
			chg_err("event_gpio request error, rc=%d\n", rc);
			return rc;
		}
		chip->dis_vbus_active =
			pinctrl_lookup_state(chip->pinctrl, "dis_vbus_active");
		if (IS_ERR_OR_NULL(chip->dis_vbus_active)) {
			chg_err("get dis_vbus_active fail\n");
			goto free_dis_vbus_gpio;
		}
		chip->dis_vbus_sleep =
			pinctrl_lookup_state(chip->pinctrl, "dis_vbus_sleep");
		if (IS_ERR_OR_NULL(chip->dis_vbus_sleep)) {
			chg_err("get dis_vbus_sleep fail\n");
			goto free_dis_vbus_gpio;
		}
		pinctrl_select_state(chip->pinctrl, chip->dis_vbus_sleep);
	} else {
		chg_err("dis_vbus_gpio not specified\n");
	}

	chip->event_gpio = of_get_named_gpio(node, "oplus,event-gpio", 0);
	if (!gpio_is_valid(chip->event_gpio)) {
		chg_err("event_gpio not specified\n");
		rc = -ENODEV;
		goto free_dis_vbus_gpio;
	}
	rc = gpio_request(chip->event_gpio, "sgm41515_event-gpio");
	if (rc < 0) {
		chg_err("event_gpio request error, rc=%d\n", rc);
		goto free_dis_vbus_gpio;
	}
	chip->event_default =
		pinctrl_lookup_state(chip->pinctrl, "event_default");
	if (IS_ERR_OR_NULL(chip->event_default)) {
		chg_err("get event_default fail\n");
		goto free_event_gpio;
	}
	gpio_direction_input(chip->event_gpio);
	pinctrl_select_state(chip->pinctrl, chip->event_default);
	chip->event_irq = gpio_to_irq(chip->event_gpio);
	rc = devm_request_irq(chip->dev, chip->event_irq,
			      sgm41515_event_handler, IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
			      "sgm41515_event-irq", chip);
	if (rc < 0) {
		chg_err("event_irq request error, rc=%d\n", rc);
		goto free_event_gpio;
	}
	chip->event_irq_enabled = true;
	sgm41515_enable_irq(chip, false);

	return 0;

free_event_gpio:
	if (gpio_is_valid(chip->event_gpio))
		gpio_free(chip->event_gpio);
free_dis_vbus_gpio:
	if (gpio_is_valid(chip->dis_vbus_gpio))
		gpio_free(chip->dis_vbus_gpio);

	return rc;
}

static int sgm41515_batfet_reset_disable(struct sgm41515_chip *chip, bool enable)
{
	int rc = 0;
	int val = 0;

	if(enable)
		val = SGM41515_BATFET_RST_DISABLE << REG07_SGM41515_BATFET_RST_EN_SHIFT;
	else
		val = SGM41515_BATFET_RST_ENABLE << REG07_SGM41515_BATFET_RST_EN_SHIFT;

	rc = sgm41515_write_byte_mask(chip, REG07_SGM41515_ADDRESS, REG07_SGM41515_BATFET_RST_EN_MASK, val);

	return rc;
}

static bool sgm41515_get_deivce_online(struct sgm41515_chip *chip)
{
	int rc = 0;
	u8 reg_val = 0;

	if (!chip)
		return 0;

	if(atomic_read(&chip->charger_suspended) == 1)
		return 0;

	chip->part_id = 0xff;
	rc = sgm41515_read_byte(chip, REG0B_SGM41515_ADDRESS, &reg_val);
	if (rc) {
		rc = sgm41515_read_byte(chip, REG0B_SGM41515_ADDRESS, &reg_val);
		if (rc) {
			chg_err("Couldn't read REG0B_SGM41515_ADDRESS rc = %d\n", rc);
			return false;
		}
	}

	chip->part_id = (reg_val & REG0B_SGM41515_PN_MASK) >> SGM41515_DEVID_SHIFT;
	chg_err("sgm41515 part_id=0x%02X\n", chip->part_id);

	if (chip->part_id == SGM41541_PART_ID || chip->part_id == SGM41515_PART_ID ||
	    chip->part_id == SGM41515D_PART_ID)
		return true;

	return false;
}

static int sgm41515_set_ovp(struct sgm41515_chip *chip, int val)
{
	int rc = 0;

	if (!chip)
		return 0;

	if (atomic_read(&chip->charger_suspended) == 1)
		return 0;

	rc = sgm41515_write_byte_mask(chip, REG06_SGM41515_ADDRESS,
			REG06_SGM41515_OVP_MASK, val);

	return rc;
}

static int sgm41515_set_chg_timer(struct sgm41515_chip *chip, bool enable)
{
	int rc = 0;

	if (!chip)
		return 0;

	if(atomic_read(&chip->charger_suspended) == 1)
		return 0;

	rc = sgm41515_write_byte_mask(chip, REG05_SGM41515_ADDRESS,
			REG05_SGM41515_CHG_SAFETY_TIMER_MASK,
			enable ? REG05_SGM41515_CHG_SAFETY_TIMER_ENABLE : REG05_SGM41515_CHG_SAFETY_TIMER_DISABLE);
	if (rc)
		chg_err("Couldn't sgm41515_set_chg_timer rc = %d\n", rc);

	return rc;
}

static int sgm41515_set_int_mask(struct sgm41515_chip *chip, int val)
{
	int rc = 0;

	if (!chip)
		return 0;

	if(atomic_read(&chip->charger_suspended) == 1)
		return 0;

	rc = sgm41515_write_byte_mask(chip, REG0A_SGM41515_ADDRESS,
			REG0A_SGM41515_VINDPM_INT_MASK | REG0A_SGM41515_IINDPM_INT_MASK,
			val);
	if (rc)
		chg_err("Couldn't sgm41515 set_int_mask rc = %d\n", rc);

	return rc;
}

static int sgm41515_set_stat_dis(struct sgm41515_chip *chip, bool enable)
{
	int rc = 0;

	if (!chip)
		return 0;

	if (atomic_read(&chip->charger_suspended) == 1)
		return 0;

	rc = sgm41515_write_byte_mask(chip, REG00_SGM41515_ADDRESS,
			REG00_SGM41515_STAT_DIS_MASK,
			enable ? REG00_SGM41515_STAT_DIS_ENABLE : REG00_SGM41515_STAT_DIS_DISABLE);
	if (rc)
		chg_err("Couldn't sgm41515 set_stat_dis rc = %d\n", rc);

	return rc;
}

#define HW_AICL_POINT_OFFSET 4440
#define SW_AICL_POINT_OFFSET 4500
static int sgm41515_hw_init(struct sgm41515_chip *chip)
{
	chg_err("init sgm41515 hardware! \n");

	if (!chip)
		return 0;

	/*must be before set_vindpm_vol and set_input_current*/
	chip->hw_aicl_point = HW_AICL_POINT_OFFSET;
	chip->sw_aicl_point = SW_AICL_POINT_OFFSET;


	sgm41515_set_stat_dis(chip, false);
	sgm41515_set_int_mask(chip, REG0A_SGM41515_VINDPM_INT_NOT_ALLOW | REG0A_SGM41515_IINDPM_INT_NOT_ALLOW);
	sgm41515_set_chg_timer(chip, false);
	sgm41515_disable_charging(chip);
	sgm41515_set_ovp(chip, REG06_SGM41515_OVP_14P0V);
	sgm41515_set_chging_term_disable(chip);
	sgm41515_float_voltage_write(chip, SGM41515_DEFAULT_TERMINATION_VOLTAGE);
	sgm41515_otg_ilim_set(chip, REG02_SGM41515_BOOSTI_1200MA);
	sgm41515_set_prechg_voltage_threshold(chip);
	sgm41515_set_prechg_current(chip, SGM41515_DEFAULT_PRECHG_CURRENT);
	sgm41515_charging_current_write_fast(chip, REG02_SGM41515_FAST_CHG_CURRENT_LIMIT_2000MA);
	sgm41515_set_termchg_current(chip, 200);
	sgm41515_input_current_limit_without_aicl(chip, REG00_SGM41515_INIT_INPUT_CURRENT_LIMIT_500MA);
	sgm41515_set_rechg_voltage(chip, 1);
	sgm41515_set_vindpm_vol(chip);
	sgm41515_set_otg_voltage(chip, REG06_SGM41515_OTG_VLIM_5000MV);
	sgm41515_batfet_reset_disable(chip, true);
	sgm41515_unsuspend_charger(chip);
	sgm41515_enable_charging(chip);
	sgm41515_set_wdt_timer(chip, REG05_SGM41515_WATCHDOG_TIMER_40S);

	return 0;
}

#ifdef CONFIG_OPLUS_CHARGER_MTK
static const struct charger_properties  sgm41515_chg_props = {
	.alias_name = "sgm41515",
};

static int sgm41515_plug_in(struct charger_device *chg_dev)
{
	int ret = 0;
	struct sgm41515_chip *chip = dev_get_drvdata(&chg_dev->dev);

	if (!chip)
		return -EINVAL;

	ret = sgm41515_enable_charging(chip);
	if (ret) {
		chg_err("failed to enable charging:%d", ret);
	}
	return ret;
}

static int sgm41515_plug_out(struct charger_device *chg_dev)
{
	int ret = 0;
	struct sgm41515_chip *chip = dev_get_drvdata(&chg_dev->dev);

	if (!chip)
		return -EINVAL;

	ret = sgm41515_disable_charging(chip);

	if (ret)
		chg_err("failed to disable charging:%d", ret);

	return ret;
}

static int sgm41515_charge_kick_wdt(struct charger_device *chg_dev)
{
	struct sgm41515_chip *chip = dev_get_drvdata(&chg_dev->dev);

	if (!chip)
		return -EINVAL;

	return sgm41515_kick_watchdog(chip);
}

static int sgm41515_charge_enable(struct charger_device *chg_dev, bool en)
{
	struct sgm41515_chip *chip = dev_get_drvdata(&chg_dev->dev);

	if (!chip)
		return -EINVAL;

	if (en)
		return sgm41515_enable_charging(chip);
	else
		return sgm41515_disable_charging(chip);
}

static struct charger_ops sgm41515_chg_ops = {
	.plug_in = sgm41515_plug_in,
	.plug_out = sgm41515_plug_out,
	.kick_wdt = sgm41515_charge_kick_wdt,
	.enable = sgm41515_charge_enable,
};

#if (LINUX_VERSION_CODE < KERNEL_VERSION(6, 12, 0))
static enum power_supply_usb_type sgm41515_charger_usb_types[] = {
	POWER_SUPPLY_USB_TYPE_UNKNOWN,
	POWER_SUPPLY_USB_TYPE_SDP,
	POWER_SUPPLY_USB_TYPE_DCP,
	POWER_SUPPLY_USB_TYPE_CDP,
	POWER_SUPPLY_USB_TYPE_C,
	POWER_SUPPLY_USB_TYPE_PD,
	POWER_SUPPLY_USB_TYPE_PD_DRP,
	POWER_SUPPLY_USB_TYPE_APPLE_BRICK_ID
};
#endif

static enum power_supply_property sgm41515_charger_properties[] = {
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_TYPE,
	POWER_SUPPLY_PROP_USB_TYPE,
};

static int sgm41515_charger_get_property(struct power_supply *psy,
				   enum power_supply_property psp,
				   union power_supply_propval *val)
{
	struct sgm41515_chip *chip;
	int ret = 0;
#ifndef CONFIG_DISABLE_OPLUS_FUNCTION
	int boot_mode = get_boot_mode();
#endif

	chip = power_supply_get_drvdata(psy);

	if (!chip) {
		chg_info("oplus_chip not ready!\n");
		return -EINVAL;
	}

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = chip->power_good;
		break;
	case POWER_SUPPLY_PROP_TYPE:
	case POWER_SUPPLY_PROP_USB_TYPE:
#ifndef CONFIG_DISABLE_OPLUS_FUNCTION
		if (boot_mode == META_BOOT) {
			val->intval = POWER_SUPPLY_TYPE_USB;
		} else {
			val->intval = chip->charge_type;
		}
#endif
		chg_info("sgm41515 get power_supply_type = %d\n", val->intval);
		break;
	default:
		ret = -ENODATA;
	}
	return ret;
}

static char *sgm41515_charger_supplied_to[] = {
	"battery",
	"mtk-master-charger"
};

static const struct power_supply_desc sgm41515_charger_desc = {
	.type	= POWER_SUPPLY_TYPE_USB,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(6, 12, 0))
	.usb_types	= sgm41515_charger_usb_types,
	.num_usb_types	= ARRAY_SIZE(sgm41515_charger_usb_types),
#else
	.usb_types		= BIT(POWER_SUPPLY_USB_TYPE_UNKNOWN) |
				  BIT(POWER_SUPPLY_USB_TYPE_SDP)     |
				  BIT(POWER_SUPPLY_USB_TYPE_DCP)     |
				  BIT(POWER_SUPPLY_USB_TYPE_CDP)     |
				  BIT(POWER_SUPPLY_USB_TYPE_C)       |
				  BIT(POWER_SUPPLY_USB_TYPE_PD)      |
				  BIT(POWER_SUPPLY_USB_TYPE_PD_DRP)  |
				  BIT(POWER_SUPPLY_USB_TYPE_APPLE_BRICK_ID),
#endif
	.properties	= sgm41515_charger_properties,
	.num_properties	= ARRAY_SIZE(sgm41515_charger_properties),
	.get_property	= sgm41515_charger_get_property,
};

static int sgm41515_chg_init_psy(struct sgm41515_chip *chip)
{
	struct power_supply_config cfg = {
		.drv_data = chip,
		.of_node = chip->dev->of_node,
		.supplied_to = sgm41515_charger_supplied_to,
		.num_supplicants = ARRAY_SIZE(sgm41515_charger_supplied_to),
	};

	chg_err("%s\n", __func__);
	memcpy(&chip->psy_desc, &sgm41515_charger_desc, sizeof(chip->psy_desc));
	chip->psy_desc.name = "sgm41515";
	chip->chg_psy = devm_power_supply_register(chip->dev, &chip->psy_desc, &cfg);
	return IS_ERR(chip->chg_psy) ? PTR_ERR(chip->chg_psy) : 0;
}

static int sgm41515_parse_dt(struct sgm41515_chip *chip)
{
	if (of_property_read_string(chip->client->dev.of_node, "charger_name", &chip->chg_dev_name) < 0) {
		chip->chg_dev_name = "primary_chg";
		chg_err("no charger name\n");
	}
	return 0;
}
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0))
static int sgm41515_driver_probe(struct i2c_client *client)
#else
static int sgm41515_driver_probe(struct i2c_client *client,
				 const struct i2c_device_id *id)
#endif
{
	int rc = 0;
	struct sgm41515_chip *chip;
	struct device_node *node = client->dev.of_node;
	struct oplus_chg_ic_cfg ic_cfg = { 0 };
	enum oplus_chg_ic_type ic_type;
	int ic_index;

	chip = devm_kzalloc(&client->dev, sizeof(struct sgm41515_chip),
			    GFP_KERNEL);
	if (!chip) {
		chg_err("kzalloc failed\n");
		return -ENOMEM;
	}

	chip->client = client;
	chip->dev = &client->dev;
	i2c_set_clientdata(client, chip);
	mutex_init(&chip->i2c_lock);
	mutex_init(&chip->dpdm_lock);
	mutex_init(&chip->pinctrl_lock);
	INIT_DELAYED_WORK(&chip->event_work, sgm41515_event_work);
	INIT_DELAYED_WORK(&chip->bc12_timeout_work, sgm41515_bc12_timeout_work);
	INIT_DELAYED_WORK(&chip->bc12_retry_work, sgm41515_bc12_retry_work);

	chip->dpdm_reg = devm_regulator_get_optional(chip->dev, "dpdm");
	if (IS_ERR(chip->dpdm_reg)) {
		rc = PTR_ERR(chip->dpdm_reg);
		chg_err("Couldn't get dpdm regulator, rc=%d\n", rc);
		chip->dpdm_reg = NULL;
	}

	chip->regmap = devm_regmap_init_i2c(client, &sgm41515_regmap_config);
	if (!chip->regmap) {
		rc = -ENODEV;
		goto regmap_init_err;
	}

	atomic_set(&chip->charger_suspended, 0);
	atomic_set(&chip->is_suspended, 0);
	oplus_chg_awake_init(chip);
	init_waitqueue_head(&chip->wait);
	oplus_keep_resume_awake_init(chip);
	chip->charge_type = POWER_SUPPLY_TYPE_UNKNOWN;
	chip->power_good = false;
	chip->bc12_complete = false;
	chip->bc12_retried = 0;
	chip->bc12_delay_cnt = 0;

	rc = sgm41515_get_deivce_online(chip);
	if (rc < 0) {
		chg_err("!!!sgm41515 is not detected\n");
		goto gpio_init_err;
	}

	rc = sgm41515_gpio_init(chip);
	if (rc < 0) {
		chg_err("gpio init error, rc=%d\n", rc);
		goto gpio_init_err;
	}

	sgm41515_hw_init(chip);

#ifdef CONFIG_OPLUS_CHARGER_MTK
	sgm41515_parse_dt(chip);
	rc = sgm41515_chg_init_psy(chip);
	if (rc)
		chg_err("Failed to register sgm41515 ret=%d\n", rc);

	chip->chg_dev = charger_device_register(chip->chg_dev_name,
						&client->dev, chip,
						&sgm41515_chg_ops,
						&sgm41515_chg_props);
	if (IS_ERR_OR_NULL(chip->chg_dev)) {
		rc = PTR_ERR(chip->chg_dev);
		goto err_device_register;
	}
#endif

	rc = of_property_read_u32(node, "oplus,ic_type", &ic_type);
	if (rc < 0) {
		chg_err("can't get ic type, rc=%d\n", rc);
		goto reg_ic_err;
	}
	rc = of_property_read_u32(node, "oplus,ic_index", &ic_index);
	if (rc < 0) {
		chg_err("can't get ic index, rc=%d\n", rc);
		goto reg_ic_err;
	}
	ic_cfg.name = node->name;
	ic_cfg.index = ic_index;
	snprintf(ic_cfg.manu_name, OPLUS_CHG_IC_MANU_NAME_MAX - 1, "buck-sgm41515");
	snprintf(ic_cfg.fw_id, OPLUS_CHG_IC_FW_ID_MAX - 1, "0x00");
	ic_cfg.type = ic_type;
	ic_cfg.get_func = oplus_chg_get_func;
	ic_cfg.virq_data = sgm41515_virq_table;
	ic_cfg.virq_num = ARRAY_SIZE(sgm41515_virq_table);
	ic_cfg.of_node = node;
	chip->ic_dev =
		devm_oplus_chg_ic_register(chip->dev, &ic_cfg);
	if (!chip->ic_dev) {
		rc = -ENODEV;
		chg_err("register %s error\n", node->name);
		goto reg_ic_err;
	}

	oplus_mms_wait_topic("wired", sgm41515_subscribe_wired_topic, chip);

	sgm41515_enable_irq(chip, true);
	chg_info("success\n");

	return rc;

reg_ic_err:
	sgm41515_enable_irq(chip, false);
	if (gpio_is_valid(chip->event_gpio))
		gpio_free(chip->event_gpio);
#ifdef CONFIG_OPLUS_CHARGER_MTK
err_device_register:
#endif
gpio_init_err:
regmap_init_err:
	i2c_set_clientdata(client, NULL);
	devm_kfree(&client->dev, chip);
	chg_err("probe error, rc=%d\n", rc);
	return rc;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0))
static void sgm41515_driver_remove(struct i2c_client *client)
#else
static int sgm41515_driver_remove(struct i2c_client *client)
#endif
{
	struct sgm41515_chip *chip = i2c_get_clientdata(client);

	if (chip) {
		sgm41515_enable_irq(chip, false);
#ifdef CONFIG_OPLUS_CHARGER_MTK
		if (chip->chg_dev)
			charger_device_unregister(chip->chg_dev);
#endif
		if (!IS_ERR_OR_NULL(chip->wired_subs))
			oplus_mms_unsubscribe(chip->wired_subs);
		if (chip->ic_dev)
			devm_oplus_chg_ic_unregister(chip->dev, chip->ic_dev);
		chip->ic_dev = NULL;
		if (chip->suspend_ws)
			wakeup_source_unregister(chip->suspend_ws);
		chip->suspend_ws = NULL;
		if (chip->event_irq)
			free_irq(chip->event_irq, chip);
		if (gpio_is_valid(chip->event_gpio))
			gpio_free(chip->event_gpio);
		mutex_destroy(&chip->dpdm_lock);
		mutex_destroy(&chip->i2c_lock);
		i2c_set_clientdata(client, NULL);
		devm_kfree(&client->dev, chip);
	}
#if (LINUX_VERSION_CODE < KERNEL_VERSION(6, 1, 0))
	return 0;
#endif
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))
static int sgm41515_pm_resume(struct device *dev)
{
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);
	struct sgm41515_chip *chip = i2c_get_clientdata(client);

	if (!chip) {
		chg_err("chip is null\n");
		return 0;
	}

	atomic_set(&chip->charger_suspended, 0);
	return 0;
}

static int sgm41515_pm_suspend(struct device *dev)
{
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);
	struct sgm41515_chip *chip = i2c_get_clientdata(client);

	if (!chip) {
		chg_err("chip is null\n");
		return 0;
	}

	atomic_set(&chip->charger_suspended, 1);
	return 0;
}

static const struct dev_pm_ops sgm41515_pm_ops = {
	.resume = sgm41515_pm_resume,
	.suspend = sgm41515_pm_suspend,
};
#else
static int sgm41515_resume(struct i2c_client *client)
{
	struct sgm41515_chip *chip = i2c_get_clientdata(client);

	if (!chip) {
		chg_err("chip is null\n");
		return 0;
	}

	atomic_set(&chip->charger_suspended, 0);
	return 0;
}

static int sgm41515_suspend(struct i2c_client *client, pm_message_t mesg)
{
	struct sgm41515_chip *chip = i2c_get_clientdata(client);

	if (!chip) {
		chg_err("chip is null\n");
		return 0;
	}

	atomic_set(&chip->charger_suspended, 1);
	return 0;
}
#endif

static void sgm41515_shutdown(struct i2c_client *client)
{
	struct sgm41515_chip *chip = i2c_get_clientdata(client);

	if (!chip) {
		chg_err("chip is null\n");
		return;
	}

	if(oplus_wired_shipmode_is_enabled()) {
		chg_info("oplus_wired_shipmode_is_enabled\n");
		sgm41515_set_shipmode(chip, true);
	}

	/*
	 * HIZ mode needs to be disabled on shutdown to ensure activation
	 * signal is available.
	 */
	if (READ_ONCE(chip->vbus_present))
		(void)sgm41515_write_byte_mask(chip, REG00_SGM41515_ADDRESS, REG00_SGM41515_HIZ_MODE_MASK,
						REG00_SGM41515_HIZ_MODE_DISABLE);
}

static const struct of_device_id sgm41515_match[] = {
	{ .compatible = "oplus,sgm41515-charger" },
	{},
};

static const struct i2c_device_id sgm41515_id[] = {
	{ "sgm41515-charger", 0 },
	{},
};
MODULE_DEVICE_TABLE(i2c, sgm41515_id);

static struct i2c_driver sgm41515_i2c_driver = {
	.driver		= {
		.name = "sgm41515-charger",
		.owner	= THIS_MODULE,
		.of_match_table = sgm41515_match,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))
		.pm 	= &sgm41515_pm_ops,
#endif
	},
	.probe		= sgm41515_driver_probe,
	.remove		= sgm41515_driver_remove,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 4, 0))
	.resume		= sgm41515_resume,
	.suspend	= sgm41515_suspend,
#endif
	.shutdown	= sgm41515_shutdown,
	.id_table	= sgm41515_id,
};

int sgm41515_driver_init(void)
{
	int rc;

	rc = i2c_add_driver(&sgm41515_i2c_driver);
	if (rc < 0)
		chg_err("failed to register sgm41515 i2c driver, rc=%d\n", rc);
	else
		chg_debug("Success to register sgm41515 i2c driver.\n");

	return rc;
}

void sgm41515_driver_exit(void)
{
	i2c_del_driver(&sgm41515_i2c_driver);
}
oplus_chg_module_register(sgm41515_driver);

MODULE_DESCRIPTION("Driver for sgm41515 charger chip");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("i2c:sgm41515-charger");

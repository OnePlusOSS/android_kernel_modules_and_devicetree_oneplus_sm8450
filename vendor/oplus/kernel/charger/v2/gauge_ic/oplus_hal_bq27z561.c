// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024-2024 Oplus. All rights reserved.
 */

#define pr_fmt(fmt) "[BQ27Z561]([%s][%d]): " fmt, __func__, __LINE__

#ifdef CONFIG_OPLUS_CHARGER_MTK
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/irq.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/workqueue.h>
#include <linux/kobject.h>
#include <linux/platform_device.h>
#include <asm/atomic.h>
#include <asm/unaligned.h>
#include <linux/module.h>
#include <linux/power_supply.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>

#include <linux/proc_fs.h>
#include <linux/init.h>
#else
#include <linux/i2c.h>
#include <linux/debugfs.h>
#include <linux/gpio.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/power_supply.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/bitops.h>
#include <linux/mutex.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/machine.h>
#include <linux/proc_fs.h>
#include <linux/soc/qcom/smem.h>
#endif
#ifndef CONFIG_DISABLE_OPLUS_FUNCTION
#include <soc/oplus/device_info.h>
#endif
#include <linux/version.h>
#include<linux/gfp.h>
#include <linux/pinctrl/consumer.h>
#ifdef OPLUS_SHA1_HMAC
#include <linux/random.h>
#endif
#include <oplus_chg_module.h>
#include <oplus_chg_comm.h>
#include <oplus_chg_vooc.h>
#include <oplus_mms.h>
#include <oplus_mms_gauge.h>
#include <oplus_mms_wired.h>
#include "test-kit.h"
#include "oplus_hal_bq27z561.h"

#define GAUGE_ERROR		(-1)
#define GAUGE_OK		0
#define BATT_FULL_ERROR		2
#define XBL_AUTH_DEBUG
#define BCC_TYPE_IS_SVOOC	1
#define BCC_TYPE_IS_VOOC	0
#define VOLT_MIN		1000
#define VOLT_MAX		5000
#define CURR_MAX		25000
#define CURR_MIN		(-20000)
#define TEMP_MAX		(1000 - ZERO_DEGREE_CELSIUS_IN_TENTH_KELVIN)
#define TEMP_MIN		(-400 - ZERO_DEGREE_CELSIUS_IN_TENTH_KELVIN)
#define SOH_MIN			0
#define SOH_MAX			100
#define FCC_MIN			10
#define FCC_MAX			12000
#define CC_MIN			0
#define CC_MAX			5000
#define QMAX_MIN		10
#define QMAX_MAX		12000
#define SOC_MIN			0
#define SOC_MAX			100
#define RETRY_CNT		3
#define REVE_CURR		0x8000

#ifndef I2C_ERR_MAX
#define I2C_ERR_MAX 		2
#endif

int oplus_vooc_get_fastchg_ing(void);
static bool init_sha256_gauge_auth(struct chip_bq27z561 *chip);

static int bq27z561_get_battery_soc(struct chip_bq27z561 *chip);
static int bq27z561_get_battery_mvolts(struct chip_bq27z561 *chip);
static int bq27z561_get_battery_temperature(struct chip_bq27z561 *chip);
static int bq27z561_get_average_current(struct chip_bq27z561 *chip);
static int bq27z561_i2c_txsubcmd(struct chip_bq27z561 *chip, int cmd, int writeData);
static int bq27z561_read_i2c(struct chip_bq27z561 *chip, int cmd, int *returnData);
static int bq27z561_read_i2c_block(struct chip_bq27z561 *chip, u8 cmd, u8 length, u8 *returnData);
static int bq27z561_write_i2c_block(struct chip_bq27z561 *chip, u8 cmd, u8 length, u8 *writeData);
static int bq27z561_i2c_txsubcmd_onebyte(struct chip_bq27z561 *chip, u8 cmd, u8 writeData);
#ifdef CONFIG_OPLUS_CHARGER_MTK
static const char *oplus_chg_get_cmdline(const char *target_str);
#endif

#ifndef CONFIG_OPLUS_CHARGER_MTK
#if IS_ENABLED(CONFIG_OPLUS_ADSP_CHARGER)
void __attribute__((weak)) oplus_vooc_get_fastchg_started_pfunc(int (*pfunc)(void))
{
	return;
}
void __attribute__((weak)) oplus_vooc_get_fastchg_ing_pfunc(int (*pfunc)(void))
{
	return;
}
int __attribute__((weak)) oplus_get_fg_device_type(void)
{
	return 0;
}

void __attribute__((weak)) oplus_set_fg_device_type(int device_type)
{
	return;
}
#else /*IS_ENABLED(CONFIG_OPLUS_ADSP_CHARGER)*/
void __attribute__((weak)) oplus_vooc_get_fastchg_started_pfunc(int (*pfunc)(void));
void __attribute__((weak)) oplus_vooc_get_fastchg_ing_pfunc(int (*pfunc)(void));
int __attribute__((weak)) oplus_get_fg_device_type(void);
void __attribute__((weak)) oplus_set_fg_device_type(int device_type);
#endif /*IS_ENABLED(CONFIG_OPLUS_ADSP_CHARGER)*/
#else
int __attribute__((weak)) oplus_get_fg_device_type(void)
{
	return 0;
}

void __attribute__((weak)) oplus_set_fg_device_type(int device_type)
{
	return;
}
#endif

#ifdef CONFIG_OPLUS_CHARGER_MTK
/* workaround for I2C pull SDA can't trigger error issue 230504153935012779 */
#ifdef CONFIG_OPLUS_FG_ERROR_RESET_I2C
/* this workaround only for flamingo, for scanning tool issue */
void __attribute__((weak)) oplus_set_fg_err_flag(struct i2c_adapter *adap, bool flag);
#endif
/* end workaround 230504153935012779 */
#endif
/**********************************************************
  *
  *   [I2C Function For Read/Write bq27z561]
  *
  *********************************************************/

static bool normal_range_judge(int max, int min, int data)
{
	if (data <= max && data >= min)
		return true;
	else
		return false;
}

static int oplus_bq27z561_init(struct oplus_chg_ic_dev *ic_dev)
{
	ic_dev->online = true;

	return 0;
}

static int oplus_bq27z561_exit(struct oplus_chg_ic_dev *ic_dev)
{
	if (!ic_dev->online)
		return 0;

	ic_dev->online = false;

	return 0;
}

static int bq27z561_sealed(struct chip_bq27z561 *chip)
{
	int rc = 0;
	u8 seal_op_st[2] = { 0x54, 0x00 };
	u8 read_buf[6] = { 0 };

	if (!chip)
		return -EINVAL;

	bq27z561_write_i2c_block(chip, BQ28Z610_REG_CNTL1, 2, seal_op_st);
	usleep_range(50000, 50000);
	bq27z561_read_i2c_block(chip, BQ28Z610_REG_CNTL1, 6, read_buf);

	if ((read_buf[3] & 0x01) == 0) {
		rc = 1;
		chg_info("bq27z561 is unseal\n");
	} else {
		rc = 0;
		chg_info("bq27z561 is seal\n");
	}
	return rc;
}

static int bq27z561_i2c_deep_int(struct chip_bq27z561 *chip)
{
	int rc = 0;
	int retry = 5;

	if (!chip)
		return -EINVAL;

	do {
		/* need to delay 2s before write 0x0414, delay 1s before write 0x3672
		 * make sure unseal Success rate
		 */
		mutex_lock(&chip->chip_mutex);
		i2c_smbus_write_word_data(chip->client, BQ28Z610_REG_CNTL1, BQ28Z610_UNSEAL_SUBCMD1);
		usleep_range(1000000, 1000000);
		i2c_smbus_write_word_data(chip->client, BQ28Z610_REG_CNTL1, BQ28Z610_UNSEAL_SUBCMD2);
		mutex_unlock(&chip->chip_mutex);

		rc = bq27z561_sealed(chip);
		if (rc == 1) {
			retry = 0;
		} else {
			retry--;
			usleep_range(2000000, 2000000);
			chg_info("bq27z561_unseal retry:%d, rc:%d\n", retry, rc);
		}
	} while (retry > 0);
	chg_info("bq27z561_unseal flag [%d]\n", rc);

	return rc;
}


static bool bq27z561_deep_init(struct chip_bq27z561 *chip)
{
	int ret = 0;

	if (!chip) {
		chg_err("chip is NULL\n");
		return false;
	}
	if (oplus_is_rf_ftm_mode())
		return false;

	ret = bq27z561_sealed(chip);
	if (ret == 1) {
		chg_info("bq27z561 is already unseal\n");
		return true;
	}
	/* need delay 1s before unseal to make sure unseal Success rate */
	msleep(1000);
	ret = bq27z561_i2c_deep_int(chip);
	if (ret <= 0) {
		chg_err("bq27z561 unseal failed\n");
		return false;
	}

	chg_info("bq27z561 unseal success\n");
	return true;
}

static int bq27z561_deep_deinit(struct chip_bq27z561 *chip)
{
	int ret;

	usleep_range(1000, 1000);
	bq27z561_i2c_txsubcmd(chip, BQ28Z610_REG_CNTL1, BQ28Z610_SEAL_SUBCMD);
	/* need delay 100ms make sure seal is done */
	usleep_range(100000, 100000);

	ret = bq27z561_sealed(chip);
	if (ret == 0)
		chg_info("bq27z561 seal success\n");
	else
		chg_info("bq27z561 seal fail\n");

	return 0;
}

#define CHECK_IIC_RECOVER_TIME 5000
static __inline__ void bq27z561_push_i2c_err(struct chip_bq27z561 *chip, bool read)
{
	if (unlikely(!chip->ic_dev))
		return;
	if (unlikely(!chip->ic_dev->online))
		return;

	chip->i2c_err = true;
	if (atomic_read(&chip->i2c_err_count) > I2C_ERR_MAX)
		return;

	atomic_inc(&chip->i2c_err_count);
	if (atomic_read(&chip->i2c_err_count) > I2C_ERR_MAX) {
		oplus_bq27z561_exit(chip->ic_dev);
		oplus_chg_ic_virq_trigger(chip->ic_dev, OPLUS_IC_VIRQ_OFFLINE);
		schedule_delayed_work(&chip->check_iic_recover, msecs_to_jiffies(CHECK_IIC_RECOVER_TIME));
	} else {
		oplus_chg_ic_creat_err_msg(chip->ic_dev, OPLUS_IC_ERR_I2C, 0,
					   read ? "read error" : "write error");
		oplus_chg_ic_virq_trigger(chip->ic_dev, OPLUS_IC_VIRQ_ERR);
	}
}

static __inline__ void bq27z561_i2c_err_clr(struct chip_bq27z561 *chip)
{
	if (unlikely(chip->i2c_err)) {
		/* workaround for I2C pull SDA can't trigger error issue 230504153935012779 */
		if (chip->i2c_rst_ext && chip->err_status)
			return;
		/* end workaround 230504153935012779 */
		chip->i2c_err = false;
		atomic_set(&chip->i2c_err_count, 0);
		oplus_bq27z561_init(chip->ic_dev);
		oplus_chg_ic_virq_trigger(chip->ic_dev, OPLUS_IC_VIRQ_ONLINE);
	}
}

static int bq27z561_i2c_txsubcmd(struct chip_bq27z561 *chip, int cmd, int write_data)
{
	int rc = 0;
	int retry = 3;

	if (!chip->client) {
		chg_err(" gauge_ic->client NULL, return\n");
		return -ENODEV;
	}

	if (oplus_is_rf_ftm_mode())
		return 0;

	if (cmd == BQ27Z561_CMD_INVALID)
		return 0;

	mutex_lock(&chip->chip_mutex);
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_CHG_DEBUG_KIT)
	rc = oplus_dev_bus_bulk_write(chip->odb, cmd, &write_data, 2);
#else
	rc = i2c_smbus_write_word_data(chip->client, cmd, write_data);
#endif
	if (rc < 0) {
		while(retry > 0) {
			usleep_range(5000, 5000);
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_CHG_DEBUG_KIT)
			rc = oplus_dev_bus_bulk_write(chip->odb, cmd, &write_data, 2);
#else
			rc = i2c_smbus_write_word_data(chip->client, cmd, write_data);
#endif
			if (rc < 0)
				retry--;
			else
				break;
		}
	}

	if (rc < 0) {
		chg_err("write err, rc = %d\n", rc);
		bq27z561_push_i2c_err(chip, false);
		mutex_unlock(&chip->chip_mutex);
		return -EINVAL;
	} else {
		bq27z561_i2c_err_clr(chip);
	}
	mutex_unlock(&chip->chip_mutex);
	return 0;
}

static int bq27z561_write_i2c_block(struct chip_bq27z561 *chip, u8 cmd, u8 length, u8 *write_data)
{
	int rc = 0;
	int retry = 3;

	if (!chip->client) {
		chg_err(" gauge_ic->client NULL, return\n");
		return -ENODEV;
	}

	if (oplus_is_rf_ftm_mode())
		return 0;

	if (cmd == BQ27Z561_CMD_INVALID)
		return 0;

	mutex_lock(&chip->chip_mutex);
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_CHG_DEBUG_KIT)
	rc = oplus_dev_bus_bulk_write(chip->odb, cmd, write_data, length);
#else
	rc = i2c_smbus_write_i2c_block_data(chip->client, cmd, length, write_data);
#endif
	if (rc < 0) {
		while(retry > 0) {
			usleep_range(5000, 5000);
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_CHG_DEBUG_KIT)
			rc = oplus_dev_bus_bulk_write(chip->odb, cmd, write_data, length);
#else
			rc = i2c_smbus_write_i2c_block_data(chip->client,
				cmd, length, write_data);
#endif
			if (rc < 0)
				retry--;
			else
				break;
		}
	}

	if (rc < 0) {
		chg_err("write err, rc = %d\n", rc);
		bq27z561_push_i2c_err(chip, false);
	} else {
		bq27z561_i2c_err_clr(chip);
	}
	mutex_unlock(&chip->chip_mutex);
	return 0;
}

static int bq27z561_read_i2c_block(struct chip_bq27z561 *chip, u8 cmd, u8 length, u8 *returnData)
{
	int rc = 0;
	int retry = 3;

	if (!chip->client) {
		chg_err(" gauge_ic->client NULL,return\n");
		return -ENODEV;
	}

	if (oplus_is_rf_ftm_mode())
		return 0;

	if (cmd == BQ27Z561_CMD_INVALID)
		return 0;

	mutex_lock(&chip->chip_mutex);
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_CHG_DEBUG_KIT)
	rc = oplus_dev_bus_bulk_read(chip->odb, cmd, returnData, length);
#else
	rc = i2c_smbus_read_i2c_block_data(chip->client, cmd, length, returnData);
#endif
	if (rc < 0) {
		while(retry > 0) {
			usleep_range(5000, 5000);
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_CHG_DEBUG_KIT)
			rc = oplus_dev_bus_bulk_read(chip->odb, cmd, returnData, length);
#else
			rc = i2c_smbus_read_i2c_block_data(chip->client, cmd, length, returnData);
#endif
			if (rc < 0)
				retry--;
			else
				break;
		}
	}

	if (rc < 0) {
		chg_err("read err, rc = %d\n", rc);
		bq27z561_push_i2c_err(chip, true);
		mutex_unlock(&chip->chip_mutex);
		return -EINVAL;
	} else {
		bq27z561_i2c_err_clr(chip);
	}
	mutex_unlock(&chip->chip_mutex);
	return 0;
}

static int bq27z561_read_i2c_onebyte(struct chip_bq27z561 *chip, u8 cmd, u8 *returnData)
{
	int rc;
	int retry = 3;
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_CHG_DEBUG_KIT)
	unsigned int buf;
#endif

	if (!chip->client) {
		chg_err(" gauge_ic->client NULL, return\n");
		return -ENODEV;
	}

	if (oplus_is_rf_ftm_mode())
		return 0;

	if (cmd == BQ27Z561_CMD_INVALID)
		return 0;

	mutex_lock(&chip->chip_mutex);
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_CHG_DEBUG_KIT)
	rc = oplus_dev_bus_read(chip->odb, cmd, &buf);
	if (rc >= 0)
		rc = buf;
#else
	rc = i2c_smbus_read_byte_data(chip->client, cmd);
#endif
	if (rc < 0) {
		while(retry > 0) {
			usleep_range(5000, 5000);
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_CHG_DEBUG_KIT)
			rc = oplus_dev_bus_read(chip->odb, cmd, &buf);
			if (rc >= 0)
				rc = buf;
#else
			rc = i2c_smbus_read_byte_data(chip->client, cmd);
#endif
			if (rc < 0) {
				retry--;
			} else {
				*returnData = (u8)rc;
				break;
			}
		}
	} else {
		*returnData = (u8)rc;
	}
	mutex_unlock(&chip->chip_mutex);
	if (rc < 0) {
		chg_err("read err, rc = %d\n", rc);
		bq27z561_push_i2c_err(chip, true);
		return 1;
	} else {
		bq27z561_i2c_err_clr(chip);
		return 0;
	}
}

static int bq27z561_i2c_txsubcmd_onebyte(struct chip_bq27z561 *chip, u8 cmd, u8 writeData)
{
	int rc = 0;
	int retry = 3;

	if (!chip->client) {
		chg_err(" gauge_ic->client NULL, return\n");
		return -ENODEV;
	}

	if (oplus_is_rf_ftm_mode())
		return 0;

	if (cmd == BQ27Z561_CMD_INVALID)
		return 0;

	mutex_lock(&chip->chip_mutex);
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_CHG_DEBUG_KIT)
	rc = oplus_dev_bus_write(chip->odb, cmd, writeData);
#else
	rc = i2c_smbus_write_byte_data(chip->client, cmd, writeData);
#endif
	if (rc < 0) {
		while(retry > 0) {
			usleep_range(5000, 5000);
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_CHG_DEBUG_KIT)
			rc = oplus_dev_bus_write(chip->odb, cmd, writeData);
#else
			rc = i2c_smbus_write_byte_data(chip->client, cmd, writeData);
#endif
			if (rc < 0)
				retry--;
			else
				break;
		}
	}

	if (rc < 0) {
		chg_err("write err, rc = %d\n", rc);
		bq27z561_push_i2c_err(chip, true);
	} else {
		bq27z561_i2c_err_clr(chip);
	}
	mutex_unlock(&chip->chip_mutex);
	return 0;
}

static u8 bq27z561_calc_checksum(u8 *buf, int len)
{
	u8 checksum = 0;

	while (len--)
		checksum += buf[len];

	return 0xff - checksum;
}

static int bq27z561_block_check_conditions(struct chip_bq27z561 *chip, u8 *buf, int len, int offset, bool do_checksum)
{
	if (!chip || !buf || (offset < 0) || (offset >= BQ27Z561_BLOCK_SIZE) || (len <= 0) ||
	    (len + do_checksum > BQ27Z561_BLOCK_SIZE) || (offset + len + do_checksum > BQ27Z561_BLOCK_SIZE)) {
		chg_err("%soffset %d or len %d invalid\n", buf ? "buf is null or " : "", offset, len);
		return -EINVAL;
	}

	if (atomic_read(&chip->locked))
		return -EINVAL;

	return 0;
}

int bq27z561_read_block(struct chip_bq27z561 *chip, int addr, u8 *buf, int len, int offset, bool do_checksum)
{
	int ret;
	int data_check;
	int try_count = GAUGE_SUBCMD_TRY_COUNT;
	u8 extend_data[BQ27Z561_EXTEND_DATA_SIZE] = { 0 };
	u8 checksum = 0;

	ret = bq27z561_block_check_conditions(chip, buf, len, offset, do_checksum);
	if (ret < 0)
		return ret;

try:
	mutex_lock(&chip->bq27z561_alt_manufacturer_access);
	ret = bq27z561_i2c_txsubcmd(chip, GAUGE_EXTERN_DATAFLASHBLOCK, addr);
	if (ret < 0)
		goto error;
	usleep_range(1000, 1000);
	ret = bq27z561_read_i2c_block(chip, GAUGE_EXTERN_DATAFLASHBLOCK, (offset + len + do_checksum + 2), extend_data);
	if (ret < 0)
		goto error;

	data_check = (extend_data[1] << 0x8) | extend_data[0];
	if (try_count-- > 0 && data_check != addr) {
		chg_err("0x%04x not match. try_count=%d extend_data[0]=0x%2x, extend_data[1]=0x%2x\n", addr, try_count,
			extend_data[0], extend_data[1]);
		mutex_unlock(&chip->bq27z561_alt_manufacturer_access);
		usleep_range(2000, 2000);
		goto try;
	}
	if (data_check != addr)
		goto error;

	if (do_checksum) {
		checksum = bq27z561_calc_checksum(&extend_data[offset + 2], len);
		if (checksum != extend_data[offset + len + 2]) {
			chg_err("[%*ph]checksum not match. expect=0x%02x actual=0x%02x\n",
				offset + len + do_checksum + 2, extend_data, checksum, extend_data[offset + len + 2]);
			goto error;
		}
	}

	memcpy(buf, &extend_data[offset + 2], len);
	chg_info("addr=0x%04x offset=%d buf=[%*ph] do_checksum=%d read success\n", addr, offset, len, buf, do_checksum);
	mutex_unlock(&chip->bq27z561_alt_manufacturer_access);
	return 0;

error:
	chg_info("addr=0x%04x offset=%d buf=[%*ph] do_checksum=%d read fail\n", addr, offset, len, buf, do_checksum);
	mutex_unlock(&chip->bq27z561_alt_manufacturer_access);
	return -EINVAL;
}

static int bq27z561_read_i2c(struct chip_bq27z561 *chip, int cmd, int *returnData)
{
	int retry = 4;
	int retry_c = 3;
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_CHG_DEBUG_KIT)
	int rc;
#endif

	if (!chip->client) {
		chg_err(" gauge_ic->client NULL, return\n");
		return -ENODEV;
	}

	if (oplus_is_rf_ftm_mode())
		return 0;

	if (cmd == BQ27Z561_CMD_INVALID)
		return 0;

	mutex_lock(&chip->chip_mutex);
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_CHG_DEBUG_KIT)
	rc = oplus_dev_bus_bulk_read(chip->odb, cmd, returnData, 2);
	if (rc < 0)
		*returnData = rc;
#else
	*returnData = i2c_smbus_read_word_data(chip->client, cmd);
#endif
	if (cmd == BQ27Z561_REG_CNTL) {
		if (*returnData < 0) {
			while (retry > 0) {
				usleep_range(5000, 5000);
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_CHG_DEBUG_KIT)
				rc = oplus_dev_bus_bulk_read(chip->odb, cmd, returnData, 2);
				if (rc < 0)
					*returnData = rc;
#else
				*returnData = i2c_smbus_read_word_data(chip->client, cmd);
#endif
				if (*returnData < 0)
					retry--;
				else
					break;
			}
		}
	} else {
		if (*returnData < 0) {
			while(retry_c > 0) {
				usleep_range(5000, 5000);
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_CHG_DEBUG_KIT)
				rc = oplus_dev_bus_bulk_read(chip->odb, cmd, returnData, 2);
				if (rc < 0)
					*returnData = rc;
#else
				*returnData = i2c_smbus_read_word_data(chip->client, cmd);
#endif
				if (*returnData < 0)
					retry_c--;
				else
					break;
			}
		}
	}

	mutex_unlock(&chip->chip_mutex);
	if (*returnData < 0) {
		chg_err("read err, rc = %d\n", *returnData);
#ifdef CONFIG_OPLUS_CHARGER_MTK
		if (chip->i2c_rst_ext) {
			chip->err_status = false;
#ifdef CONFIG_OPLUS_FG_ERROR_RESET_I2C
/* this workaround only for flamingo, for scanning tool issue */
			oplus_set_fg_err_flag(chip->client->adapter, false);
#endif
		}
#endif
		bq27z561_push_i2c_err(chip, true);
		return 1;
	} else {
		bq27z561_i2c_err_clr(chip);
		return 0;
	}
}

static void bq27z561_cntl_cmd(struct chip_bq27z561 *chip, int subcmd)
{
	bq27z561_i2c_txsubcmd(chip, BQ27Z561_REG_CNTL, subcmd);
}

static void gauge_set_cmd_addr(struct chip_bq27z561 *chip)
{
	chip->cmd_addr.reg_cntl = BQ27Z561_REG_CNTL;
	chip->cmd_addr.reg_temp = BQ27Z561_REG_TEMP;
	chip->cmd_addr.reg_volt = BQ27Z561_REG_VOLT;
	chip->cmd_addr.reg_flags = BQ27Z561_REG_FLAGS;
	chip->cmd_addr.reg_nac = BQ27Z561_REG_NAC;
	chip->cmd_addr.reg_fac = BQ27Z561_REG_FAC;
	chip->cmd_addr.reg_rm = BQ27Z561_REG_RM;
	chip->cmd_addr.reg_fcc = BQ27Z561_REG_FCC;
	chip->cmd_addr.reg_ai = BQ27Z561_REG_AI;
	chip->cmd_addr.reg_si = BQ27Z561_REG_SI;
	chip->cmd_addr.reg_mli = BQ27Z561_REG_MLI;
	chip->cmd_addr.reg_ap = BQ27Z561_REG_AP;
	chip->cmd_addr.reg_soc = BQ27Z561_REG_SOC;
	chip->cmd_addr.reg_inttemp = BQ27Z561_REG_INTTEMP;
	chip->cmd_addr.reg_soh = BQ27Z561_REG_SOH;
	chip->cmd_addr.flag_dsc = BQ27Z561_FLAG_DSC;
	chip->cmd_addr.flag_fc = BQ27Z561_FLAG_FC;
	chip->cmd_addr.cs_dlogen = BQ27Z561_CS_DLOGEN;
	chip->cmd_addr.cs_ss = BQ27Z561_CS_SS;
	chip->cmd_addr.reg_ar = BQ27Z561_REG_AR;
	chip->cmd_addr.reg_artte = BQ27Z561_REG_ARTTE;
	chip->cmd_addr.reg_tte = BQ27Z561_REG_TTE;
	chip->cmd_addr.reg_ttf = BQ27Z561_REG_TTF;
	chip->cmd_addr.reg_stte = BQ27Z561_REG_STTE;
	chip->cmd_addr.reg_mltte = BQ27Z561_REG_MLTTE;
	chip->cmd_addr.reg_ae = BQ27Z561_REG_AE;
	chip->cmd_addr.reg_ttecp = BQ27Z561_REG_TTECP;
	chip->cmd_addr.reg_cc = BQ27Z561_REG_CC;
	chip->cmd_addr.reg_nic = BQ27Z561_REG_NIC;
	chip->cmd_addr.reg_icr = BQ27Z561_REG_ICR;
	chip->cmd_addr.reg_logidx = BQ27Z561_REG_LOGIDX;
	chip->cmd_addr.reg_logbuf = BQ27Z561_REG_LOGBUF;
	chip->cmd_addr.reg_dod0 = BQ27Z561_REG_DOD0;
	chip->cmd_addr.subcmd_cntl_status = BQ27Z561_SUBCMD_CTNL_STATUS;
	chip->cmd_addr.subcmd_device_type = BQ27Z561_SUBCMD_DEVICE_TYPE;
	chip->cmd_addr.subcmd_fw_ver = BQ27Z561_SUBCMD_FW_VER;
	chip->cmd_addr.subcmd_dm_code = BQ27Z561_CMD_INVALID;
	chip->cmd_addr.subcmd_prev_macw = BQ27Z561_SUBCMD_PREV_MACW;
	chip->cmd_addr.subcmd_chem_id = BQ27Z561_SUBCMD_CHEM_ID;
	chip->cmd_addr.subcmd_set_hib = BQ27Z561_SUBCMD_SET_HIB;
	chip->cmd_addr.subcmd_clr_hib = BQ27Z561_SUBCMD_CLR_HIB;
	chip->cmd_addr.subcmd_set_cfg = BQ27Z561_CMD_INVALID;
	chip->cmd_addr.subcmd_sealed = BQ27Z561_SUBCMD_SEALED;
	chip->cmd_addr.subcmd_reset = BQ27Z561_SUBCMD_RESET;
	chip->cmd_addr.subcmd_softreset = BQ27Z561_CMD_INVALID;
	chip->cmd_addr.subcmd_exit_cfg = BQ27Z561_CMD_INVALID;
	chip->cmd_addr.subcmd_enable_dlog = BQ27Z561_SUBCMD_ENABLE_DLOG;
	chip->cmd_addr.subcmd_disable_dlog = BQ27Z561_SUBCMD_DISABLE_DLOG;
	chip->cmd_addr.subcmd_enable_it = BQ27Z561_SUBCMD_ENABLE_IT;
	chip->cmd_addr.subcmd_disable_it = BQ27Z561_SUBCMD_DISABLE_IT;
	chip->cmd_addr.subcmd_hw_ver = BQ27Z561_SUBCMD_HW_VER;
	chip->cmd_addr.subcmd_df_csum = BQ27Z561_SUBCMD_DF_CSUM;
	chip->cmd_addr.subcmd_bd_offset = BQ27Z561_SUBCMD_BD_OFFSET;
	chip->cmd_addr.subcmd_int_offset = BQ27Z561_SUBCMD_INT_OFFSET;
	chip->cmd_addr.subcmd_cc_ver = BQ27Z561_SUBCMD_CC_VER;
	chip->cmd_addr.subcmd_ocv = BQ27Z561_SUBCMD_OCV;
	chip->cmd_addr.subcmd_bat_ins = BQ27Z561_SUBCMD_BAT_INS;
	chip->cmd_addr.subcmd_bat_rem = BQ27Z561_SUBCMD_BAT_REM;
	chip->cmd_addr.subcmd_set_slp = BQ27Z561_SUBCMD_SET_SLP;
	chip->cmd_addr.subcmd_clr_slp = BQ27Z561_SUBCMD_CLR_SLP;
	chip->cmd_addr.subcmd_fct_res = BQ27Z561_SUBCMD_FCT_RES;
	chip->cmd_addr.subcmd_cal_mode = BQ27Z561_SUBCMD_CAL_MODE;
}

static void oplus_set_device_type_by_extern_cmd(struct chip_bq27z561 *chip)
{
	int ret;
	int device_type = 0;
	int data_check;
	int try_count = BQ27Z561_SUBCMD_TRY_COUNT;
	u8 extend_data[4] = {0};
	struct gauge_track_info_reg extend = { BQ27Z561_SUBCMD_DEVICE_TYPE, 2 };

	chip->device_type = DEVICE_BQ27Z561;
	chip->device_type_for_vooc = DEVICE_TYPE_FOR_VOOC_BQ27Z561;
try:
	ret = bq27z561_i2c_txsubcmd(chip, BQ27Z561_DATAFLASHBLOCK, extend.addr);
	if (ret < 0)
		return;

	msleep(5);
	ret = bq27z561_read_i2c_block(chip, BQ27Z561_DATAFLASHBLOCK, (extend.len + 2), extend_data);
	if (ret < 0)
		return;

	data_check = (extend_data[1] << 0x8) | extend_data[0];
	if (try_count-- > 0 && data_check != extend.addr) {
		chg_info("not match. extend_data[0]=0x%2x, extend_data[1]=0x%2x\n",
			extend_data[0], extend_data[1]);
		goto try;
	}
	if (try_count) {
		device_type = (extend_data[3] << 0x8) | extend_data[2];
		if (device_type == DEVICE_TYPE_BQ27Z561)
			chip->batt_bq27z561 = true;
	}
	chg_info("device_type : 0x%04x\n", device_type);
}

static void oplus_set_device_name(struct chip_bq27z561 *chip, int device_type)
{
	strncpy(chip->device_name, "bq27z561", DEVICE_NAME_LEN);
}

static void bq27z561_hw_config(struct chip_bq27z561 *chip)
{
	if (chip->support_extern_cmd)
		oplus_set_device_type_by_extern_cmd(chip);

	chip->device_type = DEVICE_BQ27Z561;
	oplus_set_device_name(chip, DEVICE_TYPE_BQ27Z561);

	bq27z561_cntl_cmd(chip, BQ27Z561_SUBCMD_CTNL_STATUS);
	udelay(66);
	bq27z561_cntl_cmd(chip, BQ27Z561_SUBCMD_ENABLE_IT);

	gauge_set_cmd_addr(chip);

	dev_err(chip->dev, "DEVICE_TYPE is 0x%02X\n", chip->device_type);
}

static void bq27z561_parse_dt(struct chip_bq27z561 *chip)
{
	struct device_node *node = chip->dev->of_node;
	int rc = 0;

	chip->calib_info_save_support = of_property_read_bool(node, "oplus,calib_info_save_support");
	rc = of_property_read_u32(node, "oplus,batt_num", &chip->batt_num);
	if (rc < 0) {
		chg_err("can't get oplus,batt_num, rc=%d\n", rc);
		chip->batt_num = 1;
	}
	rc = of_property_read_u32(node, "qcom,gauge_num", &chip->gauge_num);
	if (rc)
		chip->gauge_num = 0;
	/* workaround for I2C pull SDA can't trigger error issue 230504153935012779 */
	chip->i2c_rst_ext = of_property_read_bool(node, "oplus,i2c_rst_ext");
	/* end workaround 230504153935012779 */

	chip->support_extern_cmd = of_property_read_bool(node, "oplus,support_extern_cmd");
	if (chip->support_extern_cmd)
		chip->support_sha256_hmac = true;

	chip->support_eco_design = !!(oplus_chg_get_nvid_support_flags() & BIT(ECO_DESIGN_SUPPORT_REGION));
	chg_info("support_eco_design = %d\n", chip->support_eco_design);
}

#ifdef CONFIG_OPLUS_CHARGER_MTK
#define AUTH_MESSAGE_LEN 20
#define AUTH_TAG "ogauge_auth="
#define AUTH_SHA256_TAG "ogauge_sha256_auth="
#define AUTH_PROP "ogauge_auth"
#define AUTH_SHA256_PROP "ogauge_sha256_auth"
#ifdef MODULE
#include <asm/setup.h>
static char __oplus_chg_cmdline[COMMAND_LINE_SIZE];
static char *oplus_chg_cmdline = __oplus_chg_cmdline;

const char *oplus_chg_get_cmdline(const char *target_str)
{
	struct device_node * of_chosen = NULL;
	char *ogauge_auth = NULL;

	if (!target_str)
		return oplus_chg_cmdline;

	if (__oplus_chg_cmdline[0] != 0)
		return oplus_chg_cmdline;

	of_chosen = of_find_node_by_path("/chosen");
	if (of_chosen) {
		ogauge_auth = (char *)of_get_property(
					of_chosen, target_str, NULL);
		if (!ogauge_auth) {
			chg_err("%s: failed to get %s\n", __func__, target_str);
		} else {
			strcpy(__oplus_chg_cmdline, ogauge_auth);
			chg_err("%s: %s: %s\n", __func__, target_str, ogauge_auth);
		}
	} else {
		chg_err("%s: failed to get /chosen \n", __func__);
	}

	return oplus_chg_cmdline;
}
#else
const char *oplus_chg_get_cmdline(const char *target_str)
{
	if (!target_str)
		chg_err("%s: args set error\n", __func__);

	return saved_command_line;
}
#endif

static int get_auth_sha256_msg(u8 *source, u8 *rst)
{
	char *str = NULL;
	int i;

	str = strstr(oplus_chg_get_cmdline(AUTH_SHA256_PROP), AUTH_SHA256_TAG);
	if (str == NULL) {
		chg_err("Asynchronous authentication is not supported!!!\n");
		return -ENODEV;
	}
	chg_info("%s\n", str);
	str += strlen(AUTH_SHA256_TAG);
	for (i = 0; i < GAUGE_SHA256_AUTH_MSG_LEN; i++) {
		source[i] = (str[2 * i] - 64) | ((str[2 * i + 1] - 64) << 4);
		chg_info("source index %d = %x\n", i, source[i]);
	}
	str += GAUGE_SHA256_AUTH_MSG_LEN * 2;
	for (i = 0; i < GAUGE_SHA256_AUTH_MSG_LEN; i++) {
		rst[i] = (str[2 * i] - 64) | ((str[2 * i + 1] - 64) << 4);
		chg_info("expected index %d = %x\n", i, rst[i]);
	}

	return 0;
}
#endif

static bool get_smem_sha256_batt_info(struct chip_bq27z561 *chip,
					struct oplus_gauge_sha256_auth_result *sha256_auth)
{
#ifdef CONFIG_OPLUS_CHARGER_MTK
	int ret = 0;

	if (!chip || !sha256_auth) {
		chg_err("invalid parameters\n");
		return false;
	}

	ret = get_auth_sha256_msg(sha256_auth->msg, sha256_auth->rcv_msg);
	if (!ret)
		return true;

	return false;
#else
	size_t smem_size;
	void *smem_addr;
	oplus_gauge_auth_info_type *smem_data;

	if (!chip || !sha256_auth) {
		chg_err("invalid parameters\n");
		return false;
	}

	memset(sha256_auth, 0, sizeof(struct oplus_gauge_sha256_auth_result));
	smem_addr = qcom_smem_get(QCOM_SMEM_HOST_ANY, SMEM_RESERVED_BOOT_INFO_FOR_APPS, &smem_size);
	if (IS_ERR(smem_addr)) {
		chg_err("unable to acquire smem SMEM_RESERVED_BOOT_INFO_FOR_APPS entry\n");
		return false;
	} else {
		smem_data = (oplus_gauge_auth_info_type *)smem_addr;
		if (smem_data == ERR_PTR(-EPROBE_DEFER)) {
			chg_err("fail to get smem_data\n");
			return false;
		} else {
			memcpy(sha256_auth, &smem_data->sha256_rst_k0,
					sizeof(struct oplus_gauge_sha256_auth_result));
			chg_info("ap_random=[%*ph]\n", GAUGE_SHA256_AUTH_MSG_LEN, sha256_auth->msg);
			chg_info("ap_encode=[%*ph]\n", GAUGE_SHA256_AUTH_MSG_LEN, sha256_auth->rcv_msg);
		}
	}

	return true;
#endif
}

static int bq27z561_get_info(struct chip_bq27z561 *chip, u8 *info, int len)
{
	int i;
	int j;
	int ret;
	int data = 0;
	int index = 0;
	int data_check;
	int try_count = BQ27Z561_SUBCMD_TRY_COUNT;
	u8 extend_data[34] = {0};
	struct gauge_track_info_reg standard[] = {
		{ chip->cmd_addr.reg_temp, 2 },
		{ chip->cmd_addr.reg_volt , 2 },
		{ chip->cmd_addr.reg_flags  , 2 },
		{ chip->cmd_addr.reg_nac , 2 },
		{ chip->cmd_addr.reg_rm , 2 },
		{ chip->cmd_addr.reg_fcc , 2 },
		{ chip->cmd_addr.reg_cc , 2 },
		{ chip->cmd_addr.reg_soc , 2 },
		{ chip->cmd_addr.reg_soh , 2 },
	};

	struct gauge_track_info_reg extend[] = {
		{ BQ27Z561_SUBCMD_CHEMID , 2 },
		{ BQ27Z561_SUBCMD_GAUGE_STATUS  , 3 },
		{ BQ27Z561_SUBCMD_IT_STATUS1 , 16 },
		{ BQ27Z561_SUBCMD_IT_STATUS2 , 20 },
		{ BQ27Z561_SUBCMD_IT_STATUS3 , 12 },
		{ BQ27Z561_SUBCMD_FILTERED_CAP , 6 },
	};


	for (i = 0; i < ARRAY_SIZE(standard); i++) {
		ret = bq27z561_read_i2c(chip, standard[i].addr, &data);
		if (ret < 0)
			continue;
		index += scnprintf(info + index, len - index,
			  "0x%02x=%02x,%02x|", standard[i].addr, (data & 0xff), ((data >> 8) & 0xff));
	}

	for (i = 0; i < ARRAY_SIZE(extend); i++) {
		try_count = BQ27Z561_SUBCMD_TRY_COUNT;
try:
		mutex_lock(&chip->bq27z561_alt_manufacturer_access);
		ret = bq27z561_i2c_txsubcmd(chip, BQ27Z561_DATAFLASHBLOCK, extend[i].addr);
		if (ret < 0) {
			mutex_unlock(&chip->bq27z561_alt_manufacturer_access);
			continue;
		}

		if (sizeof(extend_data) >= extend[i].len + 2) {
			usleep_range(1000, 1000);
			ret = bq27z561_read_i2c_block(chip, BQ27Z561_DATAFLASHBLOCK, (extend[i].len + 2), extend_data);
			mutex_unlock(&chip->bq27z561_alt_manufacturer_access);
			if (ret < 0)
				continue;
			data_check = (extend_data[1] << 0x8) | extend_data[0];
			if (try_count-- > 0 && data_check != extend[i].addr) {
				chg_info("0x%4x not match. try_count=%d, extend_data[0]=0x%2x, extend_data[1]=0x%2x\n",
					extend[i].addr, try_count, extend_data[0], extend_data[1]);
				usleep_range(2000, 2000);
				goto try;
			}
			if (!try_count)
				continue;
			index += scnprintf(info + index, len - index, "0x%04x=", extend[i].addr);
			for (j = 0; j < extend[i].len - 1; j++)
				index += scnprintf(info + index, len - index, "%02x,", extend_data[j + 2]);
			index += scnprintf(info + index, len - index, "%02x", extend_data[j + 2]);
			if (i < ARRAY_SIZE(extend) - 1)
				usleep_range(2000, 2000);
		} else {
			mutex_unlock(&chip->bq27z561_alt_manufacturer_access);
		}
		if (i  <  ARRAY_SIZE(extend) - 1)
			index += scnprintf(info + index, len - index, "|");
	}

	return index;
}

static int oplus_bq27z561_get_reg_info(struct oplus_chg_ic_dev *ic_dev, u8 *info, int len)
{
	struct chip_bq27z561 *chip;

	if (ic_dev == NULL || !info) {
		chg_err("oplus_chg_ic_dev or info is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	if (!chip || atomic_read(&chip->suspended) || atomic_read(&chip->locked))
		return -ENODEV;

	return bq27z561_get_info(chip, info, len);
}

static void oplus_bq27z561_calib_args_to_check_args(
			struct chip_bq27z561 *chip, char *calib_args, int len)
{
	int i;
	int j;

	if (len != (CALIB_TIME_CHECK_ARGS * 2)) {
		chg_err("len not match\n");
		return;
	}

	for (i = 0, j = 0; i < CALIB_TIME_CHECK_ARGS; i++, j += 2) {
		chip->calib_check_args_pre[i] = (calib_args[j + 1] << 0x8) + calib_args[j];
		chg_debug("calib_check_args_pre[%d]=0x%04x\n", i, chip->calib_check_args_pre[i]);
	}
}

static void oplus_bq27z561_check_args_to_calib_args(
				struct chip_bq27z561 *chip, char *calib_args, int len)
{
	int i;
	int j;

	if (len != (CALIB_TIME_CHECK_ARGS * 2)) {
		chg_err("len not match\n");
		return;
	}

	for (i = 0, j = 0; i < CALIB_TIME_CHECK_ARGS; i++, j += 2) {
		calib_args[j] = chip->calib_check_args_pre[i] & 0xff;
		calib_args[j + 1] = (chip->calib_check_args_pre[i] >> 0x8) & 0xff;
		chg_debug("calib_args[%d]=0x%02x, 0x%02x\n", i, calib_args[j], calib_args[j + 1]);
	}
}

static int bq27z561_get_calib_time(struct chip_bq27z561 *chip, int *dod_time, int *qmax_time)
{
	int i;
	int ret;
	int data_check;
	int try_count;
	u8 extend_data[14] = {0};
	int check_args[CALIB_TIME_CHECK_ARGS] = {0};
	static bool init_flag = false;
	struct gauge_track_info_reg extend[] = {
		{ BQ27Z561_SUBCMD_IT_STATUS2 , 12 },
		{ BQ27Z561_SUBCMD_IT_STATUS3 , 4 },
	};

	if (!chip || !dod_time || !qmax_time)
		return -ENODEV;

	for (i = 0; i < ARRAY_SIZE(extend); i++) {
		try_count = BQ27Z561_SUBCMD_TRY_COUNT;
try:
		mutex_lock(&chip->bq27z561_alt_manufacturer_access);
		ret = bq27z561_i2c_txsubcmd(chip, BQ27Z561_DATAFLASHBLOCK, extend[i].addr);
		if (ret < 0) {
			mutex_unlock(&chip->bq27z561_alt_manufacturer_access);
			return -EINVAL;
		}

		if (sizeof(extend_data) >= extend[i].len + 2) {
			usleep_range(1000, 1000);
			ret = bq27z561_read_i2c_block(chip, BQ27Z561_DATAFLASHBLOCK, (extend[i].len + 2), extend_data);
			mutex_unlock(&chip->bq27z561_alt_manufacturer_access);
			if (ret < 0)
				return -EINVAL;
			data_check = (extend_data[1] << 0x8) | extend_data[0];
			if (try_count-- > 0 && data_check != extend[i].addr) {
				chg_info("not match. add=0x%4x, count=%d, extend_data[0]=0x%2x, extend_data[1]=0x%2x\n",
					extend[i].addr, try_count, extend_data[0], extend_data[1]);
				usleep_range(2000, 2000);
				goto try;
			}
			if (!try_count)
				return -EINVAL;
			if (extend[i].addr == BQ27Z561_SUBCMD_IT_STATUS2) {
				check_args[0] = (extend_data[13] << 0x08) | extend_data[12];
			} else {
				check_args[1] = (extend_data[3] << 0x08) | extend_data[2];
				check_args[2] = (extend_data[5] << 0x08) | extend_data[4];
			}
			if (i < ARRAY_SIZE(extend) - 1)
				usleep_range(2000, 2000);
		} else {
			mutex_unlock(&chip->bq27z561_alt_manufacturer_access);
		}
	}

	if (!init_flag || check_args[0] != chip->calib_check_args_pre[0])
		chip->dod_time = 0;
	else
		chip->dod_time++;

	if (!init_flag || check_args[1] != chip->calib_check_args_pre[1] ||
		check_args[2] != chip->calib_check_args_pre[2])
		chip->qmax_time = 0;
	else
		chip->qmax_time++;

	init_flag = true;
	memcpy(chip->calib_check_args_pre, check_args, sizeof(check_args));
	*dod_time = chip->dod_time;
	*qmax_time = chip->qmax_time;

	return ret;
}

static int oplus_bq27z561_set_calib_time(struct oplus_chg_ic_dev *ic_dev,
			int dod_calib_time, int qmax_calib_time, char *calib_args, int len)
{
	struct chip_bq27z561 *chip;

	if (ic_dev == NULL)
		return -ENODEV;

	if (calib_args == NULL || !len)
		return -EINVAL;

	chip = oplus_chg_ic_get_drvdata(ic_dev);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (len != (CALIB_TIME_CHECK_ARGS * 2)) {
		chg_err("len not match\n");
		return -EINVAL;
	}

	if (dod_calib_time) {
		chip->dod_time_pre = dod_calib_time;
		chip->qmax_time_pre = qmax_calib_time;
	} else {
		chip->dod_time_pre = 1;
		chip->qmax_time_pre = 1;
	}
	chip->dod_time = dod_calib_time;
	chip->qmax_time = qmax_calib_time;
	oplus_bq27z561_calib_args_to_check_args(chip, calib_args, len);
	chip->calib_info_init = true;

	return 0;
}

static int oplus_bq27z561_get_batt_sn(struct chip_bq27z561 *chip)
{
	int i;
	int ret;
	int retry = 0;
	u8 check_sum = 0xFF;
	u8 buf[BQ27Z561_BATT_SN_READ_BUF_LEN] = {0x00};
	u8 batt_sn[OPLUS_BATT_SERIAL_NUM_SIZE] = {0x00};

	if (!chip) {
		chg_err("chip fg_ic is not ready.");
		return -ENODEV;
	}
	if (atomic_read(&chip->suspended) == 1)
		return 0;

RETRY:
	while (retry < BQ27Z561_BATT_SN_RETRY_MAX) {
		retry++;
		mutex_lock(&chip->bq27z561_alt_manufacturer_access);
		ret = bq27z561_i2c_txsubcmd(chip, BQ27Z561_BATT_SN_EN_ADDR, BQ27Z561_BATT_SN_CMD);
		if (ret < 0) {
			mutex_unlock(&chip->bq27z561_alt_manufacturer_access);
			continue;
		}
		usleep_range(1000, 1000);
		ret = bq27z561_read_i2c_block(chip, BQ27Z561_BATT_SN_EN_ADDR,
						BQ27Z561_BATT_SN_READ_BUF_LEN, buf);
		mutex_unlock(&chip->bq27z561_alt_manufacturer_access);
		if (ret < 0)
			continue;

		if ((buf[0] == (BQ27Z561_BATT_SN_CMD & 0xFF))
			&& (buf[1] == (BQ27Z561_BATT_SN_CMD >> 8))) {
			memcpy(batt_sn, &buf[2], OPLUS_BATT_SERIAL_NUM_SIZE);
			break;
		}
		chg_info("read sn fail, retry(%d)", retry);
	}

	if (retry < BQ27Z561_BATT_SN_RETRY_MAX) {
		if (batt_sn[OPLUS_BATT_SERIAL_NUM_SIZE - 1] != BQ27Z561_BATTINFO_NO_CHECKSUM) {
			check_sum = 0xFF;
			for (i = 0; i < OPLUS_BATT_SERIAL_NUM_SIZE - 1; i++)
				check_sum -= batt_sn[i];

			if (check_sum != batt_sn[OPLUS_BATT_SERIAL_NUM_SIZE - 1]) {
				chg_err("check_sum fail calc[%d] read[%d], retry.",
					check_sum, batt_sn[OPLUS_BATT_SERIAL_NUM_SIZE - 1]);
				goto RETRY;
			}
		}
		/* replace checksum byte by end of string '\0' */
		batt_sn[OPLUS_BATT_SERIAL_NUM_SIZE - 1] = '\0';
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0))
		strscpy(chip->battinfo.batt_serial_num, (char*)batt_sn, OPLUS_BATT_SERIAL_NUM_SIZE);
#else
		strlcpy(chip->battinfo.batt_serial_num, (char*)batt_sn, OPLUS_BATT_SERIAL_NUM_SIZE);
#endif
		chg_info("chip->battinfo.batt_serial_num %s", chip->battinfo.batt_serial_num);
	} else {
		chg_err("get sn failed");
	}

	return 0;
}

static int oplus_bq27z561_set_first_usage_date(struct chip_bq27z561 *chip, u32 data)
{
	int ret = 0;
	u16 first_usage_date = (data >> 8) & 0xFFFF;
	u8 check_sum = data & 0xFF;
	u8 calc_check_sum = 0x00;
	u8 write_data[BQ28Z610_BATT_FIRST_USAGE_DATE_WLEN] = {BQ28Z610_BATT_FIRST_USAGE_DATE_WADDR & 0xFF,
			   BQ28Z610_BATT_FIRST_USAGE_DATE_WADDR >> 8, 0x00, 0x00, 0x00};
	u8 check_data[2] = {BQ28Z610_BATTINFO_DEFAULT_CHECKSUM, BQ28Z610_BATT_FIRST_USAGE_DATE_WLEN + 2};
	int i = 0;

	write_data[2] = first_usage_date & 0xFF;
	write_data[3] = first_usage_date >> 8;
	write_data[4] = check_sum;
	calc_check_sum = (BQ28Z610_BATTINFO_DEFAULT_CHECKSUM - write_data[2] - write_data[3]) & 0xFF;
	if (check_sum == calc_check_sum) {
		chg_info("oplus_bq27z561_set_first_usage_date: write to gauge(0x%x), seal_flag = %d",
				data, chip->bq27z561_seal_flag);
		mutex_lock(&chip->bq27z561_alt_manufacturer_access);
		if (!bq27z561_deep_init(chip)) {
			mutex_unlock(&chip->bq27z561_alt_manufacturer_access);
			chg_err("bq27z561 deep init failed");
			return -EFAULT;
		}
		ret = bq27z561_write_i2c_block(chip, BQ28Z610_REG_CNTL1, BQ28Z610_BATT_FIRST_USAGE_DATE_WLEN, write_data);
		usleep_range(1000, 1000);

		for (i = 0; i < BQ28Z610_BATT_FIRST_USAGE_DATE_WLEN; i++)
			check_data[0] -= write_data[i];

		ret = bq27z561_write_i2c_block(chip, BQ28Z610_BATT_WRITE_CHECK_SUM_ADDR, 2, check_data);
		usleep_range(1000, 1000);

		if (!chip->bq27z561_seal_flag)
			bq27z561_deep_deinit(chip);

		mutex_unlock(&chip->bq27z561_alt_manufacturer_access);
	} else {
		chg_err("oplus_bq27z561_set_first_usage_date: check_sum err");
		mutex_lock(&chip->bq27z561_alt_manufacturer_access);
		bq27z561_deep_deinit(chip);
		mutex_unlock(&chip->bq27z561_alt_manufacturer_access);
		ret = -EFAULT;
	}

	return ret;
}

static int bq27z561_set_batt_ui_cycle_count(struct chip_bq27z561 *chip, u32 data)
{
	int ret = 0;
	u16 cycle_count = (data >> 8) & 0xFFFF;
	u8 check_sum = data & 0xFF;
	u8 calc_check_sum = 0x00;
	u8 write_data[BQ28Z610_BATT_UI_CC_WLEN] = {BQ28Z610_BATT_UI_CC_WADDR & 0xFF,
			   BQ28Z610_BATT_UI_CC_WADDR >> 8, 0x00, 0x00, 0x00};
	u8 check_data[2] = {BQ28Z610_BATTINFO_DEFAULT_CHECKSUM, BQ28Z610_BATT_UI_CC_WLEN + 2};
	int i = 0;

	write_data[2] = cycle_count & 0xFF;
	write_data[3] = cycle_count >> 8;
	write_data[4] = check_sum;
	calc_check_sum = (BQ28Z610_BATTINFO_DEFAULT_CHECKSUM - write_data[2] - write_data[3]) & 0xFF;
	if (check_sum == calc_check_sum) {
		chg_info("bq27z561_set_batt_ui_cycle_count: write to gauge(0x%x), seal_flag = %d",
				data, chip->bq27z561_seal_flag);
		mutex_lock(&chip->bq27z561_alt_manufacturer_access);
		if (!bq27z561_deep_init(chip)) {
			mutex_unlock(&chip->bq27z561_alt_manufacturer_access);
			chg_err("bq27z561 deep init failed");
			return -EFAULT;
		}
		ret = bq27z561_write_i2c_block(chip, BQ28Z610_REG_CNTL1, BQ28Z610_BATT_UI_CC_WLEN, write_data);
		usleep_range(1000, 1000);

		for (i = 0; i < BQ28Z610_BATT_UI_CC_WLEN; i++)
			check_data[0] -= write_data[i];


		ret = bq27z561_write_i2c_block(chip, BQ28Z610_BATT_WRITE_CHECK_SUM_ADDR, 2, check_data);
		usleep_range(1000, 1000);

		if (!chip->bq27z561_seal_flag)
			bq27z561_deep_deinit(chip);

		mutex_unlock(&chip->bq27z561_alt_manufacturer_access);
	} else {
		chg_err("bq27z561_set_batt_ui_cycle_count: check_sum err");
		mutex_lock(&chip->bq27z561_alt_manufacturer_access);
		bq27z561_deep_deinit(chip);
		mutex_unlock(&chip->bq27z561_alt_manufacturer_access);
		ret = -EFAULT;
	}

	return ret;
}

static int bq27z561_set_batt_ui_soh(struct chip_bq27z561 *chip, u32 data)
{
	int ret = 0;
	u8 soh = (data >> 8) & 0xFF;
	u8 check_sum = data & 0xFF;
	u8 calc_check_sum = 0x00;
	u8 write_data[BQ28Z610_BATT_UI_SOH_WLEN] = {BQ28Z610_BATT_UI_SOH_WADDR & 0xFF,
			   BQ28Z610_BATT_UI_SOH_WADDR >> 8, 0x00, 0x00};
	u8 check_data[2] = {BQ28Z610_BATTINFO_DEFAULT_CHECKSUM, BQ28Z610_BATT_UI_SOH_WLEN + 2};
	int i = 0;

	write_data[2] = soh;
	write_data[3] = check_sum;
	calc_check_sum = (BQ28Z610_BATTINFO_DEFAULT_CHECKSUM - write_data[2]) & 0xFF;
	if (check_sum == calc_check_sum) {
		chg_info("bq27z561_set_batt_ui_soh: write to gauge(0x%x), seal_flag = %d",
				data, chip->bq27z561_seal_flag);
		mutex_lock(&chip->bq27z561_alt_manufacturer_access);
		if (!bq27z561_deep_init(chip)) {
			mutex_unlock(&chip->bq27z561_alt_manufacturer_access);
			chg_err("bq27z561 deep init failed");
			return -EFAULT;
		}
		ret = bq27z561_write_i2c_block(chip, BQ28Z610_REG_CNTL1, BQ28Z610_BATT_UI_SOH_WLEN, write_data);
		usleep_range(1000, 1000);

		for (i = 0; i < BQ28Z610_BATT_UI_SOH_WLEN; i++)
			check_data[0] -= write_data[i];

		ret = bq27z561_write_i2c_block(chip, BQ28Z610_BATT_WRITE_CHECK_SUM_ADDR, 2, check_data);
		usleep_range(1000, 1000);

		if (!chip->bq27z561_seal_flag)
			bq27z561_deep_deinit(chip);

		mutex_unlock(&chip->bq27z561_alt_manufacturer_access);
	} else {
		chg_err("bq27z561_set_batt_ui_soh: check_sum err");
		mutex_lock(&chip->bq27z561_alt_manufacturer_access);
		bq27z561_deep_deinit(chip);
		mutex_unlock(&chip->bq27z561_alt_manufacturer_access);
		ret = -EFAULT;
	}

	return ret;
}

static int bq27z561_set_batt_used_flag(struct chip_bq27z561 *chip, u32 data)
{
	int ret = 0;
	u8 flag = (data >> 8) & 0xFF;
	u8 check_sum = data & 0xFF;
	u8 calc_check_sum = 0x00;
	u8 write_data[BQ28Z610_BATT_USED_FLAG_WLEN] = {BQ28Z610_BATT_USED_FLAG_WADDR & 0xFF,
				BQ28Z610_BATT_USED_FLAG_WADDR >> 8, 0x00, 0x00};
	u8 check_data[2] = {BQ28Z610_BATTINFO_DEFAULT_CHECKSUM, BQ28Z610_BATT_USED_FLAG_WLEN + 2};
	int i = 0;

	write_data[2] = flag;
	write_data[3] = check_sum;
	calc_check_sum = (BQ28Z610_BATTINFO_DEFAULT_CHECKSUM - write_data[2]) & 0xFF;
	if (check_sum == calc_check_sum) {
		chg_info("bq27z561_set_batt_used_flag: write to gauge(0x%x), seal_flag = %d",
			data, chip->bq27z561_seal_flag);
		mutex_lock(&chip->bq27z561_alt_manufacturer_access);
		if (!bq27z561_deep_init(chip)) {
			mutex_unlock(&chip->bq27z561_alt_manufacturer_access);
			chg_err("bq27z561 deep init failed");
			return -EFAULT;
		}
		ret = bq27z561_write_i2c_block(chip, BQ28Z610_REG_CNTL1, BQ28Z610_BATT_USED_FLAG_WLEN, write_data);
		usleep_range(1000, 1000);

		for (i = 0; i < BQ28Z610_BATT_USED_FLAG_WLEN; i++)
			check_data[0] -= write_data[i];

		ret = bq27z561_write_i2c_block(chip, BQ28Z610_BATT_WRITE_CHECK_SUM_ADDR, 2, check_data);

		if (!chip->bq27z561_seal_flag)
			bq27z561_deep_deinit(chip);

		mutex_unlock(&chip->bq27z561_alt_manufacturer_access);
	} else {
		chg_err("bq27z561_set_batt_used_flag: check_sum err");
		mutex_lock(&chip->bq27z561_alt_manufacturer_access);
		bq27z561_deep_deinit(chip);
		mutex_unlock(&chip->bq27z561_alt_manufacturer_access);
		ret = -EFAULT;
	}

	return ret;
}

static void bq27z561_get_batt_manu_date(struct chip_bq27z561 *chip)
{
	int rc = 0;
	u8 buf[BQ28Z610_BATT_MANU_DATE_READ_BUF_LEN] = {0x00};
	u16 date_temp = 0x00;

	mutex_lock(&chip->bq27z561_alt_manufacturer_access);
	rc = bq27z561_i2c_txsubcmd(chip, BQ28Z610_BATTINFO_EN_ADDR, BQ28Z610_BATTINFO_MANUDATE_CMD);
	if (rc < 0) {
		chg_err("txsubcmd BQ28Z610_BATTINFO_MANUDATE_CMD fail %d", rc);
		mutex_unlock(&chip->bq27z561_alt_manufacturer_access);
		return;
	}
	usleep_range(1000, 1000);
	rc = bq27z561_read_i2c_block(chip, BQ28Z610_BATTINFO_EN_ADDR, BQ28Z610_BATT_MANU_DATE_READ_BUF_LEN, buf);
	mutex_unlock(&chip->bq27z561_alt_manufacturer_access);
	if (rc < 0) {
		chg_err("BQ28Z610_BATT_MANU_DATE_READ_BUF_LEN fail %d", rc);
		return;
	}
	if ((buf[0] == (BQ28Z610_BATTINFO_MANUDATE_CMD & 0xFF))
		&& (buf[1] == (BQ28Z610_BATTINFO_MANUDATE_CMD >> 8))) {
		date_temp = buf[2] | (buf[3] << 8);
	}

	chip->battinfo.manu_date = date_temp;
	chg_info("manu_date %u", chip->battinfo.manu_date);
}

static int bq27z561_get_batt_vdm_data(struct chip_bq27z561 *chip)
{
	int ret = 0;
	u8 buf[BQ28Z610_BATT_VDM_DATA_READ_BUF_LEN] = {0x00};
	u8 ui_soh = 0x00;
	u16 ui_cycle_count = 0x00;
	u8 used_flag = 0x00;
	u16 date_temp = 0x00;
	u8 check_sum = BQ28Z610_BATTINFO_DEFAULT_CHECKSUM;
	int retry = 0;

	if (!chip) {
		chg_err("chip fg_ic is not ready.");
		return -ENODEV;
	}
	if (atomic_read(&chip->suspended) == 1)
		return 0;
RETRY:
	while (retry < BQ28Z610_BATT_INFO_RETRY_MAX) {
		retry++;
		if (chip->batt_bq27z561) {
			mutex_lock(&chip->bq27z561_alt_manufacturer_access);
			ret = bq27z561_i2c_txsubcmd(chip, BQ28Z610_BATTINFO_EN_ADDR, BQ28Z610_BATTINFO_VDMDATA_CMD);
			if (ret < 0) {
				mutex_unlock(&chip->bq27z561_alt_manufacturer_access);
				continue;
			}
			usleep_range(1000, 1000);
			ret = bq27z561_read_i2c_block(chip, BQ28Z610_BATTINFO_EN_ADDR,
							BQ28Z610_BATT_VDM_DATA_READ_BUF_LEN, buf);
			mutex_unlock(&chip->bq27z561_alt_manufacturer_access);
			if (ret < 0) {
				chg_err("bq27z561_get_batt_vdm_data: read failed, ret=%d.", ret);
				continue;
			}

			if ((buf[0] == (BQ28Z610_BATTINFO_VDMDATA_CMD & 0xFF))
				&& (buf[1] == (BQ28Z610_BATTINFO_VDMDATA_CMD >> 8))) {
				date_temp = buf[BQ28Z610_BATT_FIRST_USAGE_DATE_L] |
					buf[BQ28Z610_BATT_FIRST_USAGE_DATE_H] << 8;
				ui_soh = buf[BQ28Z610_BATT_UI_SOH];
				ui_cycle_count = buf[BQ28Z610_BATT_UI_CYCLE_COUNT_L] |
					buf[BQ28Z610_BATT_UI_CYCLE_COUNT_H] << 8;
				used_flag = buf[BQ28Z610_BATT_USED_FLAG];
				break;
			}
		} else {
			/* TODO other gauge. */
			retry = BQ28Z610_BATT_INFO_RETRY_MAX;
			break;
		}
		chg_err("bq27z561_get_batt_vdm_data retry(%d) read date fail", retry);
	}

	if (retry < BQ28Z610_BATT_INFO_RETRY_MAX) {
		if (buf[BQ28Z610_BATT_FIRST_USAGE_DATE_CHECK] != BQ28Z610_BATTINFO_NO_CHECKSUM) {
			check_sum = (BQ28Z610_BATTINFO_DEFAULT_CHECKSUM - buf[BQ28Z610_BATT_FIRST_USAGE_DATE_L]
				- buf[BQ28Z610_BATT_FIRST_USAGE_DATE_H]) & 0xFF;
			if (check_sum != buf[BQ28Z610_BATT_FIRST_USAGE_DATE_CHECK]) {
				chg_err("bq27z561_get_batt_vdm_data fud_check fail calc[0x%02x] read[0x%02x], retry.",
					check_sum, buf[BQ28Z610_BATT_FIRST_USAGE_DATE_CHECK]);
				goto RETRY;
			}
		}
		chip->battinfo.first_usage_date = date_temp;

		if (buf[BQ28Z610_BATT_UI_SOH_CHECK] != BQ28Z610_BATTINFO_NO_CHECKSUM) {
			check_sum = (BQ28Z610_BATTINFO_DEFAULT_CHECKSUM - buf[BQ28Z610_BATT_UI_SOH]) & 0xFF;
			if (check_sum != buf[BQ28Z610_BATT_UI_SOH_CHECK]) {
				chg_err("bq27z561_get_batt_vdm_data uisoh_check fail calc[0x%02x] read[0x%02x], retry.",
						 check_sum, buf[BQ28Z610_BATT_UI_SOH_CHECK]);
				goto RETRY;
			}
		}
		chip->battinfo.ui_soh = ui_soh;

		if (buf[BQ28Z610_BATT_UI_CYCLE_COUNT_CHECK] != BQ28Z610_BATTINFO_NO_CHECKSUM) {
			check_sum = (BQ28Z610_BATTINFO_DEFAULT_CHECKSUM - buf[BQ28Z610_BATT_UI_CYCLE_COUNT_L]
						- buf[BQ28Z610_BATT_UI_CYCLE_COUNT_H]) & 0xFF;
			if (check_sum != buf[BQ28Z610_BATT_UI_CYCLE_COUNT_CHECK]) {
				chg_err("bq27z561_get_batt_vdm_data uicc_check fail calc[0x%02x] read[0x%02x], retry.",
						 check_sum, buf[BQ28Z610_BATT_UI_CYCLE_COUNT_CHECK]);
				goto RETRY;
			}
		}
		chip->battinfo.ui_cycle_count = ui_cycle_count;

		if (buf[BQ28Z610_BATT_USED_FLAG_CHECK] != BQ28Z610_BATTINFO_NO_CHECKSUM) {
			check_sum = (BQ28Z610_BATTINFO_DEFAULT_CHECKSUM - buf[BQ28Z610_BATT_USED_FLAG]) & 0xFF;
			if (check_sum != buf[BQ28Z610_BATT_USED_FLAG_CHECK]) {
				chg_err("bq27z561_get_batt_vdm_data used_flag fail calc[0x%02x] read[0x%02x], retry.",
						check_sum, buf[BQ28Z610_BATT_USED_FLAG_CHECK]);
				goto RETRY;
			}
		}
		chip->battinfo.used_flag = used_flag;
	} else {
		chg_err("bq27z561_get_batt_vdm_data failed");
		ret = -EFAULT;
	}

	return ret;
}

static int oplus_get_battinfo_sn(struct oplus_chg_ic_dev *ic_dev, char buf[], int len)
{
	struct chip_bq27z561 *chip;
	int bsnlen = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	if (!chip || !buf || len < OPLUS_BATT_SERIAL_NUM_SIZE)
		return -EINVAL;

	chg_info("BattSN(%s):%s", ic_dev->name, chip->battinfo.batt_serial_num);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0))
	bsnlen = strscpy(buf, chip->battinfo.batt_serial_num, OPLUS_BATT_SERIAL_NUM_SIZE);
#else
	bsnlen = strlcpy(buf, chip->battinfo.batt_serial_num, OPLUS_BATT_SERIAL_NUM_SIZE);
#endif

	return bsnlen;
}

static int oplus_set_first_usage_date(struct oplus_chg_ic_dev *ic_dev, const char *buf)
{
	struct chip_bq27z561 *chip;
	u32 data = 0x00;
	u16 date = 0x00;
	u8 check_sum = 0x00;
	int year = 0;
	int month = 0;
	int day = 0;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);
	if (!chip || !buf)
		return -EINVAL;

	if (!chip->support_eco_design)
		return -ENOTSUPP;

	sscanf(buf, "%d-%d-%d", &year, &month, &day);
	date = (((year - 1980) & 0x7F) << 9) | ((month & 0xF) << 5) | (day & 0x1F);
	check_sum = 0xFF - ((date >> 8) & 0xFF) - (date & 0xFF);
	data = date << 8 | check_sum;
	chg_info("%d-%d-%d, date=0x%04x, data=0x%08x", year, month, day, date, data);

	rc = oplus_bq27z561_set_first_usage_date(chip, data);
	if (rc) {
		chg_err("set firset usage date fail rc = %d\n", rc);
		return rc;
	}
	chip->battinfo.first_usage_date = date;
	chg_info("set firset usage date succ\n");

	return 0;
}

static int oplus_get_first_usage_date(struct oplus_chg_ic_dev *ic_dev, char *buf, int len)
{
	struct chip_bq27z561 *chip;
	int date_len = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);
	if (!chip || !buf || len < OPLUS_BATTINFO_DATE_SIZE)
		return -EINVAL;

	if (!chip->support_eco_design)
		return -ENOTSUPP;

	date_len = snprintf(buf, len, "%d-%02d-%02d", (((chip->battinfo.first_usage_date >> 9) & 0x7F) + 1980),
			    (chip->battinfo.first_usage_date >> 5) & 0xF, chip->battinfo.first_usage_date & 0x1F);

	return date_len;
}

static int oplus_set_ui_cycle_count(struct oplus_chg_ic_dev *ic_dev, u16 ui_cycle_count)
{
	struct chip_bq27z561 *chip;
	u8 check_sum = 0x00;
	u32 data = 0x00;
	int ret;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	if (!chip)
		return -EINVAL;

	if (!chip->support_eco_design)
		return -ENOTSUPP;

	check_sum = 0xFF - ((ui_cycle_count >> 8) & 0xFF) - (ui_cycle_count & 0xFF);
	data = ui_cycle_count << 8 | check_sum;
	ret = bq27z561_set_batt_ui_cycle_count(chip, data);
	if (ret < 0) {
		chg_err("set ui cycle count %u failed", ui_cycle_count);
		return ret;
	}
	chip->battinfo.ui_cycle_count = ui_cycle_count;
	chg_info("set ui cycle count %u succ", ui_cycle_count);

	return 0;
}

static int oplus_get_ui_cycle_count(struct oplus_chg_ic_dev *ic_dev, u16 *ui_cycle_count)
{
	struct chip_bq27z561 *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	if (!chip || !ui_cycle_count)
		return -EINVAL;

	if (!chip->support_eco_design)
		return -ENOTSUPP;

	chg_info("BattUICyclecount:%d", chip->battinfo.ui_cycle_count);
	*ui_cycle_count = chip->battinfo.ui_cycle_count;

	return 0;
}

static int oplus_set_ui_soh(struct oplus_chg_ic_dev *ic_dev, u8 ui_soh)
{
	struct chip_bq27z561 *chip;
	u8 check_sum = 0x00;
	u32 data = 0x00;
	int ret;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	if (!chip)
		return -EINVAL;

	if (!chip->support_eco_design)
		return -ENOTSUPP;

	check_sum = 0xFF - (ui_soh & 0xFF);
	data = ui_soh << 8 | check_sum;
	ret = bq27z561_set_batt_ui_soh(chip, data);
	if (ret < 0) {
		chg_err("set batt ui soh %u failed", ui_soh);
		return ret;
	}
	chip->battinfo.ui_soh = data;
	chg_info("set batt ui soh %u succ", ui_soh);

	return 0;
}

static int oplus_get_ui_soh(struct oplus_chg_ic_dev *ic_dev, u8 *ui_soh)
{
	struct chip_bq27z561 *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	if (!chip || !ui_soh)
		return -EINVAL;

	if (!chip->support_eco_design)
		return -ENOTSUPP;

	chg_info("BattUISoh:%d", chip->battinfo.ui_soh);
	*ui_soh = chip->battinfo.ui_soh;

	return 0;
}

static int oplus_get_used_flag(struct oplus_chg_ic_dev *ic_dev, u8 *used_flag)
{
	struct chip_bq27z561 *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	if (!chip || !used_flag)
		return -EINVAL;

	if (!chip->support_eco_design)
		return -ENOTSUPP;

	*used_flag = chip->battinfo.used_flag;
	chg_info("get used_flag %u", *used_flag);

	return 0;
}

static int oplus_set_used_flag(struct oplus_chg_ic_dev *ic_dev, u8 used_flag)
{
	struct chip_bq27z561 *chip;
	u8 check_sum = 0x00;
	u32 data = 0x00;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	if (!chip)
		return -EINVAL;

	if (!chip->support_eco_design)
		return -ENOTSUPP;

	check_sum = 0xFF - (used_flag & 0xFF);
	data = used_flag << 8 | check_sum;
	rc = bq27z561_set_batt_used_flag(chip, data);
	if (rc < 0) {
		chg_err("set batt used flag failed");
		return rc;
	}
	chip->battinfo.used_flag = used_flag;
	chg_info("set batt used flag %u succ", used_flag);

	return 0;
}

static int oplus_get_manu_date(struct oplus_chg_ic_dev *ic_dev, char buf[], int len)
{
	struct chip_bq27z561 *chip;
	int date_len = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	if (!chip || !buf || len <  OPLUS_BATTINFO_DATE_SIZE)
		return -EINVAL;

	if (!chip->support_eco_design)
		return -ENOTSUPP;

	chg_info("BattManuDate:0x%04x", chip->battinfo.manu_date);
	date_len = snprintf(buf, len, "%d-%02d-%02d", (((chip->battinfo.manu_date >> 9) & 0x7F) + 1980),
				(chip->battinfo.manu_date >> 5) & 0xF, chip->battinfo.manu_date & 0x1F);

	return date_len;
}

static int oplus_get_dec_fg_type(struct oplus_chg_ic_dev *ic_dev, int *fg_type)
{
	struct chip_bq27z561 *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	if (!chip || !fg_type)
		return -EINVAL;

	*fg_type = DEC_CV_PACK_SINGLE;

	return 0;
}

static int oplus_bq27z561_get_dec_cv_soh(struct oplus_chg_ic_dev *ic_dev, int *dec_soh)
{
	struct chip_bq27z561 *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);
	if (!chip || !dec_soh)
		return -EINVAL;

	*dec_soh = chip->cc_pre;

	return 0;
}

static void oplus_get_manu_battinfo_work(struct work_struct *work)
{
	int ret;
	struct chip_bq27z561 *chip;
	char batt_info[OPLUS_BATT_SERIAL_NUM_SIZE] = {0};

	chip = container_of(work, struct chip_bq27z561, get_manu_battinfo_work.work);
	if (!chip)
		return;

	if (!chip->support_eco_design) {
		chg_info("not support eco design\n");
		return;
	}

	ret = oplus_get_battinfo_sn(chip->ic_dev, batt_info, OPLUS_BATT_SERIAL_NUM_SIZE);
	if (ret < 0)
		chg_err("oplus_get_manu_battinfo_work get sn failed");

	bq27z561_get_batt_manu_date(chip);

	ret = bq27z561_get_batt_vdm_data(chip);
	if (ret < 0)
		chg_err("oplus_get_manu_battinfo_work get vdm-data failed");
}

static int oplus_bq27z561_get_calib_time(struct oplus_chg_ic_dev *ic_dev,
			int *dod_calib_time, int *qmax_calib_time, char *calib_args, int len)
{
	struct chip_bq27z561 *chip;
	int rc = -1;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}

	if (calib_args == NULL || !dod_calib_time || !qmax_calib_time || !len)
		return -EINVAL;

	chip = oplus_chg_ic_get_drvdata(ic_dev);
	if (!chip) {
		chg_err("chip is NULL");
		return -ENODEV;
	}

	if (chip->calib_info_save_support && !chip->calib_info_init) {
		*dod_calib_time = -1;
		*qmax_calib_time = -1;
		return 0;
	}

	if (atomic_read(&chip->suspended) || atomic_read(&chip->locked)) {
		*dod_calib_time = chip->dod_time_pre;
		*qmax_calib_time = chip->qmax_time_pre;
		return 0;
	}
	mutex_lock(&chip->calib_time_mutex);
	bq27z561_get_calib_time(chip, dod_calib_time, qmax_calib_time);
	oplus_bq27z561_check_args_to_calib_args(chip, calib_args, len);
	chip->dod_time_pre = *dod_calib_time;
	chip->qmax_time_pre = *qmax_calib_time;
	mutex_unlock(&chip->calib_time_mutex);

	return rc;
}

static bool bq27z561_sha256_hmac_authenticate(struct chip_bq27z561 *chip)
{
	int i;
	int ret;
	int count = 0;
	u8 checksum = 0;
	u8 data_buf[2] = {0};
	u8 *p_temp = chip->sha256_authenticate_data->random;
	int len = ARRAY_SIZE(chip->sha256_authenticate_data->random);
	int half_len = len / 2;

	mutex_lock(&chip->bq27z561_alt_manufacturer_access);
	for (i = 0; i < len; i++)
		checksum = checksum + chip->sha256_authenticate_data->random[i];
	checksum = 0xff - (checksum & 0xff);

	chg_info("%s start, len=%d, half_len:%d\n", __func__, len, half_len);
	ret = bq27z561_write_i2c_block(chip, BQ27Z561_DATAFLASHBLOCK, sizeof(data_buf), data_buf);
	if (ret < 0)
		goto error;

	ret = bq27z561_write_i2c_block(chip, BQ27Z561_AUTHENDATA_1ST, half_len, p_temp);
	if (ret < 0)
		goto error;

	ret = bq27z561_write_i2c_block(chip, BQ27Z561_AUTHENDATA_2ND, half_len, p_temp + half_len);
	if (ret < 0)
		goto error;

	ret = bq27z561_i2c_txsubcmd_onebyte(chip, BQ27Z561_AUTHENCHECKSUM, checksum);
	if (ret < 0)
		goto error;

	ret = bq27z561_i2c_txsubcmd_onebyte(chip, BQ27Z561_AUTHENLEN, 0x24);
	if (ret < 0)
		goto error;

	p_temp = chip->sha256_authenticate_data->gauge_encode;
try:
	msleep(60);
	ret = bq27z561_read_i2c_block(chip, BQ27Z561_AUTHENDATA_1ST, half_len, p_temp);
	if (ret < 0 && count < BQ27Z561_I2C_TRY_COUNT) {
		count++;
		chg_info("ret=%d, count=%d\n", ret, count);
		msleep(20);
		goto try;
	}
	ret = bq27z561_read_i2c_block(chip, BQ27Z561_AUTHENDATA_2ND, half_len, p_temp + half_len);
	if (ret < 0)
		goto error;
	mutex_unlock(&chip->bq27z561_alt_manufacturer_access);
	return true;

error:
	mutex_unlock(&chip->bq27z561_alt_manufacturer_access);
	return false;
}

static bool bq27z561_sha256_hmac_result_check(struct chip_bq27z561 *chip)
{
	bool ret = false;
	int len = ARRAY_SIZE(chip->sha256_authenticate_data->random);
	u8 *ap_temp = chip->sha256_authenticate_data->ap_encode;
	u8 *gauge_temp = chip->sha256_authenticate_data->gauge_encode;

	chg_info("ap_encode   =[%*ph]\n", len, ap_temp);
	chg_info("gauge_encode=[%*ph]\n", GAUGE_SHA256_AUTH_MSG_LEN, gauge_temp);

	if (memcmp(ap_temp, gauge_temp, len)) {
		chg_err("gauge authenticate compare failed\n");
		ret =  false;
	} else {
		chg_info("gauge authenticate succeed\n");
		ret = true;
	}

	return ret;
}

static bool init_sha256_gauge_auth(struct chip_bq27z561 *chip)
{
	bool ret;
	struct oplus_gauge_sha256_auth_result sha256_auth;

	if (!chip || !chip->sha256_authenticate_data)
		return false;

	ret = get_smem_sha256_batt_info(chip, &sha256_auth);
	if (!ret) {
		chg_err("get smem sha256 batt info failed\n");
		return ret;
	}

	memset(chip->sha256_authenticate_data, 0, sizeof(struct oplus_gauge_sha256_auth));
	memcpy(chip->sha256_authenticate_data->random,
					sha256_auth.msg, GAUGE_SHA256_AUTH_MSG_LEN);
	memcpy(chip->sha256_authenticate_data->ap_encode,
				sha256_auth.rcv_msg, GAUGE_SHA256_AUTH_MSG_LEN);

	ret = bq27z561_sha256_hmac_authenticate(chip);
	if (!ret)
		return ret;

	ret = bq27z561_sha256_hmac_result_check(chip);

	return ret;
}

static void register_gauge_devinfo(struct chip_bq27z561 *chip)
{
	int ret = 0;
	char *version;
	char *manufacture;

	switch (chip->device_type) {
	case DEVICE_BQ27Z561:
		version = "bq27z561";
		manufacture = "TI";
		break;
	default:
		version = "unknown";
		manufacture = "UNKNOWN";
		break;
	}
#ifndef CONFIG_DISABLE_OPLUS_FUNCTION
	if (chip->gauge_num == 0)
		ret = register_device_proc("gauge", version, manufacture);
	else
		ret = register_device_proc("sub_gauge", version, manufacture);
#endif
	if (ret)
		chg_err("register_gauge_devinfo fail\n");
}

static void bq27z561_modify_soc_smooth_parameter(struct chip_bq27z561 *chip)
{
	return;
}

static void bq27z561_reset(struct i2c_client *client)
{
	struct chip_bq27z561 *chip;
	int ui_soc;

	chip = dev_get_drvdata(&client->dev);
	/* TODO int ui_soc = oplus_chg_get_ui_soc(); */
	ui_soc = bq27z561_get_battery_soc(chip);

	if (!chip)
		return;

	if (bq27z561_get_battery_mvolts(chip) <= 3300 &&
	    bq27z561_get_battery_mvolts(chip) > 2500 && ui_soc == 0 &&
	    bq27z561_get_battery_temperature(chip) > 150) {
		chg_info("bq27z561 unseal OK vol = %d, ui_soc = %d, temp = %d!\n",
			bq27z561_get_battery_mvolts(chip), ui_soc, bq27z561_get_battery_temperature(chip));
		bq27z561_cntl_cmd(chip, BQ27Z561_SUBCMD_RESET);
		msleep(150);
		chg_info("bq27z541_reset, point = %d\r\n", bq27z561_get_battery_soc(chip));
	} else if (chip) {
		bq27z561_modify_soc_smooth_parameter(chip);
	}
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))
static int bq27z561_pm_resume(struct device *dev)
{
	struct chip_bq27z561 *chip;

	chip = dev_get_drvdata(dev);
	if (!chip)
		return 0;

	atomic_set(&chip->suspended, 0);
	bq27z561_get_battery_soc(chip);
	oplus_chg_ic_virq_trigger(chip->ic_dev, OPLUS_IC_VIRQ_RESUME);
	return 0;
}

static int bq27z561_pm_suspend(struct device *dev)
{
	struct chip_bq27z561 *chip;

	chip = dev_get_drvdata(dev);
	if (!chip)
		return 0;

	atomic_set(&chip->suspended, 1);
	return 0;
}

static const struct dev_pm_ops bq27z561_pm_ops = {
	.resume = bq27z561_pm_resume,
	.suspend = bq27z561_pm_suspend,
};

#else /*(LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))*/
static int bq27z561_resume(struct i2c_client *client)
{
	struct chip_bq27z561 *chip;

	chip = dev_get_drvdata(&client->dev);
	if (!chip)
		return 0;

	atomic_set(&chip->suspended, 0);
	bq27z561_get_battery_soc(chip);
	oplus_chg_ic_virq_trigger(chip->ic_dev, OPLUS_IC_VIRQ_RESUME);
	return 0;
}

static int bq27z561_suspend(struct i2c_client *client, pm_message_t mesg)
{
	struct chip_bq27z561 *chip;

	chip = dev_get_drvdata(&client->dev);
	if (!chip)
		return 0;

	atomic_set(&chip->suspended, 1);
	return 0;
}
#endif /*(LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))*/

#define REG_DUMP_SIZE 1024
static int dump_reg[] = { 0x08, 0x12, 0x2c };
static int gauge_reg_dump(struct chip_bq27z561 *chip)
{
	int val = 0;
	int i;
	int l = 0;
	char *pos;
	int sum = 0, ret;
	u8 iv[32] = { 0 };
	char buf[REG_DUMP_SIZE] = { 0 };
	int len_max = REG_DUMP_SIZE - 16;
	int len_sus_pwr = 6;
	int len_max_pwr = 16;
	int len_sus_curr_h = 26;

	bool read_done = false;

	if (!chip)
		return 0;

	if (atomic_read(&chip->suspended) == 1) {
		chg_err("%s: gauge suspend!\n", __func__);
		return -EINVAL;
	}

	pos = buf;
	if (atomic_read(&chip->locked) == 0) {
		l = sprintf(pos, "%d ", bq27z561_get_battery_temperature(chip));
		pos += l;
		sum += l;
		l = sprintf(pos, "/ %d ", bq27z561_get_average_current(chip));
		pos += l;
		sum += l;

		for (i = 0; !read_done; i++) {
			ret = bq27z561_read_i2c(chip, dump_reg[i], &val);
			if (ret) {
				chg_err("error reading regdump, ret:%d\n", ret);
				return -EINVAL;
			}
			l = sprintf(pos, "/ %d ", val);
			pos += l;
			sum += l;

			read_done = !(i < sizeof(dump_reg) / sizeof(int));
			read_done &= !(sum < len_max);
		}

		bq27z561_i2c_txsubcmd(chip, BQ27Z561_DATA_CLASS_ACCESS, BQ27Z561_SUBCMD_IT_STATUS1);
		usleep_range(10000, 10000);
		bq27z561_read_i2c_block(chip, BQ27Z561_REG_CNTL1, len_sus_pwr, iv);
		for (i = 2; (i < len_sus_pwr) && (sum < len_max); i++) {
			if ((i % 2) == 0) {
				l = sprintf(pos, "/ %d ",
					    (iv[i + 1] << 8) + iv[i]);
				pos += l;
				sum += l;
			}
		}
		bq27z561_i2c_txsubcmd(chip, BQ27Z561_DATA_CLASS_ACCESS, BQ27Z561_SUBCMD_IT_STATUS2);
		usleep_range(10000, 10000);
		bq27z561_read_i2c_block(chip, BQ27Z561_REG_CNTL1, len_max_pwr, iv);
		for (i = 2; (i < len_max_pwr) && (sum < len_max); i++) {
			if (i != 3 && i != 4 && i != 7 && i != 8 && i != 11 &&
			    i != 12) {
				if ((i % 2) == 0) {
					l = sprintf(pos, "/ %d ",
						    (iv[i + 1] << 8) + iv[i]);
					pos += l;
					sum += l;
				}
			}
		}
		bq27z561_i2c_txsubcmd(chip, BQ27Z561_DATA_CLASS_ACCESS, BQ27Z561_SUBCMD_IT_STATUS3);
		usleep_range(10000, 10000);
		bq27z561_read_i2c_block(chip, BQ27Z561_REG_CNTL1, len_sus_curr_h, iv);
		for (i = 12; (i < len_sus_curr_h) && (sum < len_max); i++) {
			if (i != 17 && i != 18) {
				if ((i % 2) == 0) {
					l = sprintf(pos, "/ %d ",
						    (iv[i + 1] << 8) + iv[i]);
					pos += l;
					sum += l;
				}
			}
		}
	}
	chg_err("gauge regs: %s\n", buf);
	return 0;
}

static int oplus_bq27z561_reg_dump(struct oplus_chg_ic_dev *ic_dev)
{
	struct chip_bq27z561 *chip;

	if (ic_dev == NULL) {
		chg_err("ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	return gauge_reg_dump(chip);
}

static int bq27z561_get_battery_mvolts(struct chip_bq27z561 *chip)
{
	int ret = 0;
	int volt = 0;
	int retry = RETRY_CNT;

	if (!chip)
		return -ENODEV;

	if (atomic_read(&chip->suspended) == 1)
		return chip->batt_vol_pre;

	if (atomic_read(&chip->locked) == 0) {
		ret = bq27z561_read_i2c(chip, chip->cmd_addr.reg_volt, &volt);
		if (ret) {
			dev_err(chip->dev, "error reading voltage, ret:%d\n", ret);
			chip->batt_cell_max_vol = chip->max_vol_pre;
			chip->batt_cell_min_vol = chip->min_vol_pre;
			return chip->batt_vol_pre;
		}

		chip->batt_cell_max_vol = volt;
		chip->batt_cell_min_vol = volt;
		chip->batt_vol_pre = volt;
		chip->max_vol_pre = chip->batt_cell_max_vol;
		chip->min_vol_pre = chip->batt_cell_min_vol;

		while (!normal_range_judge(VOLT_MAX, VOLT_MIN, chip->batt_cell_max_vol) && retry > 0 && !ret) {
			chg_err("bq27z561_get_battery_mvolts %d out of range, retry %d\n",
						chip->batt_cell_max_vol, retry);
			usleep_range(10000, 10000);
			ret = bq27z561_read_i2c(chip, chip->cmd_addr.reg_volt, &volt);
			if (ret) {
				dev_err(chip->dev, "error reading voltage, ret:%d\n", ret);
				chip->batt_cell_max_vol = chip->max_vol_pre;
				chip->batt_cell_min_vol = chip->min_vol_pre;
				return chip->batt_vol_pre;
			}

			chip->batt_cell_max_vol = volt;
			chip->batt_cell_min_vol = volt;
			chip->batt_vol_pre = volt;
			chip->max_vol_pre = chip->batt_cell_max_vol;
			chip->min_vol_pre = chip->batt_cell_min_vol;

			retry--;
		}
		return chip->batt_cell_max_vol;
	} else {
		return chip->batt_vol_pre;
	}
}

static int bq27z561_get_battery_mvolts_2cell_max(struct chip_bq27z561 *chip)
{
	if (!chip)
		return 0;

	return chip->batt_cell_max_vol;
}

static int bq27z561_get_battery_mvolts_2cell_min(struct chip_bq27z561 *chip)
{
	if (!chip)
		return 0;

	return chip->batt_cell_min_vol;
}

static int oplus_bq27z561_get_batt_vol(struct oplus_chg_ic_dev *ic_dev, int index, int *volt)
{
	struct chip_bq27z561 *chip;

	if (ic_dev == NULL || volt == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	switch (index) {
	case 0:
		if ((atomic_read(&chip->suspended) != 0) ||
				(atomic_read(&chip->locked) != 0)) {
			*volt = chip->volt_1_pre;
		} else {
			*volt = bq27z561_get_battery_mvolts(chip);
			chip->volt_1_pre = *volt;
		}
		break;
	case 1:
		if ((atomic_read(&chip->suspended) != 0) ||
				(atomic_read(&chip->locked) != 0)) {
			*volt = chip->volt_2_pre;
		} else {
			*volt = bq27z561_get_battery_mvolts(chip);
			chip->volt_2_pre = *volt;
		}
		break;
	default:
		chg_err("Unknown index(=%d), max is 2\n", index);
		return -EINVAL;
	}

	return 0;
}

__maybe_unused static int bq27z561_get_prev_battery_mvolts_2cell_max(struct chip_bq27z561 *chip)
{
	if (!chip)
		return 0;

	return chip->max_vol_pre;
}

static int oplus_bq27z561_get_batt_max(struct oplus_chg_ic_dev *ic_dev,
				      int *vol_mv)
{
	struct chip_bq27z561 *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	bq27z561_get_battery_mvolts(chip);
	if ((atomic_read(&chip->suspended) != 0) ||
	    (atomic_read(&chip->locked) != 0))
		*vol_mv = bq27z561_get_prev_battery_mvolts_2cell_max(chip);
	else
		*vol_mv = bq27z561_get_battery_mvolts_2cell_max(chip);

	return 0;
}

__maybe_unused static int bq27z561_get_prev_battery_mvolts_2cell_min(struct chip_bq27z561 *chip)
{
	if (!chip)
		return 0;

	return chip->min_vol_pre;
}

static int oplus_bq27z561_get_batt_min(struct oplus_chg_ic_dev *ic_dev, int *vol_mv)
{
	struct chip_bq27z561 *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	bq27z561_get_battery_mvolts(chip);
	if ((atomic_read(&chip->suspended) != 0) ||
	    (atomic_read(&chip->locked) != 0))
		*vol_mv = bq27z561_get_prev_battery_mvolts_2cell_min(chip);
	else
		*vol_mv = bq27z561_get_battery_mvolts_2cell_min(chip);

	return 0;
}

__maybe_unused static int bq27z561_get_prev_average_current(struct chip_bq27z561 *chip)
{
	if (!chip)
		return 0;

	return -chip->current_pre;
}

static int bq27z561_get_average_current(struct chip_bq27z561 *chip)
{
	int ret;
	int curr = 0;
	int retry = RETRY_CNT;
	int temp_curr = 0;

	if (!chip)
		return 0;

	if (atomic_read(&chip->suspended) == 1)
		return -chip->current_pre;

	if (atomic_read(&chip->locked) == 0) {
		ret = bq27z561_read_i2c(chip, chip->cmd_addr.reg_ai, &curr);
		if (!ret && (curr & REVE_CURR))
			temp_curr = ((~(curr - 1)) & 0xFFFF);
		else if (!ret)
			temp_curr = -curr;
		while (!normal_range_judge(CURR_MAX, CURR_MIN, temp_curr) && retry > 0 && !ret) {
			chg_err("bq27z561_get_average_current %d out of range, retry %d\n", temp_curr, retry);
			usleep_range(10000, 10000);
			ret = bq27z561_read_i2c(chip, chip->cmd_addr.reg_ai, &curr);
			if (!ret && (curr & REVE_CURR))
				temp_curr = ((~(curr - 1)) & 0xFFFF);
			else if (!ret)
				temp_curr = -curr;
			retry--;
		}
		if (ret) {
			dev_err(chip->dev, "error reading current.\n");
			return chip->current_pre;
		}
	} else {
		return -chip->current_pre;
	}
	/* negative current */
	if (curr & REVE_CURR)
		curr = -((~(curr - 1)) & 0xFFFF);

	chip->current_pre = curr;
	return -curr;
}

static int oplus_bq27z561_get_batt_curr(struct oplus_chg_ic_dev *ic_dev,
				       int *curr_ma)
{
	struct chip_bq27z561 *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	if ((atomic_read(&chip->suspended) != 0) ||
		(atomic_read(&chip->locked) != 0))
		*curr_ma = bq27z561_get_prev_average_current(chip);
	else
		*curr_ma = bq27z561_get_average_current(chip);

	return 0;
}

__maybe_unused static int bq27z561_get_prev_battery_temperature(struct chip_bq27z561 *chip)
{
	if (!chip)
		return 0;

	return chip->temp_pre + ZERO_DEGREE_CELSIUS_IN_TENTH_KELVIN;
}

static int bq27z561_get_battery_temperature(struct chip_bq27z561 *chip)
{
	int ret = 0;
	int temp = 0;
	static int count = 0;
	int retry = RETRY_CNT;

	if (!chip)
		return 0;

	if (atomic_read(&chip->suspended) == 1)
		return chip->temp_pre + ZERO_DEGREE_CELSIUS_IN_TENTH_KELVIN;

	if (atomic_read(&chip->locked) == 0) {
		ret = bq27z561_read_i2c(chip, chip->cmd_addr.reg_temp, &temp);
		while (!normal_range_judge(TEMP_MAX, TEMP_MIN, temp) && retry > 0 && !ret) {
			chg_err("bq27z561_get_battery_temperature %d out of range, retry %d\n", temp, retry);
			usleep_range(10000, 10000);
			ret = bq27z561_read_i2c(chip, chip->cmd_addr.reg_temp, &temp);
			retry--;
		}
		if (ret) {
			count++;
			dev_err(chip->dev, "error reading temperature\n");
			if (count > 1) {
				count = 0;
				chip->temp_pre = -400 - ZERO_DEGREE_CELSIUS_IN_TENTH_KELVIN;
				return -400;
			} else {
				return chip->temp_pre + ZERO_DEGREE_CELSIUS_IN_TENTH_KELVIN;
			}
		}
		count = 0;
	} else {
		return chip->temp_pre + ZERO_DEGREE_CELSIUS_IN_TENTH_KELVIN;
	}

	if ((temp + ZERO_DEGREE_CELSIUS_IN_TENTH_KELVIN) > 1000)
		temp = chip->temp_pre;

	chip->temp_pre = temp;
	return temp + ZERO_DEGREE_CELSIUS_IN_TENTH_KELVIN;
}

static int oplus_bq27z561_get_batt_temp(struct oplus_chg_ic_dev *ic_dev, int *temp)
{
	struct chip_bq27z561 *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	if ((atomic_read(&chip->suspended) != 0) ||
				(atomic_read(&chip->locked) != 0))
		*temp = bq27z561_get_prev_battery_temperature(chip);
	else
		*temp = bq27z561_get_battery_temperature(chip);

	return 0;
}

static int bq27z561_soc_calibrate(struct chip_bq27z561 *chip, int soc)
{
	unsigned int soc_calib;

	if (!chip)
		return 0;

	soc_calib = soc;
	if (soc >= 100)
		soc_calib = 100;
	else if (soc < 0)
		soc_calib = 0;

	chip->soc_pre = soc_calib;
	return soc_calib;
}

__maybe_unused static int bq27z561_get_prev_battery_soc(struct chip_bq27z561 *chip)
{
	if (!chip)
		return 50;

	return chip->soc_pre;
}

static int bq27z561_get_battery_soc(struct chip_bq27z561 *chip)
{
	int ret;
	int soc = 0;
	int retry = RETRY_CNT;

	if (!chip)
		return 50;

	if (atomic_read(&chip->suspended) == 1)
		return chip->soc_pre;

	if (atomic_read(&chip->locked) == 0) {
		ret = bq27z561_read_i2c(chip, chip->cmd_addr.reg_soc, &soc);
		while (!normal_range_judge(SOC_MAX, SOC_MIN, soc) && retry > 0 && !ret) {
			chg_err("bq27z5611_get_battery_soc %d out of range, retry %d\n", soc, retry);
			usleep_range(10000, 10000);
			ret = bq27z561_read_i2c(chip, chip->cmd_addr.reg_soc, &soc);
			retry--;
		}
		if (ret) {
			dev_err(chip->dev, "error reading soc.ret:%d\n", ret);
			goto read_soc_err;
		}
	} else {
		if (chip->soc_pre)
			return chip->soc_pre;
		else
			return 0;
	}
	soc = bq27z561_soc_calibrate(chip, soc);
	return soc;

read_soc_err:
	if (chip->soc_pre)
		return chip->soc_pre;
	else
		return 0;
}

static int oplus_bq27z561_get_batt_soc(struct oplus_chg_ic_dev *ic_dev, int *soc)
{
	struct chip_bq27z561 *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	if ((atomic_read(&chip->suspended) != 0) ||
					(atomic_read(&chip->locked) != 0))
		*soc = bq27z561_get_prev_battery_soc(chip);
	else
		*soc = bq27z561_get_battery_soc(chip);

	return 0;
}

__maybe_unused static int bq27z561_get_prev_batt_fcc(struct chip_bq27z561 *chip)
{
	if (!chip)
		return 0;

	return chip->fcc_pre;
}

static int bq27z561_get_true_fcc(struct chip_bq27z561 *chip, int *true_fcc)
{
	int ret;
	u8 buf[BQ27Z561_TRUE_FCC_NUM_SIZE] = { 0 };

	if (!chip || !true_fcc)
		return -EINVAL;

	ret = bq27z561_read_block(
		chip, BQ27Z561_REG_TRUE_FCC, buf, BQ27Z561_TRUE_FCC_NUM_SIZE, BQ27Z561_TRUE_FCC_OFFSET, false);
	if (ret < 0)
		return ret;

	*true_fcc = (buf[1] << 8) | buf[0];
	chg_info("true_fcc=%d\n", *true_fcc);

	return ret;
}

static void bq27z561_set_fcc_sync(struct chip_bq27z561 *chip)
{
	mutex_lock(&chip->bq27z561_alt_manufacturer_access);
	bq27z561_i2c_txsubcmd(chip, BQ27Z561_REG_CNTL1, BQ27Z561_FCC_SYNC_CMD);
	mutex_unlock(&chip->bq27z561_alt_manufacturer_access);
	chg_info("set fcc sync\n");
}

static void bq27z561_fcc_too_small_check_work(struct work_struct *work)
{
	int ret;
	int true_fcc = 0;
	struct chip_bq27z561 *chip = container_of(
		work, struct chip_bq27z561, fcc_too_small_check_work);

	ret = bq27z561_get_true_fcc(chip, &true_fcc);
	if (!ret && (true_fcc > 200)) /* TODO: true_fcc value is more than 200 */
		bq27z561_set_fcc_sync(chip);

	chip->fcc_too_small_checking = false;
}

static void bq27541_fcc_too_small_check(struct chip_bq27z561 *chip, int fcc)
{
	if (!chip)
		return;

	if (chip->batt_bq27z561) {
		if (chip->fcc_too_small_checking) {
			chg_info("fcc too small checking, ignore this time");
			return;
		}

		/* TODO: fcc value is less than 200 */
		if (fcc < 200) {
			chip->fcc_too_small_checking = true;
			schedule_work(&chip->fcc_too_small_check_work);
		}
	}
}

static int bq27z561_get_battery_fcc(struct chip_bq27z561 *chip)
{
	int ret = 0;
	int fcc = 0;
	int retry = RETRY_CNT;

	if (!chip)
		return 0;

	if (atomic_read(&chip->suspended) == 1)
		return chip->fcc_pre;

	if (atomic_read(&chip->locked) == 0) {
		ret = bq27z561_read_i2c(chip, chip->cmd_addr.reg_fcc, &fcc);
		while (!normal_range_judge(FCC_MAX, FCC_MIN, fcc) && retry > 0 && !ret) {
			chg_err("oplus_bq27z561_get_battery_fcc %d out of range, retry %d\n", fcc, retry);
			usleep_range(10000, 10000);
			ret = bq27z561_read_i2c(chip, chip->cmd_addr.reg_fcc, &fcc);
			retry--;
		}
		if (ret) {
			dev_err(chip->dev, "error reading fcc.\n");
			return ret;
		}
	} else {
		if (chip->fcc_pre)
			return chip->fcc_pre;
		else
			return 0;
	}
	chip->fcc_pre = fcc;
	bq27541_fcc_too_small_check(chip, fcc);
	return fcc;
}

static int oplus_bq27z561_get_batt_fcc(struct oplus_chg_ic_dev *ic_dev, int *fcc)
{
	struct chip_bq27z561 *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	if ((atomic_read(&chip->suspended) != 0) ||
					(atomic_read(&chip->locked) != 0))
		*fcc = bq27z561_get_prev_batt_fcc(chip);
	else
		*fcc = bq27z561_get_battery_fcc(chip);

	return 0;
}

static int bq27z561_get_battery_cc(struct chip_bq27z561 *chip)
{
	int ret = 0;
	int cc = 0;
	int retry = RETRY_CNT;

	if (!chip)
		return 0;

	if (atomic_read(&chip->suspended) == 1)
		return chip->cc_pre;

	if (atomic_read(&chip->locked) == 0) {
		ret = bq27z561_read_i2c(chip, chip->cmd_addr.reg_cc, &cc);
		while (!normal_range_judge(CC_MAX, CC_MIN, cc) && retry > 0 && !ret) {
			chg_err("bq27z561_get_battery_cc %d out of range, retry %d\n", cc, retry);
			usleep_range(10000, 10000);
			ret = bq27z561_read_i2c(chip, chip->cmd_addr.reg_cc, &cc);
			retry--;
		}
		if (ret) {
			dev_err(chip->dev, "error reading cc.\n");
			return 0;
		}
	} else {
		if (chip->cc_pre)
			return chip->cc_pre;
		else
			return 0;
	}
	chip->cc_pre = cc;
	return cc;
}

static int oplus_bq27z561_get_batt_cc(struct oplus_chg_ic_dev *ic_dev, int *cc)
{
	struct chip_bq27z561 *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	if ((atomic_read(&chip->suspended) != 0) ||
					(atomic_read(&chip->locked) != 0))
		*cc = chip->cc_pre;
	else
		*cc = bq27z561_get_battery_cc(chip);

	return 0;
}

static int bq27z561_get_batt_remaining_capacity(struct chip_bq27z561 *chip)
{
	int ret;
	int cap = 0;
	int retry = RETRY_CNT;

	if (!chip)
		return 0;

	if (atomic_read(&chip->suspended) == 1)
		return chip->rm_pre;

	if (atomic_read(&chip->locked) == 0) {
		ret = bq27z561_read_i2c(chip, chip->cmd_addr.reg_rm, &cap);
		while (!normal_range_judge(FCC_MAX, 0, cap) && retry > 0 && !ret) {
			chg_err("bq27z561_get_batt_remaining_capacity %d out of range, retry %d\n", cap, retry);
			usleep_range(10000, 10000);
			ret = bq27z561_read_i2c(chip, chip->cmd_addr.reg_rm, &cap);
			retry--;
		}
		if (ret) {
			dev_err(chip->dev, "error reading capacity.\n");
			return ret;
		}
		chip->rm_pre = cap;
		return chip->rm_pre;
	} else {
		return chip->rm_pre;
	}
}

__maybe_unused static int bq27z561_get_prev_batt_remaining_capacity(struct chip_bq27z561 *chip)
{
	if (!chip)
		return 0;

	return chip->rm_pre;
}

static int oplus_bq27z561_get_batt_rm(struct oplus_chg_ic_dev *ic_dev, int *rm)
{
	struct chip_bq27z561 *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	if ((atomic_read(&chip->suspended) != 0) ||
		(atomic_read(&chip->locked) != 0))
		*rm = bq27z561_get_prev_batt_remaining_capacity(chip);
	else
		*rm = bq27z561_get_batt_remaining_capacity(chip);

	return 0;
}

static int bq27z561_get_battery_soh(struct chip_bq27z561 *chip)
{
	int ret = 0;
	int soh = 0;
	int retry = RETRY_CNT;

	if (!chip)
		return 0;

	if (atomic_read(&chip->suspended) == 1)
		return chip->soh_pre;

	if (atomic_read(&chip->locked) == 0) {
		ret = bq27z561_read_i2c(chip, chip->cmd_addr.reg_soh, &soh);
		while (!normal_range_judge(SOH_MAX, SOH_MIN, soh) && retry > 0 && !ret) {
			chg_err("bq27z561_get_battery_soh %d out of range, retry %d\n", soh, retry);
			usleep_range(10000, 10000);
			ret = bq27z561_read_i2c(chip, chip->cmd_addr.reg_soh, &soh);
			retry--;
		}
		if (ret) {
			dev_err(chip->dev, "error reading fcc.\n");
			return 0;
		}
	} else {
		if (chip->soh_pre)
			return chip->soh_pre;
		else
			return 0;
	}
	chip->soh_pre = soh;
	return soh;
}

static int oplus_bq27z561_get_batt_soh(struct oplus_chg_ic_dev *ic_dev, int *soh)
{
	struct chip_bq27z561 *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	if ((atomic_read(&chip->suspended) != 0) ||
		(atomic_read(&chip->locked) != 0))
		*soh = chip->soh_pre;
	else
		*soh = bq27z561_get_battery_soh(chip);

	return 0;
}

#define GET_BATTERY_AUTH_RETRY_COUNT (5)
static bool bq27z561_get_battery_authenticate(struct chip_bq27z561 *chip)
{
	static int get_temp = 0;

	if (!chip)
		return true;

	if (chip->temp_pre == 0 || get_temp < GET_BATTERY_AUTH_RETRY_COUNT) {
		get_temp++;
		bq27z561_get_battery_temperature(chip);
		msleep(10);
		bq27z561_get_battery_temperature(chip);
	}

	if (chip->temp_pre == TEMP_MIN)
		return false;
	else
		return true;
}

static int oplus_bq27z561_get_batt_auth(struct oplus_chg_ic_dev *ic_dev, bool *pass)
{
	struct chip_bq27z561 *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);
	*pass = bq27z561_get_battery_authenticate(chip);

	return 0;
}

static bool bq27z561_get_battery_hmac(struct chip_bq27z561 *chip)
{
	if (!chip)
		return true;

	if (chip->support_sha256_hmac && !oplus_is_rf_ftm_mode())
		return init_sha256_gauge_auth(chip);
	else
		return true;
}

static int oplus_bq27z561_get_batt_hmac(struct oplus_chg_ic_dev *ic_dev, bool *pass)
{
	struct chip_bq27z561 *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);
	*pass = bq27z561_get_battery_hmac(chip);

	return 0;
}

static void bq27z561_set_battery_full(bool full)
{
	/* Do nothing */
}

static int oplus_bq27z561_set_batt_full(struct oplus_chg_ic_dev *ic_dev, bool full)
{
	bq27z561_set_battery_full(full);
	return 0;
}

int bq27z561_get_passedchg(struct chip_bq27z561 *chip, int *val)
{
	int rc = -1;
	u8 value = 0;
	s16 v = 0;

	if (chip == NULL || atomic_read(&chip->suspended) == 1)
		return rc;

	rc = bq27z561_read_i2c_onebyte(chip, 0x6C, &value);
	if (rc) {
		chg_err("%s read 0x6c fail\n", __func__);
		goto out;
	}
	v |= value;
	rc = bq27z561_read_i2c_onebyte(chip, 0x6D, &value);
	if (rc) {
		chg_err("%s read 0x6d fail\n", __func__);
		goto out;
	}
	v |= (value << 8);

	*val = v;

	if (atomic_read(&chip->suspended) == 1)
		return -EINVAL;
out:
	return rc;
}

static int oplus_bq27z561_get_passedchg(struct oplus_chg_ic_dev *ic_dev, int *val)
{
	struct chip_bq27z561 *chip;

	if (ic_dev == NULL) {
	       chg_err("oplus_chg_ic_dev is NULL");
	       return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);
	if (atomic_read(&chip->locked) != 0)
		return -EINVAL;

	return bq27z561_get_passedchg(chip, val);
}

static int oplus_bq27z561_set_lock(struct oplus_chg_ic_dev *ic_dev, bool lock)
{
	struct chip_bq27z561 *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	atomic_set(&chip->locked, lock ? 1 : 0);

	return 0;
}

static int oplus_bq27z561_is_locked(struct oplus_chg_ic_dev *ic_dev,
				   bool *locked)
{
	struct chip_bq27z561 *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	*locked = !!atomic_read(&chip->locked);

	return 0;
}

static int oplus_bq27z561_get_batt_num(struct oplus_chg_ic_dev *ic_dev, int *num)
{
	struct chip_bq27z561 *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	*num = chip->batt_num;

	return 0;
}

static int oplus_bq27561_get_gauge_type(struct oplus_chg_ic_dev *ic_dev, int *gauge_type)
{
	struct chip_bq27561 *chip;

	if (ic_dev == NULL || gauge_type == NULL) {
		chg_err("oplus_chg_ic_dev or gauge_type is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	*gauge_type = GAUGE_TYPE_PACK;

	return 0;
}

static int oplus_bq27561_set_seal_flag(struct oplus_chg_ic_dev *ic_dev, int seal_flag)
{
	struct chip_bq27z561 *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev or gauge_type is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	chip->bq27z561_seal_flag = seal_flag;

	if (seal_flag) {
		mutex_lock(&chip->bq27z561_alt_manufacturer_access);
		if (!bq27z561_deep_init(chip)) {
			chg_err("bq27z561 deep init failed");
			mutex_unlock(&chip->bq27z561_alt_manufacturer_access);
			return -EFAULT;
		}
		mutex_unlock(&chip->bq27z561_alt_manufacturer_access);
	} else {
		mutex_lock(&chip->bq27z561_alt_manufacturer_access);
		bq27z561_deep_deinit(chip);
		mutex_unlock(&chip->bq27z561_alt_manufacturer_access);
	}

	return 0;
}

static int oplus_bq27z561_get_device_type(struct oplus_chg_ic_dev *ic_dev, int *type)
{
	struct chip_bq27z561 *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	*type = chip->device_type;

	return 0;
}

static int
oplus_bq27z561_get_device_type_for_vooc(struct oplus_chg_ic_dev *ic_dev, int *type)
{
	struct chip_bq27z561 *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	*type = chip->device_type_for_vooc;

	return 0;
}

static int bq27z561_get_battery_gauge_type_for_bcc(struct oplus_chg_ic_dev *ic_dev, int *type)
{
	struct chip_bq27z561 *chip;
	int gauge_type;

	if (ic_dev == NULL || type == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	switch (chip->device_type) {
	case DEVICE_BQ27Z561:
		gauge_type = DEVICE_BQ27Z561;
		break;
	default:
		gauge_type = DEVICE_BQ27Z561;
		break;
	}

	*type = gauge_type;
	return 0;
}

static int bq27z561_get_dod0_parameters(
			struct chip_bq27z561 *chip, int *batt_dod0, int *batt_dod0_passed_q)
{
	int ret;
	int data_check;
	int try_count = BQ27Z561_SUBCMD_TRY_COUNT;
	u8 extend_data[16] = {0};
	struct gauge_track_info_reg extend = { BQ27Z561_SUBCMD_IT_STATUS2 , 14 };

	if (!chip || !batt_dod0 || !batt_dod0_passed_q)
		return -ENODEV;

try:
	mutex_lock(&chip->bq27z561_alt_manufacturer_access);
	ret = bq27z561_i2c_txsubcmd(chip, BQ27Z561_DATAFLASHBLOCK, extend.addr);
	if (ret < 0)
		goto error;
	usleep_range(1000, 1000);
	ret = bq27z561_read_i2c_block(chip, BQ27Z561_DATAFLASHBLOCK, (extend.len + 2), extend_data);
	if (ret < 0)
		goto error;

	data_check = (extend_data[1] << 0x8) | extend_data[0];
	if (try_count-- > 0 && data_check != extend.addr) {
		mutex_unlock(&chip->bq27z561_alt_manufacturer_access);
		chg_err("not match. extend_data[0]=0x%2x, extend_data[1]=0x%2x\n", extend_data[0], extend_data[1]);
		usleep_range(2000, 2000);
		goto try;
	}
	if (!try_count)
		goto error;
	*batt_dod0_passed_q = (extend_data[15] << 0x08) | extend_data[14];
	if (*batt_dod0_passed_q & REVE_CURR)
		*batt_dod0_passed_q = -((~(*batt_dod0_passed_q - 1)) & 0xFFFF);
	*batt_dod0 = (extend_data[13] << 0x08) | extend_data[12];
	chg_info("bq27z561_get_dod0_parameters dod0:%d, dod0_passed_q:%d\n", *batt_dod0, *batt_dod0_passed_q);

	mutex_unlock(&chip->bq27z561_alt_manufacturer_access);
	return 0;
error:
	mutex_unlock(&chip->bq27z561_alt_manufacturer_access);
	return ret;
}

static int bq27z561_get_battery_dod0(struct oplus_chg_ic_dev *ic_dev, int index, int *dod0)
{
	struct chip_bq27z561 *chip;
	int dod0_1;
	int dod0_2;
	int dod_passed_q;

	if (ic_dev == NULL || dod0 == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	if ((atomic_read(&chip->suspended) != 0) ||
	    	(atomic_read(&chip->locked) != 0)) {
		dod0_1 = chip->dod0_1_pre;
		dod0_2 = chip->dod0_2_pre;
	} else {
		bq27z561_get_dod0_parameters(chip, &dod0_1, &dod_passed_q);
		dod0_2 = dod0_1;
	}

	switch (index) {
	case 0:
		*dod0 = dod0_1;
		break;
	case 1:
		*dod0 = dod0_2;
		break;
	default:
		chg_err("Unknown index(=%d), max is 2\n", index);
		return -EINVAL;
	}

	return 0;
}

static int bq27z561_get_battery_dod0_passed_q(struct oplus_chg_ic_dev *ic_dev, int *dod_passed_q)
{
	struct chip_bq27z561 *chip;
	int batt_dod0;

	if (ic_dev == NULL || dod_passed_q == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	if ((atomic_read(&chip->suspended) != 0) ||
	    (atomic_read(&chip->locked) != 0))
		*dod_passed_q = chip->dod_passed_q_pre;
	else
		bq27z561_get_dod0_parameters(chip, &batt_dod0, dod_passed_q);

	return 0;
}

static int bq27z561_get_qmax_parameters(struct chip_bq27z561 *chip, int * qmax)
{
	int ret;
	int data_check;
	int try_count = BQ27Z561_SUBCMD_TRY_COUNT;
	u8 extend_data[4] = {0};
	struct gauge_track_info_reg extend = { BQ27Z561_SUBCMD_IT_STATUS3 , 2 };

	if (!chip || !qmax)
		return -ENODEV;
try:
	mutex_lock(&chip->bq27z561_alt_manufacturer_access);
	ret = bq27z561_i2c_txsubcmd(chip, BQ27Z561_DATAFLASHBLOCK, extend.addr);
	if (ret < 0) {
		mutex_unlock(&chip->bq27z561_alt_manufacturer_access);
		return ret;
	}
	usleep_range(1000, 1000);
	ret = bq27z561_read_i2c_block(chip, BQ27Z561_DATAFLASHBLOCK, (extend.len + 2), extend_data);
	if (ret < 0) {
		mutex_unlock(&chip->bq27z561_alt_manufacturer_access);
		return ret;
	}

	data_check = (extend_data[1] << 0x8) | extend_data[0];
	if (try_count-- > 0 && data_check != extend.addr) {
		chg_err("0x%4x not match. try_count=%d, extend_data[0]=0x%2x, extend_data[1]=0x%2x\n",
			extend.addr, try_count, extend_data[0], extend_data[1]);
		mutex_unlock(&chip->bq27z561_alt_manufacturer_access);
		usleep_range(2000, 2000);
		goto try;
	}
	if (!try_count) {
		mutex_unlock(&chip->bq27z561_alt_manufacturer_access);
		return -EINVAL;
	}
	*qmax = (extend_data[3] << 0x08) | extend_data[2];
	chip->qmax_pre = *qmax;
	mutex_unlock(&chip->bq27z561_alt_manufacturer_access);
	chg_info("bq27z561_get_qmax_parameters qmax:%d\n", *qmax);

	return 0;
}

static int bq27z561_get_battery_qmax(struct oplus_chg_ic_dev *ic_dev, int index, int *qmax)
{
	struct chip_bq27z561 *chip;
	int retry = RETRY_CNT;

	if (ic_dev == NULL || qmax == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	if ((atomic_read(&chip->suspended) != 0) ||
	    (atomic_read(&chip->locked) != 0)) {
		*qmax = chip->qmax_pre;
	} else {
		bq27z561_get_qmax_parameters(chip, qmax);
		while (!normal_range_judge(QMAX_MAX, QMAX_MIN, *qmax) && retry > 0) {
			chg_err("bq28z610_get_battery_qmax %d out of range, retry %d\n", *qmax, retry);
			usleep_range(10000, 10000);
			bq27z561_get_qmax_parameters(chip, qmax);
			retry--;
		}
	}

	chg_info("index(=%d), qmax is %d\n", index, *qmax);

	return 0;
}

static int oplus_bq27z561_is_suspend(struct oplus_chg_ic_dev *ic_dev, bool *suspend)
{
	struct chip_bq27z561 *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	*suspend = atomic_read(&chip->suspended);

	return 0;
}

static int oplus_bq27z561_get_batt_exist(struct oplus_chg_ic_dev *ic_dev, bool *exist)
{
	struct chip_bq27z561 *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	if (atomic_read(&chip->i2c_err_count) > I2C_ERR_MAX)
		*exist = false;
	else
		*exist = true;

	return 0;
}

static void *oplus_chg_get_func(
			struct oplus_chg_ic_dev *ic_dev, enum oplus_chg_ic_func func_id)
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
					       oplus_bq27z561_init);
		break;
	case OPLUS_IC_FUNC_EXIT:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_EXIT,
					       oplus_bq27z561_exit);
		break;
	case OPLUS_IC_FUNC_REG_DUMP:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_REG_DUMP,
					       oplus_bq27z561_reg_dump);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_VOL:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_BATT_VOL,
					       oplus_bq27z561_get_batt_vol);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_MAX:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_BATT_MAX,
					       oplus_bq27z561_get_batt_max);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_MIN:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_BATT_MIN,
					       oplus_bq27z561_get_batt_min);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_CURR:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_BATT_CURR,
							oplus_bq27z561_get_batt_curr);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_TEMP:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_BATT_TEMP,
							oplus_bq27z561_get_batt_temp);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_SOC:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_BATT_SOC,
					       oplus_bq27z561_get_batt_soc);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_FCC:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_BATT_FCC,
					       oplus_bq27z561_get_batt_fcc);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_CC:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_BATT_CC,
					       oplus_bq27z561_get_batt_cc);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_RM:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_BATT_RM,
					       oplus_bq27z561_get_batt_rm);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_SOH:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_BATT_SOH,
					       oplus_bq27z561_get_batt_soh);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_AUTH:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_BATT_AUTH,
							oplus_bq27z561_get_batt_auth);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_HMAC:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_BATT_HMAC,
							oplus_bq27z561_get_batt_hmac);
		break;
	case OPLUS_IC_FUNC_GAUGE_SET_BATT_FULL:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_SET_BATT_FULL,
							oplus_bq27z561_set_batt_full);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_PASSEDCHG:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_PASSEDCHG,
							oplus_bq27z561_get_passedchg);
		break;
	case OPLUS_IC_FUNC_GAUGE_SET_LOCK:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_SET_LOCK,
					       oplus_bq27z561_set_lock);
		break;
	case OPLUS_IC_FUNC_GAUGE_IS_LOCKED:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_IS_LOCKED,
					       oplus_bq27z561_is_locked);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_NUM:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_BATT_NUM,
					       oplus_bq27z561_get_batt_num);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_GAUGE_TYPE:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_GAUGE_TYPE,
					       oplus_bq27561_get_gauge_type);
		break;
	case OPLUS_IC_FUNC_GAUGE_SET_SEAL_FLAG:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_SET_SEAL_FLAG,
					       oplus_bq27561_set_seal_flag);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_DEVICE_TYPE:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_DEVICE_TYPE,
			oplus_bq27z561_get_device_type);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_DEVICE_TYPE_FOR_VOOC:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_DEVICE_TYPE_FOR_VOOC,
			oplus_bq27z561_get_device_type_for_vooc);
		break;
	case OPLUS_IC_FUNC_GAUGE_IS_SUSPEND:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_IS_SUSPEND,
					       oplus_bq27z561_is_suspend);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_DOD0:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_DOD0,
					       bq27z561_get_battery_dod0);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_DOD0_PASSED_Q:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_DOD0_PASSED_Q,
					       bq27z561_get_battery_dod0_passed_q);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_QMAX:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_QMAX,
					       bq27z561_get_battery_qmax);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_DEVICE_TYPE_FOR_BCC:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_DEVICE_TYPE_FOR_BCC,
					       bq27z561_get_battery_gauge_type_for_bcc);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_EXIST:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_BATT_EXIST,
							oplus_bq27z561_get_batt_exist);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_REG_INFO:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_REG_INFO,
					      oplus_bq27z561_get_reg_info);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_CALIB_TIME:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_CALIB_TIME,
					      oplus_bq27z561_get_calib_time);
		break;
	case OPLUS_IC_FUNC_GAUGE_SET_CALIB_TIME:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_SET_CALIB_TIME,
					      oplus_bq27z561_set_calib_time);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_BATT_SN:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_BATT_SN,
					      oplus_get_battinfo_sn);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_FIRST_USAGE_DATE:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_FIRST_USAGE_DATE,
					       oplus_get_first_usage_date);
		break;
	case OPLUS_IC_FUNC_GAUGE_SET_FIRST_USAGE_DATE:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_SET_FIRST_USAGE_DATE,
					       oplus_set_first_usage_date);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_UI_CC:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_UI_CC,
					      oplus_get_ui_cycle_count);
		break;
	case OPLUS_IC_FUNC_GAUGE_SET_UI_CC:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_SET_UI_CC,
					      oplus_set_ui_cycle_count);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_UI_SOH:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_UI_SOH,
					      oplus_get_ui_soh);
		break;
	case OPLUS_IC_FUNC_GAUGE_SET_UI_SOH:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_SET_UI_SOH,
					      oplus_set_ui_soh);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_USED_FLAG:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_USED_FLAG,
				       oplus_get_used_flag);
		break;
	case OPLUS_IC_FUNC_GAUGE_SET_USED_FLAG:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_SET_USED_FLAG,
					       oplus_set_used_flag);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_MANU_DATE:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_MANU_DATE,
					      oplus_get_manu_date);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_DEC_FG_TYPE:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_DEC_FG_TYPE,
					   oplus_get_dec_fg_type);
		break;
	case OPLUS_IC_FUNC_GAUGE_GET_DEC_CV_SOH:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_GAUGE_GET_DEC_CV_SOH,
					   oplus_bq27z561_get_dec_cv_soh);
		break;
	default:
		chg_err("this func(=%d) is not supported\n", func_id);
		func = NULL;
		break;
	}

	return func;
}

static void bq27z561_check_iic_recover(struct work_struct *work)
{
	struct chip_bq27z561 *chip = container_of(
				work, struct chip_bq27z561, check_iic_recover.work);

	/* workaround for I2C pull SDA can't trigger error issue 230504153935012779 */
	if (chip->i2c_rst_ext)
		bq27z561_get_battery_temperature(chip);
	else
	/* end workaround 230504153935012779 */
		bq27z561_get_battery_soc(chip);
	chg_err("gauge online state:%d\n", chip->ic_dev->online);
	if (!chip->ic_dev->online)
		schedule_delayed_work(&chip->check_iic_recover, msecs_to_jiffies(CHECK_IIC_RECOVER_TIME));
	else
	 	oplus_chg_ic_virq_trigger(chip->ic_dev, OPLUS_IC_VIRQ_ONLINE);
}

struct oplus_chg_ic_virq bq27z561_virq_table[] = {
	{ .virq_id = OPLUS_IC_VIRQ_ERR },
	{ .virq_id = OPLUS_IC_VIRQ_ONLINE },
	{ .virq_id = OPLUS_IC_VIRQ_OFFLINE },
	{ .virq_id = OPLUS_IC_VIRQ_RESUME },
};

#if IS_ENABLED(CONFIG_OPLUS_FEATURE_CHG_DEBUG_KIT)
static struct regmap_config bq27z561_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0xffff,
};
#endif

static void fg_ic_init(struct chip_bq27z561 *fg_ic)
{
	fg_ic->soc_pre = 50;
	fg_ic->batt_vol_pre = 3800;
	fg_ic->fc_pre = 0;
	fg_ic->qm_pre = 0;
	fg_ic->pd_pre = 0;
	fg_ic->rcu_pre = 0;
	fg_ic->rcf_pre = 0;
	fg_ic->fcu_pre = 0;
	fg_ic->fcf_pre = 0;
	fg_ic->sou_pre = 0;
	fg_ic->do0_pre = 0;
	fg_ic->doe_pre = 0;
	fg_ic->trm_pre = 0;
	fg_ic->pc_pre = 0;
	fg_ic->qs_pre = 0;

	fg_ic->max_vol_pre = 3800;
	fg_ic->min_vol_pre = 3800;
	fg_ic->current_pre = 999;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 3, 0))
static int bq27z561_driver_probe(struct i2c_client *client)
#else
static int bq27z561_driver_probe(struct i2c_client *client, const struct i2c_device_id *id)
#endif
{
	struct chip_bq27z561 *fg_ic;
	enum oplus_chg_ic_type ic_type;
	int ic_index;
	struct oplus_chg_ic_cfg ic_cfg = { 0 };
	int rc = 0;

	fg_ic = devm_kzalloc(&client->dev, sizeof(*fg_ic), GFP_KERNEL);
	if (!fg_ic) {
		dev_err(&client->dev, "failed to allocate device info data\n");
		return -ENOMEM;
	}

	i2c_set_clientdata(client, fg_ic);
	fg_ic->dev = &client->dev;
	fg_ic->client = client;

#if IS_ENABLED(CONFIG_OPLUS_FEATURE_CHG_DEBUG_KIT)
	fg_ic->regmap = devm_regmap_init_i2c(client, &bq27z561_regmap_config);
	if (!fg_ic->regmap) {
		rc = -ENODEV;
		goto regmap_init_err;
	}
#endif /* CONFIG_OPLUS_FEATURE_CHG_DEBUG_KIT */

	atomic_set(&fg_ic->suspended, 0);
	mutex_init(&fg_ic->chip_mutex);
	mutex_init(&fg_ic->calib_time_mutex);
	mutex_init(&fg_ic->bq27z561_alt_manufacturer_access);
	bq27z561_parse_dt(fg_ic);
	/* workaround for I2C pull SDA can't trigger error issue 230504153935012779 */
	fg_ic->err_status = false;
	/* end workaround 230504153935012779 */

	bq27z561_hw_config(fg_ic);
	INIT_WORK(&fg_ic->fcc_too_small_check_work, bq27z561_fcc_too_small_check_work);
	INIT_DELAYED_WORK(&fg_ic->check_iic_recover, bq27z561_check_iic_recover);
	fg_ic_init(fg_ic);

	register_gauge_devinfo(fg_ic);
	fg_ic->sha256_authenticate_data =
			devm_kzalloc(&client->dev, sizeof(struct oplus_gauge_sha256_auth), GFP_KERNEL);
	if (!fg_ic->sha256_authenticate_data) {
		chg_err("kzalloc() authenticate_data failed.\n");
		devm_kfree(&client->dev, fg_ic);
		fg_ic = NULL;
		return -ENOMEM;
	}

	oplus_bq27z561_get_batt_sn(fg_ic);

	atomic_set(&fg_ic->locked, 0);
	rc = of_property_read_u32(fg_ic->dev->of_node, "oplus,ic_type", &ic_type);
	if (rc < 0) {
		chg_err("can't get ic type, rc=%d\n", rc);
		goto error;
	}
	rc = of_property_read_u32(fg_ic->dev->of_node, "oplus,ic_index", &ic_index);
	if (rc < 0) {
		chg_err("can't get ic index, rc=%d\n", rc);
		goto error;
	}
	ic_cfg.name = fg_ic->dev->of_node->name;
	ic_cfg.index = ic_index;

	switch (fg_ic->device_type) {
	case DEVICE_BQ27Z561:
		snprintf(ic_cfg.manu_name, OPLUS_CHG_IC_MANU_NAME_MAX - 1, "gauge-bq27z561:%d", ic_index);
		snprintf(ic_cfg.fw_id, OPLUS_CHG_IC_FW_ID_MAX - 1, "0x00");
		break;
	default:
		snprintf(ic_cfg.manu_name, OPLUS_CHG_IC_MANU_NAME_MAX - 1, "gauge-unknown:%d", ic_index);
		snprintf(ic_cfg.fw_id, OPLUS_CHG_IC_FW_ID_MAX - 1, "0x00");
		break;
	}

#if IS_ENABLED(CONFIG_OPLUS_FEATURE_CHG_DEBUG_KIT)
	fg_ic->odb = devm_oplus_device_bus_register(fg_ic->dev, &bq27z561_regmap_config, ic_cfg.manu_name);
	if (IS_ERR_OR_NULL(fg_ic->odb)) {
		chg_err("register odb error\n");
		rc = -EFAULT;
		goto error;
	}
#endif /* CONFIG_OPLUS_FEATURE_CHG_DEBUG_KIT */

	ic_cfg.type = ic_type;
	ic_cfg.get_func = oplus_chg_get_func;
	ic_cfg.virq_data = bq27z561_virq_table;
	ic_cfg.virq_num = ARRAY_SIZE(bq27z561_virq_table);
	ic_cfg.of_node = fg_ic->dev->of_node;
	fg_ic->ic_dev = devm_oplus_chg_ic_register(fg_ic->dev, &ic_cfg);
	if (!fg_ic->ic_dev) {
		rc = -ENODEV;
		chg_err("register %s error\n", fg_ic->dev->of_node->name);
		goto ic_reg_error;
	}
	chg_info("register %s\n", fg_ic->dev->of_node->name);

#ifndef CONFIG_OPLUS_CHARGER_MTK
	oplus_vooc_get_fastchg_started_pfunc(&oplus_vooc_get_fastchg_started);
	oplus_vooc_get_fastchg_ing_pfunc(&oplus_vooc_get_fastchg_ing);
#endif

	oplus_bq27z561_init(fg_ic->ic_dev);
	fg_ic->bq27z561_seal_flag = 0;
	INIT_DELAYED_WORK(&fg_ic->get_manu_battinfo_work, oplus_get_manu_battinfo_work);
	schedule_delayed_work(&fg_ic->get_manu_battinfo_work, 0);
	chg_info("bq27z561_driver_probe success\n");
	return 0;

ic_reg_error:
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_CHG_DEBUG_KIT)
	devm_oplus_device_bus_unregister(fg_ic->odb);
#endif
error:
#if IS_ENABLED(CONFIG_OPLUS_FEATURE_CHG_DEBUG_KIT)
regmap_init_err:
#endif
	devm_kfree(&client->dev, fg_ic);
	return rc;
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(6, 0, 0))
static int bq27z561_driver_remove(struct i2c_client *client)
#else
static void bq27z561_driver_remove(struct i2c_client *client)
#endif
{
	struct chip_bq27z561 *chip = i2c_get_clientdata(client);

#if (LINUX_VERSION_CODE < KERNEL_VERSION(6, 0, 0))
	if (chip == NULL)
		return -ENODEV;
#else
	if (chip == NULL)
		return;
#endif

	devm_kfree(&client->dev, chip);

#if (LINUX_VERSION_CODE < KERNEL_VERSION(6, 0, 0))
	return 0;
#else
	return;
#endif
}

/**********************************************************
  *
  *   [platform_driver API]
  *
  *********************************************************/

static const struct of_device_id bq27z561_match[] = {
	{ .compatible = "oplus,bq27z561-battery" },
	{},
};

static const struct i2c_device_id bq27z561_id[] = {
	{ "bq27z561-battery", 0 },
	{},
};
MODULE_DEVICE_TABLE(i2c, bq27z561_id);

static struct i2c_driver bq27z561_i2c_driver = {
	.driver = {
	.name = "bq27z561-battery",
	.owner = THIS_MODULE,
	.of_match_table = bq27z561_match,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))
	.pm = &bq27z561_pm_ops,
#endif
	},
	.probe = bq27z561_driver_probe,
	.remove = bq27z561_driver_remove,
	.shutdown = bq27z561_reset,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 4, 0))
	.resume = bq27z561_resume,
	.suspend = bq27z561_suspend,
#endif
	.id_table = bq27z561_id,
};

static __init int bq27z561_driver_init(void)
{
	return i2c_add_driver(&bq27z561_i2c_driver);
}

static __exit void bq27z561_driver_exit(void)
{
	i2c_del_driver(&bq27z561_i2c_driver);
}

oplus_chg_module_register(bq27z561_driver);

MODULE_DESCRIPTION("Driver for bq27z561 charger chip");
MODULE_LICENSE("GPL v2");

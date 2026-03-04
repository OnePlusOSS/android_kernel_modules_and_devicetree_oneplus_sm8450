// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2023 Oplus. All rights reserved.
 */

#define pr_fmt(fmt) "[bq27426] %s(%d): " fmt, __func__, __LINE__

#include <linux/version.h>
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0))
#include <linux/unaligned.h>
#else
#include <asm/unaligned.h>
#endif
#include <linux/acpi.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/idr.h>
#include <linux/interrupt.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/module.h>
#include <linux/of_gpio.h>
#include <linux/param.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/random.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/workqueue.h>
#include <asm/div64.h>
#include <linux/iio/consumer.h>
#include <linux/sched/clock.h>
#include <linux/rtc.h>

#include "../oplus_charger.h"
#include "oplus_bq27541.h"
#include "oplus_bqfs.h"

#define IIC_ADDR_OF_2_KERNEL(addr) ((u8)((u8)addr >> 1))
#define CMD_MAX_DATA_SIZE	32

typedef enum {
	CMD_INVALID = 0,
	CMD_R,
	CMD_W,
	CMD_C,
	CMD_X,
} cmd_type_t;

typedef struct {
	cmd_type_t cmd_type;
	uint8_t addr;
	uint8_t reg;
	union {
		uint8_t bytes[CMD_MAX_DATA_SIZE + 1];
		uint16_t delay;
	} data;
	uint8_t  data_len;
	uint16_t line_num;
} bqfs_cmd_t;

static int bqfs_read_word(struct chip_bq27541 *chip, int cmd, int *returnData)
{
	if (!chip->client) {
		chg_err(" chip->client NULL, return\n");
		return 0;
	}
	if(oplus_is_rf_ftm_mode()) {
		return 0;
	}
	if (cmd == BQ27541_BQ27411_CMD_INVALID) {
		return 0;
	}

	mutex_lock(&chip->chip_mutex);
	*returnData = i2c_smbus_read_word_data(chip->client, cmd);

	if (*returnData < 0) {
		chg_err("reg0x%x read err, rc = %d\n", cmd, *returnData);
		mutex_unlock(&chip->chip_mutex);
		return *returnData;
	}
	mutex_unlock(&chip->chip_mutex);

	return 0;
}

static int bqfs_write_word(struct chip_bq27541 *chip, int cmd, int writeData)
{
	int rc = 0;

	if (!chip->client) {
		pr_err(" chip->client NULL, return\n");
		return 0;
	}
	if(oplus_is_rf_ftm_mode()) {
		return 0;
	}
	if (cmd == BQ27541_BQ27411_CMD_INVALID) {
		return 0;
	}
	mutex_lock(&chip->chip_mutex);
	rc = i2c_smbus_write_word_data(chip->client, cmd, writeData);

	if (rc < 0) {
		pr_err("reg0x%x write 0x%x err, rc = %d\n", cmd, writeData, rc);
		mutex_unlock(&chip->chip_mutex);
		return rc;
	}
	mutex_unlock(&chip->chip_mutex);
	return 0;
}

static s32 bqfs_fg_read_block(struct chip_bq27541 *chip, uint8_t addr, uint8_t reg, uint8_t *buf, uint8_t len)
{
	static struct i2c_msg msg[2];
	u8 i2c_addr = IIC_ADDR_OF_2_KERNEL(addr);
	s32 ret;

	if (!chip || !chip->client || !chip->client->adapter)
		return -ENODEV;

	if (oplus_is_rf_ftm_mode())
		return 0;

	mutex_lock(&chip->chip_mutex);

	msg[0].addr = i2c_addr;
	msg[0].flags = 0;
	msg[0].buf = &(reg);
	msg[0].len = sizeof(u8);
	msg[1].addr = i2c_addr;
	msg[1].flags = I2C_M_RD;
	msg[1].buf = buf;
	msg[1].len = len;
	ret = (s32)i2c_transfer(chip->client->adapter, msg, ARRAY_SIZE(msg));

	mutex_unlock(&chip->chip_mutex);
	return ret;
}
static s32 bqfs_fg_write_block(struct chip_bq27541 *chip, uint8_t addr, uint8_t reg, uint8_t *buf, uint8_t len)
{
#define WRITE_BUF_MAX_LEN 32
	static struct i2c_msg msg[1];
	static u8 write_buf[WRITE_BUF_MAX_LEN];
	u8 i2c_addr = IIC_ADDR_OF_2_KERNEL(addr);
	u8 length = len;
	s32 ret;

	if (!chip || !chip->client || !chip->client->adapter)
		return -ENODEV;

	if (oplus_is_rf_ftm_mode())
		return 0;

	if ((length <= 0) || (length + 1 >= WRITE_BUF_MAX_LEN)) {
		pr_err("i2c write buffer fail: length invalid!\n");
		return -1;
	}

	mutex_lock(&chip->chip_mutex);
	memset(write_buf, 0, WRITE_BUF_MAX_LEN * sizeof(u8));
	write_buf[0] = reg;
	memcpy(&write_buf[1], buf, length);

	msg[0].addr = i2c_addr;
	msg[0].flags = 0;
	msg[0].buf = write_buf;
	msg[0].len = sizeof(u8) * (length + 1);

	ret = i2c_transfer(chip->client->adapter, msg, ARRAY_SIZE(msg));
	if (ret < 0) {
		pr_err("i2c write buffer fail: can't write reg 0x%02X\n", reg);
	}

	mutex_unlock(&chip->chip_mutex);
	return (ret < 0) ? ret : 0;
}

static bool bqfs_fg_fw_update_write_block(struct chip_bq27541 *chip, uint8_t addr, uint8_t reg, uint8_t* buf, uint8_t len)
{
#define I2C_BLK_SIZE	30
	int ret;
	uint8_t wr_len = 0;

	while (len > I2C_BLK_SIZE) {
		ret = bqfs_fg_write_block(chip, addr, reg + wr_len, buf + wr_len, I2C_BLK_SIZE);
		if (ret < 0)
			return false;
		wr_len += I2C_BLK_SIZE;
		len -= I2C_BLK_SIZE;
	}

	if (len) {
		ret = bqfs_fg_write_block(chip, addr, reg + wr_len, buf + wr_len, len);
		if (ret < 0)
			return false;
	}

	return true;
}

static bool bqfs_fg_fw_update_cmd(struct chip_bq27541 *chip, const bqfs_cmd_t *cmd)
{
	int ret;
	uint8_t tmp_buf[10];

	switch (cmd->cmd_type) {
	case CMD_R:
		ret = bqfs_fg_read_block(chip, cmd->addr, cmd->reg, (uint8_t *)&cmd->data.bytes, cmd->data_len);
		if (ret < 0)
			return false;
		else
			return true;
		break;
	case CMD_W:
		return bqfs_fg_fw_update_write_block(chip, cmd->addr, cmd->reg,
					(uint8_t *)&cmd->data.bytes,
					cmd->data_len);
	case CMD_C:
		if (bqfs_fg_read_block(chip, cmd->addr, cmd->reg, tmp_buf, cmd->data_len) < 0)
			return false;
		if (memcmp(tmp_buf, cmd->data.bytes, cmd->data_len))
			return false;

		return true;
	case CMD_X:
		mdelay(cmd->data.delay);
		return true;
	default:
		chg_err("Unsupported command at line\n");
		return false;
	}
}

static void bqfs_cntl_cmd(struct chip_bq27541 *chip, int subcmd)
{
	bqfs_write_word(chip, BQ27426_REG_CNTL, subcmd);
}

static void bqfs_cntl_subcmd(struct chip_bq27541 *chip, int subcmd)
{
	bqfs_write_word(chip, 0x3E, subcmd);
}

static int bq27426_sealed(struct chip_bq27541 *chip)
{
	int value = 0;
	bqfs_cntl_cmd(chip, BQ27426_SUBCMD_CTNL_STATUS);
	usleep_range(10000, 10000);
	bqfs_read_word(chip, BQ27426_REG_CNTL, &value);

	if (value & BIT(13)) {
		pr_err("bq27426 sealed, value = %x return 1\n", value);
		return 1;
	} else {
		pr_err("bq27426 unseal, value = %x return 0\n", value);
		return 0;
	}
}

static int bq27426_unseal(struct chip_bq27541 *chip)
{
	int retry = 2;
	int rc = 0;
	int value = 0;
	mutex_lock(&chip->gauge_alt_manufacturer_access);
	if (!bq27426_sealed(chip)) {
		mutex_unlock(&chip->gauge_alt_manufacturer_access);
		pr_err("bq27426 unsealed, return\n");
		return rc;
	}

	do {
		bqfs_cntl_cmd(chip, 0x8000);
		usleep_range(10000, 10000);
		bqfs_cntl_cmd(chip, 0x8000);
		usleep_range(10000, 10000);
		bqfs_cntl_cmd(chip, BQ27426_SUBCMD_CTNL_STATUS);
		usleep_range(10000, 10000);
		bqfs_read_word(chip, BQ27426_REG_CNTL, &value);
		if (!(value & BIT(13))) {
			retry = 0;
			rc = 0;
		} else {
			retry--;
			rc = -1;
		}
	} while (retry > 0);
	mutex_unlock(&chip->gauge_alt_manufacturer_access);

	pr_err("%s [%d][0x%x]\n", __func__, rc, value);

	return rc;
}

static int bq27426_seal(struct chip_bq27541 *chip)
{
	int retry = 2;
	int rc = 0;
	int value = 0;

	mutex_lock(&chip->gauge_alt_manufacturer_access);
	if (bq27426_sealed(chip)) {
		mutex_unlock(&chip->gauge_alt_manufacturer_access);
		pr_err("bq8z610 sealed, return\n");
		return rc;
	}

	do {
		bqfs_cntl_cmd(chip, 0x0020);
		usleep_range(10000, 10000);

		bqfs_cntl_cmd(chip, BQ27426_SUBCMD_CTNL_STATUS);
		usleep_range(10000, 10000);

		bqfs_read_word(chip, BQ27426_REG_CNTL, &value);
		if (value & BIT(13)) {
			retry = 0;
			rc = 0;
		} else {
			retry--;
			rc = -1;
		}
	} while (retry > 0);
	mutex_unlock(&chip->gauge_alt_manufacturer_access);
	pr_err("%s [%d][0x%x]\n", __func__, rc, value);

	return rc;
}

void bq27426_modify_soc_smooth_parameter(struct chip_bq27541 *chip, bool on)
{
	int rc = 0;
	int value = 0;
	u8 oldl_csum = 0, byte0 = 0, byte1_old = 0, byte1_new = 0, new_csum = 0, temp = 0;

	if (!chip->bqfs_info.bqfs_ship)
		return;

	if (bq27426_unseal(chip)) {
		chg_err("bq27426_unseal fail !\n");
		return;
	}
	mutex_lock(&chip->gauge_alt_manufacturer_access);

	gauge_i2c_txsubcmd_onebyte(chip, 0x61, 0x00);

	bqfs_cntl_subcmd(chip, 0x0040);
	usleep_range(10000, 10000);

	bqfs_read_word(chip, 0x40, &value);
	if ((on && (value & BIT(13))) || (!on && !(value & BIT(13)))) {
		rc = -1;
		mutex_unlock(&chip->gauge_alt_manufacturer_access);
		goto smooth_exit;
	}

	bqfs_cntl_cmd(chip, 0x0013);
	usleep_range(1100000, 1100000);
	bq27541_read_i2c_onebyte(chip, 0x06, &temp);

	gauge_i2c_txsubcmd_onebyte(chip, 0x61, 0x00);

	bqfs_cntl_subcmd(chip, 0x0040);
	usleep_range(10000, 10000);

	bq27541_read_i2c_onebyte(chip, 0x60, &oldl_csum);

	bqfs_read_word(chip, 0x40, &value);
	byte0 = value & 0xFF;
	byte1_old = value >> 8;

	if (on)
		byte1_new = byte1_old | BIT(5);
	else
		byte1_new = byte1_old & ~BIT(5);
	value = (byte0 | (byte1_new << 8));
	bqfs_write_word(chip, 0x40, value);

	temp = (0xFF - oldl_csum - byte1_old) % 256;
	new_csum = 0xFF - (temp + byte1_new) % 256;

	gauge_i2c_txsubcmd_onebyte(chip, 0x60, new_csum);
	bqfs_cntl_cmd(chip, 0x0042);
	usleep_range(1100000, 1100000);
	mutex_unlock(&chip->gauge_alt_manufacturer_access);

	rc = 1;

smooth_exit:

	if (bq27426_seal(chip))
		chg_err("bq27411 seal fail\n");

	chg_err("[%d, %d] [0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x] end\n", on, rc, byte0, byte1_old, value, temp, new_csum, oldl_csum);
}

enum {
	BAT_TYPE_UNKNOWN,
	BAT_TYPE_LIWINON, /* 1K resistance, adc:[70,180]*/
	BAT_TYPE_COSMX, /* 15 resistance, adc:[180,350]*/
	BAT_TYPE_ATL, /* 68K resistance, adc:[550,790]*/
	BAT_TYPE_MAX,
};

static int oplus_bqfs_get_iio_channel(struct chip_bq27541 *chip, const char *propname, struct iio_channel **chan)
{
	int rc = 0;

	rc = of_property_match_string(chip->dev->of_node, "io-channel-names", propname);
	if (rc < 0)
		return rc;

	*chan = iio_channel_get(chip->dev, propname);
	if (IS_ERR(*chan)) {
		rc = PTR_ERR(*chan);
		if (rc != -EPROBE_DEFER)
			chg_err("%s channel unavailable, %d\n", propname, rc);
		*chan = NULL;
	}

	return rc;
}

#define UNIT_TRANS_1000		1000
#define BATTID_ARR_LEN 3
#define BATTID_ARR_WIDTH 3
/* This function is for mainboard fuelgauge. Use adc to judge battery id */
int oplus_battery_type_check_bqfs(struct chip_bq27541 *chip)
{
	int value = 0;
	int ret = -1;
	int battery_type = BAT_TYPE_UNKNOWN;
	int batt_id_vol[BATTID_ARR_LEN][BATTID_ARR_WIDTH] = {	{70, 180},
								{180, 350},
								{550, 790}};

	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: chip_bq27541 not ready!\n", __func__);
		return false;
	}
	if (chip->device_type != DEVICE_BQ27426) {
		return true;
	}
	if (IS_ERR_OR_NULL(chip->batt_id_chan)) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: chg->iio.batt_id_chan is NULL !\n", __func__);
		return false;
	} else {
		ret = iio_read_channel_processed(chip->batt_id_chan, &value);
		if (ret < 0 || value <= 0) {
			chg_err("fail to read batt id adc ret = %d\n", ret);
			return false;
		}
	}

	value = value / UNIT_TRANS_1000;
	if (value >= batt_id_vol[0][0] && value <= batt_id_vol[0][1]) {
		battery_type = BAT_TYPE_LIWINON;
	} else if (value >= batt_id_vol[1][0] && value <= batt_id_vol[1][1]) {
		battery_type = BAT_TYPE_COSMX;
	} else if (value >= batt_id_vol[2][0] && value <= batt_id_vol[2][1]) {
		battery_type = BAT_TYPE_ATL;
	}

	chg_err("battery_id := %d, battery_type:%d\n", value, battery_type);

	if (battery_type > BAT_TYPE_UNKNOWN && battery_type < BAT_TYPE_MAX) {
		return battery_type;
	} else {
		return false;
	}
}
EXPORT_SYMBOL(oplus_battery_type_check_bqfs);

#define TRACK_LOCAL_T_NS_TO_S_THD 1000000000
#define TRACK_UPLOAD_COUNT_MAX 10
#define TRACK_DEVICE_ABNORMAL_UPLOAD_PERIOD (24 * 3600)
static int oplus_bqfs_track_get_local_time_s(void)
{
	int local_time_s;

	local_time_s = local_clock() / TRACK_LOCAL_T_NS_TO_S_THD;
	pr_info("local_time_s:%d\n", local_time_s);

	return local_time_s;
}

int oplus_bqfs_track_upload_upgrade_info(struct chip_bq27541 *chip, char *bsfs_msg)
{
	int index = 0;
	int curr_time;
	static int upload_count = 0;
	static int pre_upload_time = 0;

	mutex_lock(&chip->track_upload_lock);
	curr_time = oplus_bqfs_track_get_local_time_s();
	if (curr_time - pre_upload_time > TRACK_DEVICE_ABNORMAL_UPLOAD_PERIOD)
		upload_count = 0;

	if (upload_count > TRACK_UPLOAD_COUNT_MAX) {
		mutex_unlock(&chip->track_upload_lock);
		return 0;
	}
	chg_err(" bsfs_msg = %s\n", bsfs_msg);

	mutex_lock(&chip->track_bqfs_err_lock);
	if (chip->bqfs_err_uploading) {
		pr_info("bqfs_err_uploading, should return\n");
		mutex_unlock(&chip->track_bqfs_err_lock);
		mutex_unlock(&chip->track_upload_lock);
		return 0;
	}

	if (chip->bqfs_err_load_trigger)
		kfree(chip->bqfs_err_load_trigger);
	chip->bqfs_err_load_trigger = kzalloc(sizeof(oplus_chg_track_trigger), GFP_KERNEL);
	if (!chip->bqfs_err_load_trigger) {
		pr_err("bqfs_err_load_trigger memery alloc fail\n");
		mutex_unlock(&chip->track_bqfs_err_lock);
		mutex_unlock(&chip->track_upload_lock);
		return -ENOMEM;
	}
	chip->bqfs_err_load_trigger->type_reason = TRACK_NOTIFY_TYPE_DEVICE_ABNORMAL;
	chip->bqfs_err_load_trigger->flag_reason = TRACK_NOTIFY_FLAG_GAGUE_ABNORMAL;
	chip->bqfs_err_uploading = true;
	upload_count++;
	pre_upload_time = oplus_bqfs_track_get_local_time_s();
	mutex_unlock(&chip->track_bqfs_err_lock);

	index += snprintf(&(chip->bqfs_err_load_trigger->crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			  "$$bqfs_msg@@%s", bsfs_msg);
	index += snprintf(&(chip->bqfs_err_load_trigger->crux_info[index]), OPLUS_CHG_TRACK_CURX_INFO_LEN - index,
			  "$$err_scene@@%s", OPLUS_CHG_TRACK_SCENE_GAUGE_BQFS_ERR);

	schedule_delayed_work(&chip->bqfs_err_load_trigger_work, 0);
	mutex_unlock(&chip->track_upload_lock);
	chg_err("success\n");

	return 0;
}

static int oplus_bqfs_track_debugfs_init(struct chip_bq27541 *chip)
{
	int ret = 0;
	struct dentry *debugfs_root;
	struct dentry *debugfs_bqfs_ic;

	debugfs_root = oplus_chg_track_get_debugfs_root();
	if (!debugfs_root) {
		ret = -ENOENT;
		return ret;
	}

	debugfs_bqfs_ic = debugfs_create_dir("bqfs_track", debugfs_root);
	if (!debugfs_bqfs_ic) {
		ret = -ENOENT;
		return ret;
	}

	chip->debug_force_bqfs_err = false;
	debugfs_create_u32("debug_force_bqfs_err", 0644, debugfs_bqfs_ic, &(chip->debug_force_bqfs_err));

	return ret;
}

static void oplus_bqfs_track_upgrade_err_load_trigger_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct chip_bq27541 *chip = container_of(dwork, struct chip_bq27541, bqfs_err_load_trigger_work);

	if (!chip->bqfs_err_load_trigger)
		return;

	oplus_chg_track_upload_trigger_data(chip->bqfs_err_load_trigger);

	kfree(chip->bqfs_err_load_trigger);
	chip->bqfs_err_load_trigger = NULL;

	chip->bqfs_err_uploading = false;
}

static void oplus_bqfs_track_update_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct chip_bq27541 *chip = container_of(dwork, struct chip_bq27541, bqfs_track_update_work);

	oplus_bqfs_track_upload_upgrade_info(chip, chip->bqfs_info.track_info);
}

#define BQFS_FORCE_UPGRADE_COUNT_MAX 1
#define BQFS_FORCE_UPGRADE_PERIOD (24 * 3600)
#define BQFS_UTC_BASE_TIME	(1900)
static int oplus_bqfs_get_current_time_s(struct rtc_time *tm)
{
	struct timespec ts;

	getnstimeofday(&ts);

	rtc_time_to_tm(ts.tv_sec, tm);
	tm->tm_year = tm->tm_year + BQFS_UTC_BASE_TIME;
	tm->tm_mon = tm->tm_mon + 1;
	return ts.tv_sec;
}

static bool oplus_bqfs_force_upgrade_counts_check(struct chip_bq27541 *chip)
{
	static int upload_count = 0;
	static int pre_upload_time = 0;
	int curr_time;
	struct rtc_time tm;

	if (!chip)
		return false;

	curr_time = oplus_bqfs_get_current_time_s(&tm);
	if (curr_time - pre_upload_time > BQFS_FORCE_UPGRADE_PERIOD)
		upload_count = 0;

	if (upload_count >= BQFS_FORCE_UPGRADE_COUNT_MAX)
		return false;

	pre_upload_time = curr_time;
	upload_count++;

	return true;
}

#define OPLUS_GAUGE_CP_ABNORMAL_ABS_VBAT (1000)
#define OPLUS_GAUGE_ABNORMAL_RETRY_CNTS (3)
#define OPLUS_GAUGE_ABNORMAL_TBATT_MAX (770)
#define OPLUS_GAUGE_ABNORMAL_TBATT_HIGH (530)
#define OPLUS_GAUGE_ABNORMAL_TBATT_MIN (-400)
#define OPLUS_GAUGE_ABNORMAL_TBATT_LOW (-100)
#define OPLUS_GAUGE_ABNORMAL_TBATT_ABS_MIN (200)
#define OPLUS_GAUGE_ABNORMAL_TBATT_ABS_HIGH (300)
#define OPLUS_GAUGE_ABNORMAL_TBATT_ABS_MAX (400)

enum { BQFS_DATA_ERR_NONE = 0,
       BQFS_DATA_ERR_TBATT,
       BQFS_DATA_ERR_VBAT,
       BQFS_DATA_ERR_FCC,
       BQFS_DATA_ERR_SOH,
       BQFS_DATA_ERR_QMAX,
       BQFS_DATA_ERR_CURR,
       BQFS_DATA_ERR_MAX = 31,
};

bool bqfs_fw_tbatt_check(struct chip_bq27541 *chip)
{
	int ret = 0, retry_cnt = 0;
	int tbatt = 0, inttemp = 0;

	if (atomic_read(&chip->suspended) == 1 || atomic_read(&chip->gauge_i2c_status)) {
		chg_err(" chip->locked return\n");
		return false;
	}

	do {
		ret = gauge_read_i2c(chip, chip->cmd_addr.reg_temp, &tbatt);
		if (ret) {
			chg_err("error reading tbatt, ret:%d\n", ret);
			return false;
		}
		tbatt = tbatt + ZERO_DEGREE_CELSIUS_IN_TENTH_KELVIN;

		ret = gauge_read_i2c(chip, chip->cmd_addr.reg_inttemp, &inttemp);
		if (ret) {
			chg_err("error reading tbatt, ret:%d\n", ret);
			return false;
		}
		inttemp = inttemp + ZERO_DEGREE_CELSIUS_IN_TENTH_KELVIN;
		if ((((tbatt < OPLUS_GAUGE_ABNORMAL_TBATT_MIN) || (tbatt > OPLUS_GAUGE_ABNORMAL_TBATT_MAX)) &&
			abs(tbatt - inttemp) > OPLUS_GAUGE_ABNORMAL_TBATT_ABS_MIN) ||
			(((tbatt >= OPLUS_GAUGE_ABNORMAL_TBATT_MIN) && (tbatt <= OPLUS_GAUGE_ABNORMAL_TBATT_LOW)) &&
			abs(tbatt - inttemp) >= OPLUS_GAUGE_ABNORMAL_TBATT_ABS_MAX) ||
			(((tbatt >= OPLUS_GAUGE_ABNORMAL_TBATT_HIGH) && (tbatt <= OPLUS_GAUGE_ABNORMAL_TBATT_MAX)) &&
			abs(tbatt - inttemp) >= OPLUS_GAUGE_ABNORMAL_TBATT_ABS_HIGH))
			retry_cnt++;
		else
			break;
	} while (retry_cnt < OPLUS_GAUGE_ABNORMAL_RETRY_CNTS);

	if (retry_cnt >= OPLUS_GAUGE_ABNORMAL_RETRY_CNTS) {
		snprintf(chip->bqfs_info.data_err, BQFS_DATA_LEN, "$$tbatt@@%d$$inttemp@@%d", tbatt, inttemp);
		chip->bqfs_info.err_code |= 1 << BQFS_DATA_ERR_TBATT;
		return true;
	} else {
		return false;
	}
}

bool bqfs_fw_vbatt_check(struct chip_bq27541 *chip)
{
	int ret = 0, retry_cnt = 0;
	int vbatt = 0;

	if (atomic_read(&chip->suspended) == 1 || atomic_read(&chip->gauge_i2c_status)) {
		chg_err(" chip->locked return\n");
		return false;
	}

	do {
		ret = gauge_read_i2c(chip, chip->cmd_addr.reg_volt, &vbatt);
		if (ret) {
			chg_err("error reading vbatt, ret:%d\n", ret);
			return false;
		}
		if ((vbatt >= chip->gauge_abnormal_vbatt_min) && (vbatt <= chip->gauge_abnormal_vbatt_max)) {
			break;
		}
		retry_cnt++;
	} while (retry_cnt < OPLUS_GAUGE_ABNORMAL_RETRY_CNTS);

	if (retry_cnt >= OPLUS_GAUGE_ABNORMAL_RETRY_CNTS) {
		snprintf(chip->bqfs_info.data_err, BQFS_DATA_LEN, "$$vbatt@@%d", vbatt);
		chip->bqfs_info.err_code |= 1 << BQFS_DATA_ERR_VBAT;
		return true;
	} else {
		return false;
	}
}

bool bqfs_fw_fcc_check(struct chip_bq27541 *chip)
{
	int ret = 0, retry_cnt = 0;
	int fcc = 0;

	if (atomic_read(&chip->suspended) == 1 || atomic_read(&chip->gauge_i2c_status)) {
		chg_err(" chip->locked return\n");
		return false;
	}

	do {
		ret = gauge_read_i2c(chip, chip->cmd_addr.reg_fcc, &fcc);
		if (ret) {
			chg_err("error reading fcc, ret:%d\n", ret);
			return false;
		}
		if ((fcc >= chip->cp_abnormal_fcc_min) && (fcc <= chip->cp_abnormal_fcc_max)) {
			break;
		}
		retry_cnt++;
	} while (retry_cnt < OPLUS_GAUGE_ABNORMAL_RETRY_CNTS);

	if (retry_cnt >= OPLUS_GAUGE_ABNORMAL_RETRY_CNTS) {
		snprintf(chip->bqfs_info.data_err, BQFS_DATA_LEN, "$$fcc@@%d", fcc);
		chip->bqfs_info.err_code |= 1 << BQFS_DATA_ERR_FCC;
		return true;
	} else {
		return false;
	}
}

bool bqfs_fw_soh_check(struct chip_bq27541 *chip)
{
	int ret = 0, retry_cnt = 0;
	int soh = 0;

	if (atomic_read(&chip->suspended) == 1 || atomic_read(&chip->gauge_i2c_status)) {
		chg_err(" chip->locked return\n");
		return false;
	}

	do {
		ret = gauge_read_i2c(chip, chip->cmd_addr.reg_soh, &soh);
		if (ret) {
			chg_err("error reading soh, ret:%d\n", ret);
			return false;
		}
		if ((soh > chip->cp_abnormal_soh_min) && (soh <= chip->cp_abnormal_soh_max)) {
			break;
		}
		retry_cnt++;
	} while (retry_cnt < OPLUS_GAUGE_ABNORMAL_RETRY_CNTS);

	if (retry_cnt >= OPLUS_GAUGE_ABNORMAL_RETRY_CNTS) {
		snprintf(chip->bqfs_info.data_err, BQFS_DATA_LEN, "$$soh@@%d", soh);
		chip->bqfs_info.err_code |= 1 << BQFS_DATA_ERR_SOH;
		return true;
	} else {
		return false;
	}
}

bool bqfs_fw_qmax_check(struct chip_bq27541 *chip)
{
	int ret = 0, retry_cnt = 0;
	int qmax = 0;

	if (atomic_read(&chip->suspended) == 1 || atomic_read(&chip->gauge_i2c_status)) {
		chg_err(" chip->locked return\n");
		return false;
	}

	do {
		ret = gauge_read_i2c(chip, BQ27426_QMAX_16, &qmax);
		if (ret) {
			chg_err("error reading qmax, ret:%d\n", ret);
			return false;
		}
		if ((qmax >= chip->cp_abnormal_qmax_min) && (qmax <= chip->cp_abnormal_qmax_max)) {
			break;
		}
		retry_cnt++;
	} while (retry_cnt < OPLUS_GAUGE_ABNORMAL_RETRY_CNTS);

	if (retry_cnt >= OPLUS_GAUGE_ABNORMAL_RETRY_CNTS) {
		snprintf(chip->bqfs_info.data_err, BQFS_DATA_LEN, "$$qmax@@%d", qmax);
		chip->bqfs_info.err_code |= 1 << BQFS_DATA_ERR_QMAX;
		return true;
	} else {
		return false;
	}
}

void bqfs_data_check_upgrade(struct chip_bq27541 *chip)
{
	if (chip->bqfs_info.upgrade_ing)
		return;

	memset(chip->bqfs_info.data_err, 0, BQFS_DATA_LEN);
	if (bqfs_fw_tbatt_check(chip) || bqfs_fw_vbatt_check(chip) || bqfs_fw_fcc_check(chip) ||
		bqfs_fw_soh_check(chip) || bqfs_fw_qmax_check(chip)) {
		if (!oplus_bqfs_force_upgrade_counts_check(chip)) {
			return;
		} else {
			chip->bqfs_info.force_upgrade = true;
			bqfs_fw_upgrade(chip, 2);
		}
	}
}

static void oplus_bqfs_data_check_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct chip_bq27541 *chip = container_of(dwork, struct chip_bq27541, bqfs_data_check_work);

	 bqfs_data_check_upgrade(chip);
}

void oplus_bqfs_data_check(struct chip_bq27541 *chip)
{
	schedule_delayed_work(&chip->bqfs_data_check_work, 0);
}

static int oplus_bqfs_track_init(struct chip_bq27541 *chip)
{
	int rc;

	if (!chip)
		return -EINVAL;

	mutex_init(&chip->track_bqfs_err_lock);
	mutex_init(&chip->track_upload_lock);

	chip->bqfs_err_uploading = false;
	chip->bqfs_err_load_trigger = NULL;

	INIT_DELAYED_WORK(&chip->bqfs_err_load_trigger_work, oplus_bqfs_track_upgrade_err_load_trigger_work);
	INIT_DELAYED_WORK(&chip->bqfs_track_update_work, oplus_bqfs_track_update_work);
	INIT_DELAYED_WORK(&chip->bqfs_data_check_work, oplus_bqfs_data_check_work);

	rc = oplus_bqfs_track_debugfs_init(chip);
	if (rc < 0) {
		chg_err("bqfs track debugfs init error, rc=%d\n", rc);
		return rc;
	}

	return rc;
}

static void oplus_bqfs_awake_init(struct chip_bq27541 *chip)
{
	if (!chip) {
		return;
	}
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 9, 0))
	wake_lock_init(&chip->suspend_lock, WAKE_LOCK_SUSPEND, "bqfs suspend wakelock");
#else
	chip->suspend_ws = wakeup_source_register(NULL, "bqfs suspend wakelock");
#endif
}

static void oplus_bqfs_set_awake(struct chip_bq27541 *chip, bool awake)
{
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 9, 0))
	if (awake) {
		wake_lock(&chip->suspend_lock);
	} else {
		wake_unlock(&chip->suspend_lock);
	}
#else
	static bool pm_flag = false;

	if (!chip || !chip->suspend_ws)
		return;

	if (awake && !pm_flag) {
		pm_flag = true;
		__pm_stay_awake(chip->suspend_ws);
	} else if (!awake && pm_flag) {
		__pm_relax(chip->suspend_ws);
		pm_flag = false;
	}
#endif
}

enum { BQFS_FW_CHECK_OK,
       BQFS_FW_UNSEAL_FAIL,
       BQFS_FW_CMD_LEN_ERR,
       BQFS_FW_CMD_UPGRADE_ERR,
       BQFS_FW_UPGRADE_MAX,
};

int bqfs_fw_upgrade(struct chip_bq27541 *chip, int init)
{
#define BQFS_INIT_RETRY_MAX	3
#define BQFS_CMD_X_LEN	2
#define BQFS_CMD_SHITF	8
#define PUSH_DELAY_MS 15000
	unsigned char *p;
	bqfs_cmd_t cmd;
	int i, buflen = 0, len;
	int rc = BQFS_FW_CHECK_OK, rec_cnt = 0, retry_times = 0;
	int read_buf = 0, value_dm = 0, index = 0;

	if (chip->bqfs_info.upgrade_ing || atomic_read(&chip->suspended) == 1 || atomic_read(&chip->gauge_i2c_status)) {
		chg_err(" chip->locked return\n");
		return rc;
	}

	oplus_bqfs_set_awake(chip, true);
	chip->bqfs_info.upgrade_ing = true;
	mutex_lock(&chip->gauge_alt_manufacturer_access);
	bqfs_read_word(chip, BQ27426_REG_FLAGS, &read_buf);
	bqfs_cntl_cmd(chip, BQ27426_SUBCMD_DM_CODE);
	bqfs_read_word(chip, BQ27426_REG_CNTL, &value_dm);
	mutex_unlock(&chip->gauge_alt_manufacturer_access);

	if (!(read_buf & BIT(5)) && (value_dm == chip->bqfs_info.bqfs_dm) &&
		!(read_buf & BIT(4)) && !(chip->bqfs_info.force_upgrade)) {
		chip->bqfs_info.bqfs_status = true;
		goto BQFS_CHECK_END;
	}

	oplus_chg_disable_charge();
	if (bq27426_unseal(chip)) {
		rc = BQFS_FW_UNSEAL_FAIL;
		chg_err("bq27426_unseal fail !\n");
		goto UNSEAL_PROCESS_ERR;
	}

	if (chip->bqfs_info.err_code) {
		mutex_lock(&chip->gauge_alt_manufacturer_access);
		bqfs_cntl_cmd(chip, 0x0041);
		mutex_unlock(&chip->gauge_alt_manufacturer_access);
		usleep_range(250000, 250000);
		chg_err(" BQ27411_RESET_SUBCMD\n");
	}

	mutex_lock(&chip->gauge_alt_manufacturer_access);
BQFS_EXECUTE_CMD_RETRY:
	p = (unsigned char *)chip->bqfs_info.firmware_data;
	buflen = chip->bqfs_info.fw_lenth;
	rec_cnt = 0;
	while (p < chip->bqfs_info.firmware_data + buflen) {
		cmd.cmd_type = *p++;

		if (cmd.cmd_type == CMD_X) {
			len = *p++;
			if (len != BQFS_CMD_X_LEN) {
				rc = BQFS_FW_CMD_LEN_ERR;
				mutex_unlock(&chip->gauge_alt_manufacturer_access);
				goto BQFS_EXECUTE_CMD_ERR;
			}
			cmd.data.delay = *p << BQFS_CMD_SHITF | *(p + 1);
			p += BQFS_CMD_X_LEN;
		} else {
			cmd.addr = *p++;
			cmd.reg  = *p++;
			cmd.data_len = *p++;
			for (i = 0; i < cmd.data_len; i++)
				cmd.data.bytes[i] = *p++;
		}

		rec_cnt++;
		if (!bqfs_fg_fw_update_cmd(chip, &cmd)) {
			retry_times++;
			chg_err("Failed at [%d, %d]\n", rec_cnt, retry_times);
			if (retry_times < BQFS_INIT_RETRY_MAX) {
				goto BQFS_EXECUTE_CMD_RETRY;
			} else {
				rc = BQFS_FW_CMD_UPGRADE_ERR;
				mutex_unlock(&chip->gauge_alt_manufacturer_access);
				goto BQFS_EXECUTE_CMD_ERR;
			}
		}
		mdelay(5);
	}
	chip->bqfs_info.bqfs_status = true;
	chip->bqfs_info.force_upgrade = false;
	mutex_unlock(&chip->gauge_alt_manufacturer_access);
	chg_err("Parameter update Successfully,bqfs_status %d, %s, err = 0x%x\n", chip->bqfs_info.bqfs_status, chip->bqfs_info.data_err, chip->bqfs_info.err_code);
	mdelay(1000);

BQFS_EXECUTE_CMD_ERR:
	if (bq27426_seal(chip))
		chg_err("bq27411 seal fail\n");
UNSEAL_PROCESS_ERR:
	oplus_chg_enable_charge();
	index = snprintf(chip->bqfs_info.track_info, BQFS_INFO_LEN, "$$bqfs_status@@%d$$bqfs_result@@%d$$bqfs_times@@%d"
		"$$value_dm@@0x%x$$bqfs_dm@@0x%x$$bqfs_flag@@0x%x$$bqfs_type@@%d$$bqfs_on@@%d",
		chip->bqfs_info.bqfs_status, rc, retry_times, value_dm, chip->bqfs_info.bqfs_dm, read_buf, chip->bqfs_info.batt_type, init);
	index += snprintf(&(chip->bqfs_info.track_info[index]), BQFS_INFO_LEN - index, "$$data_err@@%s", chip->bqfs_info.data_err);
	schedule_delayed_work(&chip->bqfs_track_update_work, msecs_to_jiffies(PUSH_DELAY_MS));
BQFS_CHECK_END:
	chg_err(" end[%d %d 0x%x %d 0x%x %d %d]\n", chip->bqfs_info.bqfs_status, rc, value_dm, chip->bqfs_info.bqfs_dm, read_buf, chip->bqfs_info.bqfs_ship, init);
	chip->bqfs_info.upgrade_ing = false;
	chip->bqfs_info.err_code = 0;
	oplus_bqfs_set_awake(chip, false);

	return rc;
}

int bqfs_init(struct chip_bq27541 *chip)
{
	struct device_node *node = NULL;
	struct device_node *bqfs_node = NULL;
	const u8 *pBuf;
	int bqfs_unfilt = 0, buflen = 0, batt_id = BAT_TYPE_UNKNOWN, rc = -1;
	char dm_name[128] = {0}, data_name[128] = {0};

	if (!chip ||!chip->dev ||!chip->dev->of_node)
		return -EINVAL;

	node = chip->dev->of_node;

	oplus_bqfs_track_init(chip);
	oplus_bqfs_awake_init(chip);

	bqfs_node = of_find_node_by_name(node, "battery_bqfs_params");
	if (bqfs_node == NULL) {
		chg_err(": Can't find child node \"battery_bqfs_params\"");
		return -EINVAL;
	}
	rc = of_property_read_u32(bqfs_node, "bqfs_unfilt", &bqfs_unfilt);
	if (rc) {
		bqfs_unfilt = BQ27426_BQFS_FILT;
	}
	chip->bqfs_info.bqfs_ship = of_property_read_bool(bqfs_node, "oplus,bqfs_ship");

	rc = oplus_bqfs_get_iio_channel(chip, "batt_id_chan", &chip->batt_id_chan);
	if (rc < 0) {
		chg_err("batt_id_chan get failed, rc = %d\n", rc);
		return rc;
	} else {
		batt_id = oplus_battery_type_check_bqfs(chip);
	}

	if (batt_id <= BAT_TYPE_UNKNOWN || batt_id >= BAT_TYPE_MAX)
		chip->bqfs_info.batt_type = BAT_TYPE_COSMX;
	else
		chip->bqfs_info.batt_type = batt_id;

	sprintf(dm_name, "bqfs_dm_%d", chip->bqfs_info.batt_type);
	sprintf(data_name, "sinofs_bqfs_data_%d", chip->bqfs_info.batt_type);
	rc = of_property_read_u32(bqfs_node, dm_name, &chip->bqfs_info.bqfs_dm);
	if (rc) {
		chip->bqfs_info.bqfs_dm = 0;
	}

	pBuf = of_get_property(bqfs_node, data_name, &buflen);
	if (!pBuf) {
		chg_err(": fw get error\n");
		return -EINVAL;
	}

	chip->bqfs_info.firmware_data = pBuf;
	chip->bqfs_info.fw_lenth = buflen;

	rc = bqfs_fw_upgrade(chip, true);
	if (rc)
		chg_err(": fail, rc = %d\n", rc);
	bqfs_data_check_upgrade(chip);
	bq27426_seal(chip);
	return rc;
}

MODULE_DESCRIPTION("TI FG FW UPDATE Driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("JJ Kong");

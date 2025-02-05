// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024-2026 Oplus. All rights reserved.
 */

#include <linux/module.h>
#include <linux/param.h>
#include <linux/jiffies.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/idr.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/acpi.h>
#include <asm/unaligned.h>
#include <linux/uaccess.h>
#include <linux/interrupt.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/iio/consumer.h>
#include <linux/debugfs.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include "oplus_bq27541.h"
#include "oplus_nfg1000a.h"

int nfg1000a_get_calib_time(struct chip_bq27541 *chip, int *dod_time, int *qmax_time)
{
	int ret;
	int check_args[CALIB_TIME_CHECK_ARGS] = {0};
	int try_count = NFG1000A_SUBCMD_TRY_COUNT;
	int data_check;
	u8 extend_data[22] = {0};
	struct gauge_track_info_reg extend = { NFG1000A_SUBCMD_DA_STATUS1 , 20};
	static bool init_flag = false;

	if (!chip || !dod_time || !qmax_time)
		return -1;

	for (; try_count > 0; try_count--) {
		mutex_lock(&chip->gauge_alt_manufacturer_access);
		ret = gauge_i2c_txsubcmd(chip, NFG1000A_DATAFLASHBLOCK, extend.addr);
		if (ret < 0)
			goto error;
		usleep_range(1000, 1000);
		ret = gauge_read_i2c_block(chip, NFG1000A_DATAFLASHBLOCK, (extend.len + 2), extend_data);
		if (ret < 0)
			goto error;
		data_check = (extend_data[1] << 0x8) | extend_data[0];
		mutex_unlock(&chip->gauge_alt_manufacturer_access);
		if (data_check != extend.addr) {
			pr_info("not match. add=0x%4x, count=%d, extend_data[0]=0x%2x, extend_data[1]=0x%2x\n",
			extend.addr, try_count, extend_data[0], extend_data[1]);
			usleep_range(2000, 2000);
		} else {
			break;
		}
	}
	if (!try_count)
		return -1;

	check_args[0] = (extend_data[21] << 0x08) | extend_data[20];
	check_args[1] = (extend_data[13] << 0x08) | extend_data[12];
	check_args[2] = (extend_data[7] << 0x08) | extend_data[6];
	check_args[3] = (extend_data[15] << 0x08) | extend_data[14];
	check_args[4] = (extend_data[9] << 0x08) | extend_data[8];
	if (!init_flag || check_args[0] != chip->calib_check_args_pre[0])
		chip->dod_time = 0;
	else
		chip->dod_time++;

	if (!init_flag || check_args[2] != chip->calib_check_args_pre[2] ||
		check_args[4] != chip->calib_check_args_pre[4] ||
		check_args[1] != chip->calib_check_args_pre[1] || check_args[3] != chip->calib_check_args_pre[3])
		chip->qmax_time = 0;
	else
		chip->qmax_time++;

	init_flag = true;
	memcpy(chip->calib_check_args_pre, check_args, sizeof(check_args));
	*dod_time = chip->dod_time;
	*qmax_time = chip->qmax_time;
	return ret;

error:
	mutex_unlock(&chip->gauge_alt_manufacturer_access);
	return -1;
}

void nfg1000a_get_info(struct chip_bq27541 *chip, u8 *info, int len)
{
	int i;
	int j;
	int ret;
	int data = 0;
	int index = 0;
	int data_check;
	int try_count = NFG1000A_SUBCMD_TRY_COUNT;
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
		{ NFG1000A_REG_CIS_ALERT_LEVEL , 2 },
	};

	struct gauge_track_info_reg extend[] = {
		{ NFG1000A_SUBCMD_CHEMID, 2 },
		{ NFG1000A_SUBCMD_GAUGE_STATUS  , 3 },
		{ NFG1000A_SUBCMD_DA_STATUS1 , 32 },
		{ NFG1000A_SUBCMD_DA_STATUS2 , 32 },
		{ NFG1000A_SUBCMD_IT_STATUS1 , 18 },
	};


	for (i = 0; i < ARRAY_SIZE(standard); i++) {
		ret = gauge_read_i2c(chip, standard[i].addr, &data);
		if (ret < 0)
			continue;
		index += snprintf(info + index, len - index,
			  "0x%02x=%02x,%02x|", standard[i].addr, (data & 0xff), ((data >> 8) & 0xff));
	}

	for (i = 0; i < ARRAY_SIZE(extend); i++) {
		for (try_count = NFG1000A_SUBCMD_TRY_COUNT; try_count > 0; try_count--) {
			mutex_lock(&chip->gauge_alt_manufacturer_access);
			ret = gauge_i2c_txsubcmd(chip, NFG1000A_DATAFLASHBLOCK, extend[i].addr);
			if (ret < 0) {
				mutex_unlock(&chip->gauge_alt_manufacturer_access);
				break;
			}

			if (sizeof(extend_data) >= extend[i].len + 2) {
				usleep_range(1000, 1000);
				ret = gauge_read_i2c_block(chip, NFG1000A_DATAFLASHBLOCK, (extend[i].len + 2), extend_data);
				mutex_unlock(&chip->gauge_alt_manufacturer_access);
				if (ret < 0)
					break;
				data_check = (extend_data[1] << 0x8) | extend_data[0];
				if (data_check != extend[i].addr) {
					pr_info("0x%4x not match. try_count=%d, extend_data[0]=0x%2x, extend_data[1]=0x%2x\n",
						extend[i].addr, try_count, extend_data[0], extend_data[1]);
					usleep_range(2000, 2000);
					continue;
				}
				index += snprintf(info + index, len - index, "0x%04x=", extend[i].addr);
				for (j = 0; j < extend[i].len - 1; j++)
					index += snprintf(info + index, len - index, "%02x,", extend_data[j + 2]);
				index += snprintf(info + index, len - index, "%02x", extend_data[j + 2]);
				if (i < ARRAY_SIZE(extend) - 1)
					usleep_range(2000, 2000);
			} else {
				mutex_unlock(&chip->gauge_alt_manufacturer_access);
			}
			if (i  <  ARRAY_SIZE(extend) - 1)
				index += snprintf(info + index, len - index, "|");
			break;
		}
	}
}

int nfg1000a_get_qmax_parameters(struct chip_bq27541 *chip, int *cell_qmax)
{
	int ret;
	int data_check;
	int try_count = NFG1000A_SUBCMD_TRY_COUNT;
	u8 extend_data[12] = {0};
	struct gauge_track_info_reg extend = { NFG1000A_SUBCMD_DA_STATUS1 , 10 };

	if (!chip || !cell_qmax)
		return -1;

	for (; try_count > 0; try_count--) {
		mutex_lock(&chip->gauge_alt_manufacturer_access);
		ret = gauge_i2c_txsubcmd(chip, NFG1000A_DATAFLASHBLOCK, extend.addr);
		if (ret < 0)
			goto error;
		usleep_range(1000, 1000);
		ret = gauge_read_i2c_block(chip, NFG1000A_DATAFLASHBLOCK, (extend.len + 2), extend_data);
		if (ret < 0)
			goto error;
		data_check = (extend_data[1] << 0x8) | extend_data[0];
		mutex_unlock(&chip->gauge_alt_manufacturer_access);
		if (data_check != extend.addr) {
			pr_info("not match. add=0x%4x, count=%d, extend_data[0]=0x%2x, extend_data[1]=0x%2x\n",
			extend.addr, try_count, extend_data[0], extend_data[1]);
			usleep_range(2000, 2000);
		} else {
			break;
		}
	}
	if (!try_count)
		return -1;
	*cell_qmax = (extend_data[11] << 0x08) | extend_data[10];
	pr_info("nfg1000a_get_qmax_parameters cell_qmax:%d\n", *cell_qmax);
	return 0;
error:
	mutex_unlock(&chip->gauge_alt_manufacturer_access);
	return ret;
}

int nfg1000a_get_rsoc_parameters(struct chip_bq27541 *chip, int *rsoc)
{
	int ret;
	int data_check;
	int try_count = NFG1000A_SUBCMD_TRY_COUNT;
	u8 extend_data[6] = {0};
	struct gauge_track_info_reg extend = { NFG1000A_SUBCMD_DA_STATUS1 , 4 };

	if (!chip || !rsoc)
		return -1;

	for (; try_count > 0; try_count--) {
		mutex_lock(&chip->gauge_alt_manufacturer_access);
		ret = gauge_i2c_txsubcmd(chip, NFG1000A_DATAFLASHBLOCK, extend.addr);
		if (ret < 0)
			goto error;
		usleep_range(1000, 1000);
		ret = gauge_read_i2c_block(chip, NFG1000A_DATAFLASHBLOCK, (extend.len + 2), extend_data);
		if (ret < 0)
			goto error;
		data_check = (extend_data[1] << 0x8) | extend_data[0];
		mutex_unlock(&chip->gauge_alt_manufacturer_access);
		if (data_check != extend.addr) {
			pr_info("not match. add=0x%4x, count=%d, extend_data[0]=0x%2x, extend_data[1]=0x%2x\n",
			extend.addr, try_count, extend_data[0], extend_data[1]);
			usleep_range(2000, 2000);
		} else {
			break;
		}
	}

	if (!try_count)
		return -1;
	*rsoc = (extend_data[5] << 0x08) | extend_data[4];
	pr_info("nfg1000a_get_rsoc_parameters cell_soc:%d\n", *rsoc);
	return 0;
error:
	mutex_unlock(&chip->gauge_alt_manufacturer_access);
	return ret;
}

bool nfg1000a_sha256_hmac_authenticate(struct chip_bq27541 *chip)
{
	int i;
	int ret;
	int count = 0;
	u8 checksum = 0;
	u8 data_buf[2] = {0};
	u8 *p_temp = chip->sha256_authenticate_data->random;
	int len = ARRAY_SIZE(chip->sha256_authenticate_data->random);
	int half_len = len / 2;

	mutex_lock(&chip->gauge_alt_manufacturer_access);
	for (i = 0; i < len; i++)
		checksum = checksum + chip->sha256_authenticate_data->random[i];
	checksum = 0xff - (checksum & 0xff);

	pr_info("%s start, len=%d, half_len:%d\n", __func__, len, half_len);
	ret = gauge_write_i2c_block(chip, NFG1000A_DATAFLASHBLOCK, sizeof(data_buf), data_buf);
	if (ret < 0)
		goto error;

	ret = gauge_write_i2c_block(chip, NFG1000A_AUTHENDATA_1ST, half_len, p_temp);
	if (ret < 0)
		goto error;

	ret = gauge_write_i2c_block(chip, NFG1000A_AUTHENDATA_2ND, half_len, p_temp + half_len);
	if (ret < 0)
		goto error;

	ret = gauge_i2c_txsubcmd_onebyte(chip, NFG1000A_AUTHENCHECKSUM, checksum);
	if (ret < 0)
		goto error;

	ret = gauge_i2c_txsubcmd_onebyte(chip, NFG1000A_AUTHENLEN, 0x24);
	if (ret < 0)
		goto error;

	p_temp = chip->sha256_authenticate_data->gauge_encode;

	for (; count< NFG1000A_I2C_TRY_COUNT; count ++) {
		msleep(20);
		ret = gauge_read_i2c_block(chip, NFG1000A_AUTHENDATA_1ST, half_len, p_temp);
		if (ret < 0) {
			pr_info("ret=%d, count=%d\n", ret, count);
			msleep(20);
		} else {
			break;
		}
	}

	ret = gauge_read_i2c_block(chip, NFG1000A_AUTHENDATA_2ND, half_len, p_temp + half_len);
	if (ret < 0)
		goto error;

	mutex_unlock(&chip->gauge_alt_manufacturer_access);
	return true;

error:
	mutex_unlock(&chip->gauge_alt_manufacturer_access);
	return false;
}

void nfg1000a_bcc_set_buffer(struct chip_bq27541 *chip, int *buffer)
{
	if (!chip || !buffer) {
		pr_err("chip or buffer is null\n");
		return;
	}

	if (SINGLE_CELL == buffer[18]) {
		nfg1000a_get_qmax_parameters(chip, &buffer[3]);
		buffer[4]= buffer[3];
		buffer[5] = 0;
		nfg1000a_get_rsoc_parameters(chip, &buffer[0]);
		buffer[1] = buffer[0];
		buffer[12] = buffer[0];
		buffer[13] = buffer[0];
		buffer[2] = 0;
	} else {
		buffer[2] = 0;
		buffer[5] = 0;
		nfg1000a_get_rsoc_parameters(chip, &buffer[0]);
		buffer[12] = buffer[0];
		nfg1000a_get_qmax_parameters(chip, &buffer[3]);
	}
}

void nfg1000a_sub_bcc_set_buffer(struct chip_bq27541 *chip, int *buffer)
{
	int sub_batt_qmax_2 = 0;
	int sub_batt_dod0_2 = 0;

	if (!chip || !buffer) {
		pr_err("chip or buffer is null\n");
		return;
	}

	nfg1000a_get_qmax_parameters(chip, &sub_batt_qmax_2);
	buffer[4] = sub_batt_qmax_2;
	nfg1000a_get_rsoc_parameters(chip, &sub_batt_dod0_2);
	buffer[1] = sub_batt_dod0_2;
	buffer[13] = sub_batt_dod0_2;
}

/*
 * Copyright (c) 2021 ZEKU Technology(Shanghai) Corp.,Ltd.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * Basecode Created :        2021/5/27 Author: wangyingju@zeku.com
 *
 */

#include <media/v4l2-subdev.h>
#include "include/aon_sensor_io.h"
#include "include/aon_sensor_common.h"
#include "include/aon_sensor_core.h"

static s32 aon_sensor_cci_i2c_util(struct cam_sensor_cci_client *cci_client,
		u16 cci_cmd)
{
	s32 rc = 0;
	struct cam_cci_ctrl cci_ctrl = {0};

	if (!cci_client)
		return -EINVAL;

	cci_ctrl.cmd = cci_cmd;
	cci_ctrl.cci_info = cci_client;
	rc = v4l2_subdev_call(cci_client->cci_subdev,
		core, ioctl, VIDIOC_MSM_CCI_CFG, &cci_ctrl);
	if (rc < 0) {
		pr_err("%s Failed rc = %d", __func__, rc);
		return rc;
	}
	return cci_ctrl.status;
}

static s32 aon_cci_i2c_read(struct cam_sensor_cci_client *cci_client,
	u32 addr, u32 *data,
	enum camera_sensor_i2c_type addr_type,
	enum camera_sensor_i2c_type data_type)
{
	s32 rc = -EINVAL;
	unsigned char buf[CAMERA_SENSOR_I2C_TYPE_DWORD] = {0};
	struct cam_cci_ctrl cci_ctrl = {0};

	if (!cci_client || !data
		|| addr_type <= CAMERA_SENSOR_I2C_TYPE_INVALID
		|| addr_type >= CAMERA_SENSOR_I2C_TYPE_MAX
		|| data_type <= CAMERA_SENSOR_I2C_TYPE_INVALID
		|| data_type >= CAMERA_SENSOR_I2C_TYPE_MAX)
		return rc;

	cci_ctrl.cmd = MSM_CCI_I2C_READ;
	cci_ctrl.cci_info = cci_client;
	cci_ctrl.cfg.cci_i2c_read_cfg.addr = addr;
	cci_ctrl.cfg.cci_i2c_read_cfg.addr_type = addr_type;
	cci_ctrl.cfg.cci_i2c_read_cfg.data_type = data_type;
	cci_ctrl.cfg.cci_i2c_read_cfg.data = buf;
	cci_ctrl.cfg.cci_i2c_read_cfg.num_byte = data_type;
	rc = v4l2_subdev_call(cci_client->cci_subdev,
		core, ioctl, VIDIOC_MSM_CCI_CFG, &cci_ctrl);
	if (rc < 0) {
		pr_err("%s rc = %d", __func__, rc);
		return rc;
	}

	rc = cci_ctrl.status;
	if (data_type == CAMERA_SENSOR_I2C_TYPE_BYTE)
		*data = buf[0];
	else if (data_type == CAMERA_SENSOR_I2C_TYPE_WORD)
		*data = buf[0] << 8 | buf[1];
	else if (data_type == CAMERA_SENSOR_I2C_TYPE_3B)
		*data = buf[0] << 16 | buf[1] << 8 | buf[2];
	else
		*data = buf[0] << 24 | buf[1] << 16 |
			buf[2] << 8 | buf[3];

	return rc;
}

static s32 aon_cci_i2c_write_table_cmd(
	struct cam_sensor_cci_client *client,
	struct cam_sensor_i2c_reg_setting *write_setting,
	enum cam_cci_cmd_type cmd)
{
	s32 rc = -EINVAL;
	struct cam_cci_ctrl cci_ctrl = {0};

	if (!client || !write_setting)
		return rc;

	if (write_setting->addr_type <= CAMERA_SENSOR_I2C_TYPE_INVALID
		|| write_setting->addr_type >= CAMERA_SENSOR_I2C_TYPE_MAX
		|| write_setting->data_type <= CAMERA_SENSOR_I2C_TYPE_INVALID
		|| write_setting->data_type >= CAMERA_SENSOR_I2C_TYPE_MAX)
		return rc;

	cci_ctrl.cmd = cmd;
	cci_ctrl.cci_info = client;
	cci_ctrl.cfg.cci_i2c_write_cfg.reg_setting =
		write_setting->reg_setting;
	cci_ctrl.cfg.cci_i2c_write_cfg.data_type = write_setting->data_type;
	cci_ctrl.cfg.cci_i2c_write_cfg.addr_type = write_setting->addr_type;
	cci_ctrl.cfg.cci_i2c_write_cfg.size = write_setting->size;
	rc = v4l2_subdev_call(client->cci_subdev,
		core, ioctl, VIDIOC_MSM_CCI_CFG, &cci_ctrl);
	pr_info("%s v4l2_subdev_call rc=%d", __func__, rc);
	if (rc < 0) {
		pr_err("%s Failed rc = %d", __func__, rc);
		return rc;
	}
	rc = cci_ctrl.status;
	if (write_setting->delay > 20)
		msleep(write_setting->delay);
	else if (write_setting->delay)
		usleep_range(write_setting->delay * 1000, (write_setting->delay
			* 1000) + 1000);

	return rc;
}

static s32 aon_cci_i2c_write_table(
	struct cam_sensor_cci_client *client,
	struct cam_sensor_i2c_reg_setting *write_setting)
{
	return aon_cci_i2c_write_table_cmd(client, write_setting,
		MSM_CCI_I2C_WRITE);
}

static s32 aon_cci_i2c_write_continuous_table(
	struct cam_sensor_cci_client *client,
	struct cam_sensor_i2c_reg_setting *write_setting,
	u8 cam_sensor_i2c_write_flag)
{
	s32 rc = 0;

	if (cam_sensor_i2c_write_flag == 1)
		rc = aon_cci_i2c_write_table_cmd(client, write_setting,
			MSM_CCI_I2C_WRITE_BURST);
	else if (cam_sensor_i2c_write_flag == 0)
		rc = aon_cci_i2c_write_table_cmd(client, write_setting,
			MSM_CCI_I2C_WRITE_SEQ);

	return rc;
}

static s32 aon_cci_i2c_compare(struct cam_sensor_cci_client *client,
	u32 addr, u16 data, u16 data_mask,
	enum camera_sensor_i2c_type data_type,
	enum camera_sensor_i2c_type addr_type)
{
	s32 rc = 0;
	u32 reg_data = 0;

	rc = aon_cci_i2c_read(client, addr, &reg_data,
		addr_type, data_type);
	if (rc < 0)
		return rc;

	reg_data = reg_data & 0xFFFF;
	if (data == (reg_data & ~data_mask))
		return I2C_COMPARE_MATCH;
	return I2C_COMPARE_MISMATCH;
}

static s32 aon_cci_i2c_poll(struct cam_sensor_cci_client *client,
	u32 addr, u16 data, u16 data_mask,
	enum camera_sensor_i2c_type data_type,
	enum camera_sensor_i2c_type addr_type,
	u32 delay_ms)
{
	s32 rc = -EINVAL;
	s32 i = 0;

	if (!client)
		return rc;

	pr_info("%s addr: 0x%x data: 0x%x dt: %d", __func__,
		addr, data, data_type);

	if (delay_ms > MAX_POLL_DELAY_MS) {
		pr_err("%s invalid delay = %d max_delay = %d", __func__,
			delay_ms, MAX_POLL_DELAY_MS);
		return -EINVAL;
	}
	for (i = 0; i < delay_ms; i++) {
		rc = aon_cci_i2c_compare(client,
			addr, data, data_mask, data_type, addr_type);
		if (!rc)
			return rc;

		usleep_range(1000, 1010);
	}

	/* If rc is 1 then read is successful but poll is failure */
	if (rc == 1)
		pr_err("%s poll failed rc=%d(non-fatal)", __func__, rc);

	if (rc < 0)
		pr_err("%s poll failed rc=%d", __func__, rc);

	return rc;
}


s32 aon_io_dev_poll(struct cam_sensor_cci_client *client,
	u32 addr, u16 data, u32 data_mask,
	enum camera_sensor_i2c_type addr_type,
	enum camera_sensor_i2c_type data_type,
	u32 delay_ms)
{
	s16 mask = data_mask & 0xFF;

	if (!client) {
		pr_err("%s Invalid Args", __func__);
		return -EINVAL;
	}

	return aon_cci_i2c_poll(client,
		addr, data, mask, data_type, addr_type, delay_ms);
}

s32 aon_io_dev_read(struct cam_sensor_cci_client *client,
	u32 addr, u32 *data,
	enum camera_sensor_i2c_type addr_type,
	enum camera_sensor_i2c_type data_type)
{
	if (!client) {
		pr_err("%s Invalid Args", __func__);
		return -EINVAL;
	}

	return aon_cci_i2c_read(client,
		addr, data, addr_type, data_type);
}

s32 aon_io_dev_write(struct cam_sensor_cci_client *client,
	struct cam_sensor_i2c_reg_setting *write_setting)
{
	if (!write_setting || !client) {
		pr_err("%s Input parameters not valid ws: %pK ioinfo: %pK", __func__,
			write_setting, client);
		return -EINVAL;
	}

	if (!write_setting->reg_setting) {
		pr_err("%s Invalid Register Settings", __func__);
		return -EINVAL;
	}

	return aon_cci_i2c_write_table(client, write_setting);
}

s32 aon_io_dev_write_continuous(struct cam_sensor_cci_client *client,
	struct cam_sensor_i2c_reg_setting *write_setting,
	u8 cam_sensor_i2c_write_flag)
{
	if (!write_setting || !client) {
		pr_err("%s Input parameters not valid ws: %pK ioinfo: %pK", __func__,
			write_setting, client);
		return -EINVAL;
	}

	if (!write_setting->reg_setting) {
		pr_err("%s Invalid Register Settings", __func__);
		return -EINVAL;
	}

	return aon_cci_i2c_write_continuous_table(client,
		write_setting, cam_sensor_i2c_write_flag);
}

s32 aon_io_init(struct cam_sensor_cci_client *client)
{
	if (!client) {
		pr_err("%s Invalid Args", __func__);
		return -EINVAL;
	}

	pr_info("%s client->cci_device: %d",
		__func__, client->cci_device);

	client->cci_subdev = cam_cci_get_subdev(client->cci_device);
	pr_info("%s cci_subdev: %p", __func__, client->cci_subdev);

	return aon_sensor_cci_i2c_util(client, MSM_CCI_INIT);
}

s32 aon_io_release(struct cam_sensor_cci_client *client)
{
	if (!client) {
		pr_err("%s Invalid Args", __func__);
		return -EINVAL;
	}

	return aon_sensor_cci_i2c_util(client, MSM_CCI_RELEASE);
}

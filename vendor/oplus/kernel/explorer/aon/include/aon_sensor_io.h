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

#ifndef _AON_SENSOR_IO_H_
#define _AON_SENSOR_IO_H_

#include <linux/videodev2.h>
#include "aon_sensor_common.h"

#define CCI_MASTER 1
#define I2C_MASTER 2
#define SPI_MASTER 3

#define MAX_POLL_DELAY_MS 100
#define I2C_COMPARE_MATCH 0
#define I2C_COMPARE_MISMATCH 1

enum cam_cci_cmd_type {
	MSM_CCI_INIT,
	MSM_CCI_RELEASE,
	MSM_CCI_SET_SID,
	MSM_CCI_SET_FREQ,
	MSM_CCI_SET_SYNC_CID,
	MSM_CCI_I2C_READ,
	MSM_CCI_I2C_WRITE,
	MSM_CCI_I2C_WRITE_SEQ,
	MSM_CCI_I2C_WRITE_BURST,
	MSM_CCI_I2C_WRITE_ASYNC,
	MSM_CCI_GPIO_WRITE,
	MSM_CCI_I2C_WRITE_SYNC,
	MSM_CCI_I2C_WRITE_SYNC_BLOCK,
};

struct cam_cci_wait_sync_cfg {
	u16 cid;
	s16 csid;
	u16 line;
	u16 delay;
};

struct cam_cci_gpio_cfg {
	u16 gpio_queue;
	u16 i2c_queue;
};

struct cam_cci_read_cfg {
	u32 addr;
	u16 addr_type;
	u8  *data;
	u16 num_byte;
	u16 data_type;
};

struct cam_sensor_cci_client {
	struct v4l2_subdev    *cci_subdev;
	u32                   freq;
	enum i2c_freq_mode    i2c_freq_mode;
	enum cci_i2c_master_t cci_i2c_master;
	u16                   sid;
	u16                   cid;
	u32                   timeout;
	u16                   retries;
	u16                   id_map;
	u16                   cci_device;
};

struct cam_cci_ctrl {
	s32                                   status;
	struct cam_sensor_cci_client          *cci_info;
	enum cam_cci_cmd_type                 cmd;
	union {
		struct cam_sensor_i2c_reg_setting cci_i2c_write_cfg;
		struct cam_cci_read_cfg           cci_i2c_read_cfg;
		struct cam_cci_wait_sync_cfg      cci_wait_sync_cfg;
		struct cam_cci_gpio_cfg           gpio_cfg;
	} cfg;
};

extern struct v4l2_subdev *cam_cci_get_subdev(int cci_dev_index);

#define VIDIOC_MSM_CCI_CFG \
	_IOWR('V', BASE_VIDIOC_PRIVATE + 23, struct cam_cci_ctrl)

s32 aon_io_dev_read(struct cam_sensor_cci_client *client,
	u32 addr, u32 *data,
	enum camera_sensor_i2c_type addr_type,
	enum camera_sensor_i2c_type data_type);

s32 aon_io_init(struct cam_sensor_cci_client *client);

s32 aon_io_release(struct cam_sensor_cci_client *client);

s32 aon_io_dev_write(struct cam_sensor_cci_client *client,
	struct cam_sensor_i2c_reg_setting *write_setting);

s32 aon_io_dev_write_continuous(struct cam_sensor_cci_client *client,
	struct cam_sensor_i2c_reg_setting *write_setting,
	u8 cam_sensor_i2c_write_flag);

s32 aon_io_dev_poll(struct cam_sensor_cci_client *client,
	u32 addr, u16 data, u32 data_mask,
	enum camera_sensor_i2c_type addr_type,
	enum camera_sensor_i2c_type data_type,
	u32 delay_ms);

#endif /* _AON_SENSOR_IO_H_ */

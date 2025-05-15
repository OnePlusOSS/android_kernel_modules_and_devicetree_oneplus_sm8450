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

#ifndef _AON_SENSOR_COMMON_H_
#define _AON_SENSOR_COMMON_H_

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/list.h>

#define MAX_REGULATOR 5
#define MAX_POWER_CONFIG 12
#define MAX_PER_FRAME_ARRAY 32

enum aon_sensor_packet_opcodes {
	AON_SENSOR_PACKET_OPCODE_STREAMON,
	AON_SENSOR_PACKET_OPCODE_UPDATE,
	AON_SENSOR_PACKET_OPCODE_INITIAL_CONFIG,
	AON_SENSOR_PACKET_OPCODE_PROBE,
	AON_SENSOR_PACKET_OPCODE_CONFIG,
	AON_SENSOR_PACKET_OPCODE_STREAMOFF,
	AON_SENSOR_PACKET_OPCODE_GETMEM,
	AON_SENSOR_PACKET_OPCODE_OPMCLK,
	AON_SENSOR_PACKET_OPCODE_NOP = 127
};

enum camera_sensor_i2c_type {
	CAMERA_SENSOR_I2C_TYPE_INVALID,
	CAMERA_SENSOR_I2C_TYPE_BYTE,
	CAMERA_SENSOR_I2C_TYPE_WORD,
	CAMERA_SENSOR_I2C_TYPE_3B,
	CAMERA_SENSOR_I2C_TYPE_DWORD,
	CAMERA_SENSOR_I2C_TYPE_MAX,
};

enum i2c_freq_mode {
	I2C_STANDARD_MODE,
	I2C_FAST_MODE,
	I2C_CUSTOM_MODE,
	I2C_FAST_PLUS_MODE,
	I2C_MAX_MODES,
};

enum sensor_sub_module {
	SUB_MODULE_SENSOR,
	SUB_MODULE_ACTUATOR,
	SUB_MODULE_EEPROM,
	SUB_MODULE_LED_FLASH,
	SUB_MODULE_CSID,
	SUB_MODULE_CSIPHY,
	SUB_MODULE_OIS,
	SUB_MODULE_EXT,
	SUB_MODULE_MAX,
};

enum msm_camera_power_seq_type {
	SENSOR_MCLK,
	SENSOR_VANA,
	SENSOR_VDIG,
	SENSOR_VIO,
	SENSOR_VAF,
	SENSOR_VAF_PWDM,
	SENSOR_CUSTOM_REG1,
	SENSOR_CUSTOM_REG2,
	SENSOR_RESET,
	SENSOR_STANDBY,
	SENSOR_CUSTOM_GPIO1,
	SENSOR_CUSTOM_GPIO2,
	SENSOR_VANA1,
	SENSOR_SEQ_TYPE_MAX,
};

enum cci_i2c_master_t {
	MASTER_0,
	MASTER_1,
	MASTER_MAX,
};

enum cci_device_num {
	CCI_DEVICE_0,
	CCI_DEVICE_1,
	CCI_DEVICE_MAX,
};

enum msm_sensor_camera_id_t {
	CAMERA_0,
	CAMERA_1,
	CAMERA_2,
	CAMERA_3,
	CAMERA_4,
	CAMERA_5,
	CAMERA_6,
	CAMERA_7,
	CAMERA_8,
	CAMERA_9,
	MAX_CAMERAS,
};

enum msm_camera_vreg_name_t {
	CAM_VDIG,
	CAM_VIO,
	CAM_VANA,
	CAM_VAF,
	CAM_V_CUSTOM1,
	CAM_V_CUSTOM2,
	CAM_VREG_MAX,
};

enum cam_sensor_i2c_cmd_type {
	CAM_SENSOR_I2C_WRITE_RANDOM,
	CAM_SENSOR_I2C_WRITE_BURST,
	CAM_SENSOR_I2C_WRITE_SEQ,
	CAM_SENSOR_I2C_READ_RANDOM,
	CAM_SENSOR_I2C_READ_SEQ,
	CAM_SENSOR_I2C_POLL
};

struct cam_sensor_i2c_reg_array {
	u32 reg_addr;
	u32 reg_data;
	u32 delay;
	u32 data_mask;
};

struct cam_sensor_i2c_reg_setting {
	struct cam_sensor_i2c_reg_array *reg_setting;
	u32                             size;
	enum                            camera_sensor_i2c_type addr_type;
	enum                            camera_sensor_i2c_type data_type;
	unsigned short                  delay;
	u8                              *read_buff;
	u32                             read_buff_len;
};

struct cam_sensor_i2c_seq_reg {
	u32                         reg_addr;
	u8                          *reg_data;
	u32                         size;
	enum camera_sensor_i2c_type addr_type;
};

struct i2c_settings_list {
	struct cam_sensor_i2c_reg_setting i2c_settings;
	struct cam_sensor_i2c_seq_reg     seq_settings;
	enum cam_sensor_i2c_cmd_type      op_code;
	struct list_head                  list;
};

struct i2c_settings_array {
	struct list_head list_head;
	s32              is_settings_valid;
};

struct i2c_data_settings {
	struct i2c_settings_array init_settings;
	struct i2c_settings_array config_settings;
	struct i2c_settings_array streamon_settings;
	struct i2c_settings_array streamoff_settings;
};

struct aon_sensor_power_setting {
	enum msm_camera_power_seq_type seq_type;
	unsigned short                 seq_val;
	long                           config_val;
	unsigned short                 delay;
	void                           *data[10];
};

struct aon_camera_gpio_num_info {
	u16 gpio_num[SENSOR_SEQ_TYPE_MAX];
	u8  valid[SENSOR_SEQ_TYPE_MAX];
};

struct aon_pinctrl_info {
	struct pinctrl       *pinctrl;
	struct pinctrl_state *gpio_state_active;
	struct pinctrl_state *gpio_state_suspend;
	bool                 use_pinctrl;
};

struct aon_sensor_power_ctrl_t {
	struct device                     *dev;
	struct aon_sensor_power_setting   *power_setting;
	u16                               power_setting_size;
	struct aon_sensor_power_setting   *power_down_setting;
	u16                               power_down_setting_size;
	struct aon_camera_gpio_num_info   *gpio_num_info;
	struct aon_pinctrl_info           pinctrl_info;
	u8                                cam_pinctrl_status;
};

struct aon_camera_slave_info {
	u16 sensor_slave_addr;
	u16 sensor_id_reg_addr;
	u16 sensor_id;
	u16 sensor_id_mask;
#ifdef OPLUS_FEATURE_CAMERA_COMMON
	u8  addr_type;
	u8  data_type;
#endif
};

struct aon_sensor_board_info {
	struct aon_camera_slave_info   slave_info;
	s32                            sensor_mount_angle;
	s32                            secure_mode;
	int                            modes_supported;
	s32                            pos_roll;
	s32                            pos_yaw;
	s32                            pos_pitch;
	s32                            subdev_id[SUB_MODULE_MAX];
	s32                            subdev_intf[SUB_MODULE_MAX];
	const                          char *misc_regulator;
	struct aon_sensor_power_ctrl_t power_info;
};

#endif /* _AON_SENSOR_COMMON_H_ */

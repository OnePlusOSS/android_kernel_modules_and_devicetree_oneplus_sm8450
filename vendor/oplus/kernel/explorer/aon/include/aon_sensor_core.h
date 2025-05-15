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

#ifndef _AON_SENSOR_CORE_H_
#define _AON_SENSOR_CORE_H_

#include "aon_soc_util.h"
#include "aon_sensor_io.h"
#include "aon_sensor_common.h"

enum aon_sensor_state_t {
	AON_SENSOR_INIT,
	AON_SENSOR_ACQUIRE,
	AON_SENSOR_CONFIG,
	AON_SENSOR_START,
};

struct aon_sensor_ctrl_t {
	char                         device_name[20];
	struct platform_device       *pdev;
	struct aon_hw_soc_info       soc_info;
	struct mutex                 aon_sensor_mutex;
	struct aon_sensor_board_info *sensordata;
	enum cci_i2c_master_t        cci_i2c_master;
	enum cci_device_num          cci_num;
	struct cam_sensor_cci_client *client;
	enum aon_sensor_state_t      sensor_state;
	u8                           is_probe_succeed;
	u8                           is_power_on;
	u32                          id;
	struct device_node           *of_node;
	struct i2c_data_settings     i2c_data;
	u32                          streamon_count;
	u32                          streamoff_count;
	int                          bob_reg_index;
	bool                         bob_pwm_switch;
};

int explorer_aon_drv_cmd(void **ctrl, void *arg);
int explorer_aon_init(struct aon_sensor_ctrl_t **s_ctrl);
void explorer_aon_exit(struct aon_sensor_ctrl_t **s_ctrl);

int aon_sensor_core_power_up(struct aon_sensor_power_ctrl_t *ctrl,
		struct aon_hw_soc_info *soc_info);
int aon_sensor_util_power_down(struct aon_sensor_power_ctrl_t *ctrl,
		struct aon_hw_soc_info *soc_info);
int aon_sensor_parse_dt(struct aon_sensor_ctrl_t *s_ctrl);
s32 aon_enable_mclk(struct aon_sensor_power_ctrl_t *ctrl,
		struct aon_hw_soc_info *soc_info);
s32 aon_disable_mclk(struct aon_sensor_power_ctrl_t *ctrl,
		struct aon_hw_soc_info *soc_info);
void aon_dinit_variables(void);


#endif /* _AON_SENSOR_DEV_H_ */

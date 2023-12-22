/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2017-2019, The Linux Foundation. All rights reserved.
 */


#ifndef _OPLUS_CAM_ACTUATOR_DEV_H_
#define _OPLUS_CAM_ACTUATOR_DEV_H_

#include "cam_actuator_dev.h"
#include "oplus_cam_sensor_core.h"

#define AK7316_STATE_ADDR 0x02
#define AK7316_STANDBY_STATE 0x40
#define AK7316_WRITE_CONTROL_ADDR 0xAE
#define AK7316_STORE_ADDR 0x03
#define AK7316_PID_LENGTH 44

void oplus_cam_actuator_sds_enable(struct cam_actuator_ctrl_t *a_ctrl);
int32_t oplus_cam_actuator_lock(struct cam_actuator_ctrl_t *a_ctrl);
int32_t oplus_cam_actuator_unlock(struct cam_actuator_ctrl_t *a_ctrl);

int32_t oplus_cam_actuator_power_down_sds(struct cam_actuator_ctrl_t *a_ctrl);

int32_t oplus_cam_actuator_power_up_sds(struct cam_actuator_ctrl_t *a_ctrl);

int32_t oplus_cam_actuator_construct_default_power_setting_sds(struct cam_sensor_power_ctrl_t *power_info);
/*
//int oplus_cam_actuator_ram_read_sds(struct cam_actuator_ctrl_t *a_ctrl, uint32_t addr, uint32_t* data);
//int oplus_cam_actuator_ram_write_sds(struct cam_actuator_ctrl_t *a_ctrl, uint32_t addr, uint32_t data);
int oplus_cam_actuator_update_pid_sds(void *arg);
*/
#endif /* _CAM_ACTUATOR_CORE_H_ */


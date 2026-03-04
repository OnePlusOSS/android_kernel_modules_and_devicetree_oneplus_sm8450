/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2017-2018, The Linux Foundation. All rights reserved.
 */

#ifndef _OPLUS_CAM_ACTUATOR_CORE_H_
#define _OPLUS_CAM_ACTUATOR_CORE_H_

#include "cam_actuator_dev.h"

void cam_actuator_poll_setting_update(struct cam_actuator_ctrl_t *a_ctrl);
void cam_actuator_poll_setting_apply(struct cam_actuator_ctrl_t *a_ctrl);
int32_t cam_actuator_construct_default_power_setting_oem(struct cam_sensor_power_ctrl_t *power_info);
int32_t oplus_cam_actuator_driver_cmd(struct cam_actuator_ctrl_t *a_ctrl, void *arg);

void oplus_cam_actuator_power_down(struct cam_actuator_ctrl_t *a_ctrl);
int32_t oplus_cam_actuator_power_up(struct cam_actuator_ctrl_t *a_ctrl, int32_t rc);
int actuator_power_down_thread(void *arg);

void oplus_cam_second_actuator_power_down(struct cam_actuator_ctrl_t *a_ctrl);
int32_t oplus_cam_second_actuator_power_up(struct cam_actuator_ctrl_t *a_ctrl, int32_t rc);
int actuator_power_down_second_thread(void *arg);
int actuator_power_up_parklens_second_thread(void *arg);

void oplus_cam_actuator_parklens(struct cam_actuator_ctrl_t *a_ctrl);
int actuator_power_up_parklens_thread(void *arg);
int actuator_power_up_parklens_second_thread(void *arg);
void oplus_cam_parklens_init(struct cam_actuator_ctrl_t *a_ctrl);
uint32_t oplus_cam_actuator_read_vaule(struct cam_actuator_ctrl_t *a_ctrl, uint32_t addr);
int32_t cam_actuator_ramwrite(struct cam_actuator_ctrl_t *a_ctrl,
uint32_t addr, uint32_t data, unsigned short mdelay,enum camera_sensor_i2c_type addr_type,enum camera_sensor_i2c_type data_type);

#endif /* _CAM_ACTUATOR_CORE_H_ */

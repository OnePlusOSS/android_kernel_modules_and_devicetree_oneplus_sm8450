/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2017-2018, The Linux Foundation. All rights reserved.
 */

#ifndef _OPLUS_CAM_ACTUATOR_CORE_H_
#define _OPLUS_CAM_ACTUATOR_CORE_H_

#include "cam_actuator_dev.h"

void cam_actuator_poll_setting_update(struct cam_actuator_ctrl_t *a_ctrl);
void cam_actuator_poll_setting_apply(struct cam_actuator_ctrl_t *a_ctrl);

#endif /* _CAM_ACTUATOR_CORE_H_ */

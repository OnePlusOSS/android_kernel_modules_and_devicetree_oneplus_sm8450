// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017-2021, The Linux Foundation. All rights reserved.
 */

#include "cam_actuator_dev.h"
#include "cam_req_mgr_dev.h"
#include "cam_actuator_soc.h"
#include "cam_actuator_core.h"
#include "cam_trace.h"
#include "camera_main.h"

#include "oplus_cam_actuator_dev.h"
#include "oplus_cam_actuator_core.h"


void oplus_cam_actuator_sds_enable(struct cam_actuator_ctrl_t *a_ctrl)
{
	mutex_lock(&(a_ctrl->actuator_mutex));
	a_ctrl->camera_actuator_shake_detect_enable = true;
	mutex_unlock(&(a_ctrl->actuator_mutex));
}

int32_t oplus_cam_actuator_lock(struct cam_actuator_ctrl_t *a_ctrl)
{
	int rc = 0;
	mutex_lock(&(a_ctrl->actuator_mutex));
	if (a_ctrl->camera_actuator_shake_detect_enable && a_ctrl->cam_act_last_state == CAM_ACTUATOR_INIT)
	{
		CAM_INFO(CAM_ACTUATOR, "SDS Actuator lock start");
		a_ctrl->io_master_info.cci_client->i2c_freq_mode = I2C_FAST_PLUS_MODE;
		a_ctrl->io_master_info.cci_client->sid = (0xC2 >> 1);
		a_ctrl->io_master_info.cci_client->retries = 0;
		a_ctrl->io_master_info.cci_client->id_map = 0;

		rc = oplus_cam_actuator_power_up_sds(a_ctrl);
		if (rc < 0)
		{
			CAM_INFO(CAM_ACTUATOR, "Failed for Actuator Power up failed: %d", rc);
			mutex_unlock(&(a_ctrl->actuator_mutex));
			return rc;
		}

		rc = cam_actuator_ramwrite(a_ctrl, (uint32_t)0x0200, (uint32_t)0x01, 5, CAMERA_SENSOR_I2C_TYPE_WORD, CAMERA_SENSOR_I2C_TYPE_BYTE);
		msleep(5);
		rc = cam_actuator_ramwrite(a_ctrl, (uint32_t)0x0200, (uint32_t)0x01, 5, CAMERA_SENSOR_I2C_TYPE_WORD, CAMERA_SENSOR_I2C_TYPE_BYTE);
		msleep(5);
		rc = cam_actuator_ramwrite(a_ctrl, (uint32_t)0x0080, (uint32_t)0x01, 0, CAMERA_SENSOR_I2C_TYPE_BYTE, CAMERA_SENSOR_I2C_TYPE_BYTE);
		rc = cam_actuator_ramwrite(a_ctrl, (uint32_t)0x0080, (uint32_t)0x00, 0, CAMERA_SENSOR_I2C_TYPE_BYTE, CAMERA_SENSOR_I2C_TYPE_BYTE);

		if (rc < 0)
		{
			int rc_power_down = oplus_cam_actuator_power_down_sds(a_ctrl);
			if (rc_power_down < 0)
			{
				CAM_ERR(CAM_ACTUATOR, "SDS oplus_cam_actuator_power_down fail, rc_power_down = %d", rc_power_down);
			}
		}
		else
		{
			a_ctrl->cam_act_last_state = CAM_ACTUATOR_LOCK;
		}
	}
	else
	{
		CAM_ERR(CAM_ACTUATOR, "do not support SDS(shake detect service)");
	}
	mutex_unlock(&(a_ctrl->actuator_mutex));

	return rc;
}

int32_t oplus_cam_actuator_unlock(struct cam_actuator_ctrl_t *a_ctrl)
{
	int rc = 0;

	struct cam_actuator_soc_private *soc_private;
	struct cam_sensor_power_ctrl_t *power_info;

	if (!a_ctrl) {
		CAM_ERR(CAM_ACTUATOR, "failed: a_ctrl %pK", a_ctrl);
		return -EINVAL;
	}

	soc_private =
		(struct cam_actuator_soc_private *)a_ctrl->soc_info.soc_private;
	power_info = &soc_private->power_info;

	if (!power_info) {
		CAM_ERR(CAM_ACTUATOR, "failed: power_info %pK", power_info);
		return -EINVAL;
	}

	mutex_lock(&(a_ctrl->actuator_mutex));
	if (a_ctrl->camera_actuator_shake_detect_enable && a_ctrl->cam_act_last_state == CAM_ACTUATOR_LOCK)
	{
		rc = oplus_cam_actuator_power_down_sds(a_ctrl);
		if (rc < 0) {
			CAM_ERR(CAM_ACTUATOR, "Actuator Power down failed");
		} else {
			a_ctrl->cam_act_last_state = CAM_ACTUATOR_INIT;
		}
	}
	else
	{
		CAM_ERR(CAM_ACTUATOR, "do not support SDS(shake detect service)");
	}

	mutex_unlock(&(a_ctrl->actuator_mutex));

	return rc;
}

int32_t oplus_cam_actuator_power_up_sds(struct cam_actuator_ctrl_t *a_ctrl)
{
	int rc = 0;
	struct cam_hw_soc_info  *soc_info =
		&a_ctrl->soc_info;
	struct cam_actuator_soc_private  *soc_private;
	struct cam_sensor_power_ctrl_t *power_info;

	soc_private =
		(struct cam_actuator_soc_private *)a_ctrl->soc_info.soc_private;
	power_info = &soc_private->power_info;

	if ((power_info->power_setting == NULL) &&
		(power_info->power_down_setting == NULL)) {
		CAM_INFO(CAM_ACTUATOR,
			"Using default power settings");
		rc = oplus_cam_actuator_construct_default_power_setting_sds(power_info);
		if (rc < 0) {
			CAM_ERR(CAM_ACTUATOR,
				"Construct default actuator power setting failed.");
			return rc;
		}
	}

	//Parse and fill vreg params for power up settings 
	rc = msm_camera_fill_vreg_params(
		&a_ctrl->soc_info,
		power_info->power_setting,
		power_info->power_setting_size);
	if (rc) {
		CAM_ERR(CAM_ACTUATOR,
			"failed to fill vreg params for power up rc:%d", rc);
		return rc;
	}

	// Parse and fill vreg params for power down settings
	rc = msm_camera_fill_vreg_params(
		&a_ctrl->soc_info,
		power_info->power_down_setting,
		power_info->power_down_setting_size);
	if (rc) {
		CAM_ERR(CAM_ACTUATOR,
			"failed to fill vreg params power down rc:%d", rc);
		return rc;
	}

	power_info->dev = soc_info->dev;

	rc = cam_sensor_core_power_up(power_info, soc_info);
	if (rc) {
		CAM_ERR(CAM_ACTUATOR,
			"failed in actuator power up rc %d", rc);
		return rc;
	} else {
		CAM_INFO(CAM_ACTUATOR,
			"actuator Power Up success for cci_device:%d, cci_i2c_master:%d, sid:0x%x",
			a_ctrl->io_master_info.cci_client->cci_device,
			a_ctrl->io_master_info.cci_client->cci_i2c_master,
			a_ctrl->io_master_info.cci_client->sid);
	}

	rc = camera_io_init(&a_ctrl->io_master_info);
	if (rc < 0) {
		CAM_ERR(CAM_ACTUATOR, "cci init failed: rc: %d", rc);
		goto cci_failure;
	}

	return rc;
cci_failure:
	if (cam_sensor_util_power_down(power_info, soc_info)){
		CAM_ERR(CAM_ACTUATOR, "Power down failure");
	} else {
		CAM_INFO(CAM_ACTUATOR,
				"actuator Power Down success for cci_device:%d, cci_i2c_master:%d, sid:0x%x",
				a_ctrl->io_master_info.cci_client->cci_device,
				a_ctrl->io_master_info.cci_client->cci_i2c_master,
				a_ctrl->io_master_info.cci_client->sid);
	}

	return rc;
}

int32_t oplus_cam_actuator_power_down_sds(struct cam_actuator_ctrl_t *a_ctrl)
{
	int32_t rc = 0;
	struct cam_sensor_power_ctrl_t *power_info;
	struct cam_hw_soc_info *soc_info = &a_ctrl->soc_info;
	struct cam_actuator_soc_private  *soc_private;

	if (!a_ctrl) {
		CAM_ERR(CAM_ACTUATOR, "failed: a_ctrl %pK", a_ctrl);
		return -EINVAL;
	}

	soc_private =
		(struct cam_actuator_soc_private *)a_ctrl->soc_info.soc_private;
	power_info = &soc_private->power_info;
	soc_info = &a_ctrl->soc_info;

	if (!power_info) {
		CAM_ERR(CAM_ACTUATOR, "failed: power_info %pK", power_info);
		return -EINVAL;
	}

	if ((power_info->power_setting == NULL) &&
		(power_info->power_down_setting == NULL)) {
		CAM_INFO(CAM_ACTUATOR,
			"Using default power settings");
		rc = oplus_cam_actuator_construct_default_power_setting_sds(power_info);
		if (rc < 0) {
			CAM_ERR(CAM_ACTUATOR,
				"Construct default actuator power setting failed.");
			return rc;
		}
		/* Parse and fill vreg params for power up settings */
		rc = msm_camera_fill_vreg_params(
			&a_ctrl->soc_info,
			power_info->power_setting,
			power_info->power_setting_size);
		if (rc) {
			CAM_ERR(CAM_ACTUATOR,
				"failed to fill vreg params for power up rc:%d", rc);
			return rc;
		}

		/* Parse and fill vreg params for power down settings*/
		rc = msm_camera_fill_vreg_params(
			&a_ctrl->soc_info,
			power_info->power_down_setting,
			power_info->power_down_setting_size);
		if (rc) {
			CAM_ERR(CAM_ACTUATOR,
				"failed to fill vreg params power down rc:%d", rc);
		}

	}

	rc = cam_sensor_util_power_down(power_info, soc_info);
	if (rc) {
		CAM_ERR(CAM_ACTUATOR, "power down the core is failed:%d", rc);
		return rc;
	} else {
		CAM_INFO(CAM_ACTUATOR,
				"actuator Power Down success for cci_device:%d, cci_i2c_master:%d, sid:0x%x",
				a_ctrl->io_master_info.cci_client->cci_device,
				a_ctrl->io_master_info.cci_client->cci_i2c_master,
				a_ctrl->io_master_info.cci_client->sid);
	}

	camera_io_release(&a_ctrl->io_master_info);

	return rc;
}


int32_t oplus_cam_actuator_construct_default_power_setting_sds(
	struct cam_sensor_power_ctrl_t *power_info)
{
	int rc = 0;

	power_info->power_setting_size = 3;
	power_info->power_setting =
		kzalloc(sizeof(struct cam_sensor_power_setting) * 3,
			GFP_KERNEL);
	if (!power_info->power_setting)
		return -ENOMEM;

	power_info->power_setting[0].seq_type = SENSOR_VIO;
	power_info->power_setting[0].seq_val = CAM_VIO;
	power_info->power_setting[0].config_val = 1;
	power_info->power_setting[0].delay = 1;


	power_info->power_setting[1].seq_type = SENSOR_CUSTOM_REG1;
	power_info->power_setting[1].seq_val = CAM_V_CUSTOM1;
	power_info->power_setting[1].config_val = 1;
	power_info->power_setting[1].delay = 1;

	power_info->power_setting[2].seq_type = SENSOR_VAF;
	power_info->power_setting[2].seq_val = CAM_VAF;
	power_info->power_setting[2].config_val = 1;
	power_info->power_setting[2].delay = 5;


	power_info->power_down_setting_size = 3;
	power_info->power_down_setting =
		kzalloc(sizeof(struct cam_sensor_power_setting) * 3,
			GFP_KERNEL);
	if (!power_info->power_down_setting) {
		rc = -ENOMEM;
		goto free_power_settings;
	}

	power_info->power_down_setting[0].seq_type = SENSOR_VAF;
	power_info->power_down_setting[0].seq_val = CAM_VAF;
	power_info->power_down_setting[0].config_val = 0;
	power_info->power_down_setting[0].delay = 1;

	power_info->power_down_setting[1].seq_type = SENSOR_CUSTOM_REG1;
	power_info->power_down_setting[1].seq_val = CAM_V_CUSTOM1;
	power_info->power_down_setting[1].config_val = 0;
	power_info->power_down_setting[1].delay = 1;

	power_info->power_down_setting[2].seq_type = SENSOR_VIO;
	power_info->power_down_setting[2].seq_val = CAM_VIO;
	power_info->power_down_setting[2].config_val = 0;
	power_info->power_down_setting[2].delay = 0;

	return rc;

free_power_settings:
	kfree(power_info->power_setting);
	power_info->power_setting = NULL;
	power_info->power_setting_size = 0;
	return rc;
}


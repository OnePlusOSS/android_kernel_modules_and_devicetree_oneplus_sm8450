/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2017-2018, The Linux Foundation. All rights reserved.
 */

#ifndef _OPLUS_CAM_SENSOR_CORE_H_
#define _OPLUS_CAM_SENSOR_CORE_H_
#include "cam_sensor_dev.h"

#define CAM_IMX709_SENSOR_ID 0x709

struct cam_sensor_i2c_reg_setting_array {
	struct cam_sensor_i2c_reg_array reg_setting[3000];
	unsigned short size;
	enum camera_sensor_i2c_type addr_type;
	enum camera_sensor_i2c_type data_type;
	unsigned short delay;
};
struct cam_sensor_aon_reg_setting_array {
	struct cam_sensor_i2c_reg_array reg_setting[1];
	unsigned short size;
	enum camera_sensor_i2c_type addr_type;
	enum camera_sensor_i2c_type data_type;
	unsigned short delay;
};

struct cam_sensor_i2c_reg_setting_array_large {
	struct cam_sensor_i2c_reg_array reg_setting[5920];
	unsigned short size;
	enum camera_sensor_i2c_type addr_type;
	enum camera_sensor_i2c_type data_type;
	unsigned short delay;
};

struct cam_sensor_settings_large {
	struct cam_sensor_i2c_reg_setting_array_large ov32c_setting;
};

struct cam_sensor_settings {
	struct cam_sensor_i2c_reg_setting_array imx586_setting0;
	struct cam_sensor_i2c_reg_setting_array imx586_setting1;
	struct cam_sensor_i2c_reg_setting_array streamoff;
	struct cam_sensor_i2c_reg_setting_array s5k3m5_setting;
	struct cam_sensor_i2c_reg_setting_array imx355_setting;
	struct cam_sensor_i2c_reg_setting_array imx471_setting;
	struct cam_sensor_i2c_reg_setting_array imx481_setting;
	struct cam_sensor_i2c_reg_setting_array gc5035_setting;
	struct cam_sensor_i2c_reg_setting_array imx689_setting;
	struct cam_sensor_i2c_reg_setting_array gc2375_setting;

	struct cam_sensor_i2c_reg_setting_array hi846_setting; //Rear_UW
	struct cam_sensor_i2c_reg_setting_array imx615_setting; //Front_Main
	struct cam_sensor_i2c_reg_setting_array imx616_setting; //Front_Main
	struct cam_sensor_i2c_reg_setting_array gc8054_setting; //Front_UW
	struct cam_sensor_i2c_reg_setting_array ov02b10_setting;
	struct cam_sensor_i2c_reg_setting_array ov8856_setting;
	struct cam_sensor_i2c_reg_setting_array gc02m1b_setting;
	struct cam_sensor_i2c_reg_setting_array imx686_setting;
	struct cam_sensor_i2c_reg_setting_array imx789_setting;
	struct cam_sensor_i2c_reg_setting_array ov08a10_setting;
	struct cam_sensor_i2c_reg_setting_array imx766_setting;
	struct cam_sensor_i2c_reg_setting_array imx766_ferrari_setting;
	struct cam_sensor_i2c_reg_setting_array imx890_lz_setting;
	struct cam_sensor_i2c_reg_setting_array imx890_senna_setting;
	struct cam_sensor_i2c_reg_setting_array imx682_setting;
	struct cam_sensor_i2c_reg_setting_array ov32c_setting;
	struct cam_sensor_i2c_reg_setting_array ov08d10_setting;
	struct cam_sensor_i2c_reg_setting_array imx709_setting;
	struct cam_sensor_i2c_reg_setting_array imx709_daoxiang_setting;
	struct cam_sensor_i2c_reg_setting_array imx709_tele_setting;
	struct cam_sensor_i2c_reg_setting_array imx709_aon_irq_setting;
	struct cam_sensor_i2c_reg_setting_array imx709_aon_irq_he_clr_setting;
	struct cam_sensor_i2c_reg_setting_array s5kjn1sq03_setting;
	struct cam_sensor_i2c_reg_setting_array ov08d_setting;
	struct cam_sensor_i2c_reg_setting_array s5k3p9_streamoff_setting;
	struct cam_sensor_i2c_reg_setting_array s5k3p9_setting;
	struct cam_sensor_i2c_reg_setting_array s5k3p9_setting1;
	struct cam_sensor_i2c_reg_setting_array s5k3p9_setting2;
	struct cam_sensor_i2c_reg_setting_array ov32c_setting1;
	struct cam_sensor_i2c_reg_setting_array ov32c_setting2;
	struct cam_sensor_i2c_reg_setting_array imx890_setting;
	struct cam_sensor_i2c_reg_setting_array ov64b_setting;
	struct cam_sensor_i2c_reg_setting_array Sec_ov64b_setting;
	struct cam_sensor_i2c_reg_setting_array imx890_monroe_setting;
	struct cam_sensor_i2c_reg_setting_array ov64b_senna_setting;
	struct cam_sensor_i2c_reg_setting_array ov64b_monroe_setting;
};

int cam_ftm_power_down(struct cam_sensor_ctrl_t *s_ctrl);
int cam_ftm_power_up(struct cam_sensor_ctrl_t *s_ctrl);

bool cam_ftm_if_do(void);

int oplus_cam_sensor_update_setting(struct cam_sensor_ctrl_t *s_ctrl);
void cam_sensor_get_dt_data(struct cam_sensor_ctrl_t *s_ctrl);

uint32_t cam_override_chipid(struct cam_sensor_ctrl_t *s_ctrl);
int cam_sensor_read_qsc(struct cam_sensor_ctrl_t *s_ctrl);
bool cam_sensor_need_bypass_write(struct cam_sensor_ctrl_t *s_ctrl,struct i2c_settings_list *i2c_list);

int cam_sensor_match_id_oem(struct cam_sensor_ctrl_t *s_ctrl,uint32_t chip_id);
int cam_get_second_provison_vendor_id(struct cam_sensor_ctrl_t *s_ctrl);

int32_t cam_sensor_update_id_info(struct cam_cmd_probe_v2 *probe_info,
    struct cam_sensor_ctrl_t *s_ctrl);

int cam_sensor_stop(struct cam_sensor_ctrl_t *s_ctrl);
int cam_sensor_start(struct cam_sensor_ctrl_t *s_ctrl);
int32_t oplus_cam_sensor_driver_cmd(struct cam_sensor_ctrl_t *s_ctrl,void *arg);
int oplus_sensor_sony_get_dpc_data(struct cam_sensor_ctrl_t *s_ctrl);

int cam_aon_irq_power_up(struct cam_sensor_ctrl_t *s_ctrl);
int cam_aon_irq_power_down(struct cam_sensor_ctrl_t *s_ctrl);
irqreturn_t aon_interupt_handler(int irq, void *data);
void cam_aon_do_work(struct work_struct *work);
bool cam_aon_if_do(void);

int oplus_shift_sensor_mode(struct cam_sensor_ctrl_t *s_ctrl);

int oplus_cam_get_sensor_temp(struct cam_sensor_ctrl_t *s_ctrl, int32_t *sensor_temp);

void check_streamoff(struct cam_sensor_ctrl_t *s_ctrl);

#endif /* _OPLUS_CAM_SENSOR_CORE_H_ */

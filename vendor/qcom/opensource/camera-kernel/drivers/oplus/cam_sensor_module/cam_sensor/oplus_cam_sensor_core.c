// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017-2020, The Linux Foundation. All rights reserved.
 */
#include <linux/module.h>
#include <cam_sensor_cmn_header.h>
#include "cam_sensor_util.h"
#include "cam_soc_util.h"
#include "cam_trace.h"
#include "cam_common_util.h"
#include "cam_packet_util.h"
#include "cam_sensor_dev.h"
#include "cam_req_mgr_dev.h"
#include "cam_sensor_soc.h"
#include "cam_sensor_core.h"
#if IS_ENABLED(CONFIG_PROJECT_INFO)
#include <linux/oem/project_info.h>
#endif
#include "oplus_cam_sensor_core.h"
#include "cam_res_mgr_api.h"

bool is_AON_current = 0;
bool is_ftm_current_test = false;
int aon_irq_cnt = 0;
struct cam_sensor_i2c_reg_setting sensor_setting;

struct cam_sensor_settings sensor_settings =
{
#include "cam_sensor_settings.h"
};

struct cam_sensor_settings sensor_init_settings = {
#include "cam_sensor_initsettings.h"
};

struct cam_sensor_settings_large sensor_init_settings_large = {
#include "cam_sensor_initsettings_large.h"
};

bool chip_version_old = FALSE;
struct sony_dfct_tbl_t sony_dfct_tbl;

bool cam_aon_if_do(void)
{
	CAM_DBG(CAM_SENSOR, "AON state :%d",is_AON_current);
	return is_AON_current;
}

int cam_aon_irq_power_up(struct cam_sensor_ctrl_t *s_ctrl) {
	int rc = 0,i = 0;
	struct cam_sensor_power_setting *power_setting = NULL;
	aon_irq_cnt = 0;
	if(is_AON_current)
		return rc;
	is_AON_current = 1;
	for (i = 0; i < s_ctrl->sensordata->power_info.power_setting_size; i++) {
		power_setting = &s_ctrl->sensordata->power_info.power_setting[i];
		if (power_setting) {
			if (power_setting->seq_type == SENSOR_CUSTOM_GPIO1){
				power_setting->config_val = 0;
				break;
			}
		}
	}
	rc = cam_sensor_power_up(s_ctrl);
	CAM_INFO(CAM_SENSOR, "aon power up sensor id 0x%x,result %d",s_ctrl->sensordata->slave_info.sensor_id,rc);
	if(rc < 0) {
		CAM_ERR(CAM_SENSOR, "aon power up faild!");
		return rc;
	}
	for (i = 0; i < s_ctrl->sensordata->power_info.power_setting_size; i++) {
		power_setting = &s_ctrl->sensordata->power_info.power_setting[i];
		if (power_setting) {
			if (power_setting->seq_type == SENSOR_CUSTOM_GPIO1){
				power_setting->config_val = 1;
				break;
			}
		}
	}

	if (s_ctrl->sensordata->slave_info.sensor_id == 0x709) {
		CAM_INFO(CAM_SENSOR, "aon sensor setting 0x%x",s_ctrl->sensordata->slave_info.sensor_id);
		sensor_setting.reg_setting = sensor_settings.imx709_aon_irq_setting.reg_setting;
		sensor_setting.addr_type = CAMERA_SENSOR_I2C_TYPE_WORD;
		sensor_setting.data_type = CAMERA_SENSOR_I2C_TYPE_BYTE;
		sensor_setting.size = sensor_settings.imx709_aon_irq_setting.size;
		sensor_setting.delay = sensor_settings.imx709_aon_irq_setting.delay;
		rc = camera_io_dev_write(&(s_ctrl->io_master_info), &sensor_setting);
	}else {
		CAM_ERR(CAM_SENSOR, "aon unknown sensor id 0x%x",s_ctrl->sensordata->slave_info.sensor_id);
		rc = -1;
	}
	if (rc < 0) {
		CAM_ERR(CAM_SENSOR, "aon Failed to write sensor setting");
	}

	// power down MCLK,CCI, using clock in 709 sensor
	rc = cam_sensor_power_down_except_sensor(s_ctrl);
	CAM_INFO(CAM_SENSOR, "aon sensor power down _except sensor rc=%d.",rc);

    return rc;
}

int cam_aon_irq_power_down(struct cam_sensor_ctrl_t *s_ctrl) {
    int rc = 0;
    aon_irq_cnt = 0;
    if(!is_AON_current)
        return rc;
    is_AON_current = 0;
    if (s_ctrl->sensordata->slave_info.sensor_id == 0x709){
        sensor_setting.reg_setting = sensor_settings.streamoff.reg_setting;
        sensor_setting.addr_type = CAMERA_SENSOR_I2C_TYPE_WORD;
        sensor_setting.data_type = CAMERA_SENSOR_I2C_TYPE_BYTE;
        sensor_setting.size = sensor_settings.streamoff.size;
        sensor_setting.delay = sensor_settings.streamoff.delay;
        rc = camera_io_dev_write(&(s_ctrl->io_master_info),&sensor_setting);
        if(rc < 0) {
            /* If the I2C reg write failed for the first section reg, send
            the result instead of keeping writing the next section of reg. */
            CAM_ERR(CAM_SENSOR, "aon Failed to stream off setting,rc=%d.",rc);
        }
    }

	rc = cam_sensor_power_down_only_sensor(s_ctrl);
	CAM_INFO(CAM_SENSOR, "aon sensor power down only sensor rc=%d.",rc);

    return rc;
}

irqreturn_t aon_interupt_handler(int irq, void *data)
{
   struct platform_device *pdev = data;
   struct cam_sensor_ctrl_t *s_ctrl = platform_get_drvdata(pdev);
   schedule_work(&s_ctrl->aon_wq);
   return IRQ_HANDLED;
}

void cam_aon_do_work(struct work_struct *work)
{
	//struct cam_sensor_i2c_reg_setting sensor_setting;
	int rc = 0;
	struct kernel_siginfo info;
	static struct task_struct *pg_task;
	struct cam_sensor_ctrl_t *s_ctrl =
			container_of(work, struct cam_sensor_ctrl_t, aon_wq);
	CAM_INFO(CAM_SENSOR, "send signal to userspace");
    /*
	sensor_setting.reg_setting = sensor_settings.imx709_aon_irq_he_clr_setting.reg_setting;
	sensor_setting.addr_type = sensor_settings.imx709_aon_irq_he_clr_setting.addr_type;
	sensor_setting.data_type = sensor_settings.imx709_aon_irq_he_clr_setting.data_type;
	sensor_setting.size = sensor_settings.imx709_aon_irq_he_clr_setting.size;
	sensor_setting.delay = sensor_settings.imx709_aon_irq_he_clr_setting.delay;
	rc = camera_io_dev_write(&(s_ctrl->io_master_info), &sensor_setting);
	if (rc < 0) {
		CAM_ERR(CAM_SENSOR, "clear aon HE failed to write sensor setting");
		cam_aon_irq_power_down(s_ctrl);
		return;
	}
    */
	CAM_INFO(CAM_SENSOR, "send_sig_info s_ctrl->pid:%d aon_irq_cnt:%d", s_ctrl->pid, aon_irq_cnt);
	if(s_ctrl->pid != 0 && aon_irq_cnt == 1){
		CAM_INFO(CAM_SENSOR, "send signal to app");
		pg_task = get_pid_task(find_vpid(s_ctrl->pid), PIDTYPE_PID);
		info.si_signo = SIGIO;
		info.si_errno = 0;
		info.si_code = 1;
		info.si_addr = NULL;
		if(!pg_task){
			CAM_ERR(CAM_SENSOR, "pg_task got null.");
			return;
		}
		rc = send_sig_info(SIGIO, &info, pg_task);
		if (rc < 0) {
			CAM_ERR(CAM_SENSOR, "send_sig_info failed");
			return;
		}
	}
	aon_irq_cnt++;
	return;
}

int cam_ftm_power_down(struct cam_sensor_ctrl_t *s_ctrl)
{
	int rc = 0;
	CAM_ERR(CAM_SENSOR,"FTM stream off");
	if (s_ctrl->sensordata->slave_info.sensor_id == 0x586 ||
		s_ctrl->sensordata->slave_info.sensor_id == 0x30d5||
		s_ctrl->sensordata->slave_info.sensor_id == 0x471 ||
		s_ctrl->sensordata->slave_info.sensor_id == 0x355 ||
		s_ctrl->sensordata->slave_info.sensor_id == 0x481 ||
		s_ctrl->sensordata->slave_info.sensor_id == 0x5035||
		s_ctrl->sensordata->slave_info.sensor_id == 0x689 ||
		s_ctrl->sensordata->slave_info.sensor_id == 0x2375||
		s_ctrl->sensordata->slave_info.sensor_id == 0x4608||
		s_ctrl->sensordata->slave_info.sensor_id == 0x0616||
		s_ctrl->sensordata->slave_info.sensor_id == 0x8054||
		s_ctrl->sensordata->slave_info.sensor_id == 0x88  ||
		s_ctrl->sensordata->slave_info.sensor_id == 0x2b  ||
		s_ctrl->sensordata->slave_info.sensor_id == 0x02  ||
		s_ctrl->sensordata->slave_info.sensor_id == 0x686 ||
		s_ctrl->sensordata->slave_info.sensor_id == 0x789 ||
		s_ctrl->sensordata->slave_info.sensor_id == 0x841 ||
		s_ctrl->sensordata->slave_info.sensor_id == 0x766 ||
		s_ctrl->sensordata->slave_info.sensor_id == 0x766E||
		s_ctrl->sensordata->slave_info.sensor_id == 0x766F||
		s_ctrl->sensordata->slave_info.sensor_id == 0x682 ||
		s_ctrl->sensordata->slave_info.sensor_id == 0x709 ||
		s_ctrl->sensordata->slave_info.sensor_id == 0x3243||
		s_ctrl->sensordata->slave_info.sensor_id == 0x38e1||
		s_ctrl->sensordata->slave_info.sensor_id == 0x5647||
		s_ctrl->sensordata->slave_info.sensor_id == 0x3109||
		s_ctrl->sensordata->slave_info.sensor_id == 0xe000||
		s_ctrl->sensordata->slave_info.sensor_id == 0x581 ||
		s_ctrl->sensordata->slave_info.sensor_id == 0x5664||
		s_ctrl->sensordata->slave_info.sensor_id == 0x890)
	{
		sensor_setting.reg_setting = sensor_settings.streamoff.reg_setting;
		sensor_setting.addr_type = CAMERA_SENSOR_I2C_TYPE_WORD;
		sensor_setting.data_type = CAMERA_SENSOR_I2C_TYPE_BYTE;
		sensor_setting.size = sensor_settings.streamoff.size;
		sensor_setting.delay = sensor_settings.streamoff.delay;
		rc = camera_io_dev_write(&(s_ctrl->io_master_info),&sensor_setting);
		if(rc < 0)
		{
			/* If the I2C reg write failed for the first section reg, send
			the result instead of keeping writing the next section of reg. */
			CAM_ERR(CAM_SENSOR, "FTM Failed to stream off setting,rc=%d.",rc);
		}
		else
		{
			CAM_ERR(CAM_SENSOR, "FTM successfully to stream off");
		}
	}
	rc = cam_sensor_power_down(s_ctrl);
	CAM_ERR(CAM_SENSOR, "FTM power down rc=%d",rc);
	return rc;
}

int cam_ftm_power_up(struct cam_sensor_ctrl_t *s_ctrl)
{
	int rc = 0;

	rc = cam_sensor_power_up(s_ctrl);
	CAM_ERR(CAM_SENSOR, "FTM power up sensor id 0x%x,result %d",s_ctrl->sensordata->slave_info.sensor_id,rc);
	if(rc < 0)
	{
		CAM_ERR(CAM_SENSOR, "FTM power up faild!");
		return rc;
	}
	is_ftm_current_test =true;
	if (s_ctrl->sensordata->slave_info.sensor_id == 0x586||
		s_ctrl->sensordata->slave_info.sensor_id == 0x581)
	{
		CAM_ERR(CAM_SENSOR, "FTM sensor setting 0x%x",s_ctrl->sensordata->slave_info.sensor_id);
		sensor_setting.reg_setting = sensor_settings.imx586_setting0.reg_setting;
		sensor_setting.addr_type = CAMERA_SENSOR_I2C_TYPE_WORD;
		sensor_setting.data_type = CAMERA_SENSOR_I2C_TYPE_BYTE;
		sensor_setting.size = sensor_settings.imx586_setting0.size;
		sensor_setting.delay = sensor_settings.imx586_setting0.delay;
		rc = camera_io_dev_write(&(s_ctrl->io_master_info),&sensor_setting);
		if(rc < 0)
		{
			/* If the I2C reg write failed for the first section reg, send
			the result instead of keeping writing the next section of reg. */
			CAM_ERR(CAM_SENSOR, "FTM Failed to write sensor setting 1/2");
			goto power_down;;
		}
		else
		{
			CAM_ERR(CAM_SENSOR, "FTM successfully to write sensor setting 1/2");
		}
		sensor_setting.reg_setting = sensor_settings.imx586_setting1.reg_setting;
		sensor_setting.addr_type = CAMERA_SENSOR_I2C_TYPE_WORD;
		sensor_setting.data_type = CAMERA_SENSOR_I2C_TYPE_BYTE;
		sensor_setting.size = sensor_settings.imx586_setting1.size;
		sensor_setting.delay = sensor_settings.imx586_setting1.delay;
		rc = camera_io_dev_write(&(s_ctrl->io_master_info),&sensor_setting);
		if(rc < 0)
		{
			CAM_ERR(CAM_SENSOR, "FTM Failed to write sensor setting 2/2");
			goto power_down;;
		}
		else
		{
			CAM_ERR(CAM_SENSOR, "FTM successfully to write sensor setting 2/2");
		}
	}
	else
	{
		if (s_ctrl->sensordata->slave_info.sensor_id == 0x30d5)
		{
			CAM_ERR(CAM_SENSOR, "FTM sensor setting 0x%x",s_ctrl->sensordata->slave_info.sensor_id);
			sensor_setting.reg_setting = sensor_settings.s5k3m5_setting.reg_setting;
			sensor_setting.addr_type = CAMERA_SENSOR_I2C_TYPE_WORD;
			sensor_setting.data_type = CAMERA_SENSOR_I2C_TYPE_WORD;
			sensor_setting.size = sensor_settings.s5k3m5_setting.size;
			sensor_setting.delay = sensor_settings.s5k3m5_setting.delay;
			rc = camera_io_dev_write(&(s_ctrl->io_master_info), &sensor_setting);
		}
		else if (s_ctrl->sensordata->slave_info.sensor_id == 0x5035)
		{
			CAM_ERR(CAM_SENSOR, "FTM sensor setting 0x%x",s_ctrl->sensordata->slave_info.sensor_id);
			sensor_setting.reg_setting = sensor_settings.gc5035_setting.reg_setting;
			sensor_setting.addr_type = CAMERA_SENSOR_I2C_TYPE_BYTE;
			sensor_setting.data_type = CAMERA_SENSOR_I2C_TYPE_BYTE;
			sensor_setting.size = sensor_settings.gc5035_setting.size;
			sensor_setting.delay = sensor_settings.gc5035_setting.delay;
			rc = camera_io_dev_write(&(s_ctrl->io_master_info), &sensor_setting);
		}
		else if (s_ctrl->sensordata->slave_info.sensor_id == 0x471)
		{
			CAM_ERR(CAM_SENSOR, "FTM sensor setting 0x%x",s_ctrl->sensordata->slave_info.sensor_id);
			sensor_setting.reg_setting = sensor_settings.imx471_setting.reg_setting;
			sensor_setting.addr_type = CAMERA_SENSOR_I2C_TYPE_WORD;
			sensor_setting.data_type = CAMERA_SENSOR_I2C_TYPE_BYTE;
			sensor_setting.size = sensor_settings.imx471_setting.size;
			sensor_setting.delay = sensor_settings.imx471_setting.delay;
			rc = camera_io_dev_write(&(s_ctrl->io_master_info), &sensor_setting);
		}
		else if (s_ctrl->sensordata->slave_info.sensor_id == 0x0355)
		{
			CAM_ERR(CAM_SENSOR, "FTM sensor setting 0x%x",s_ctrl->sensordata->slave_info.sensor_id);
			sensor_setting.reg_setting = sensor_settings.imx355_setting.reg_setting;
			sensor_setting.addr_type = CAMERA_SENSOR_I2C_TYPE_WORD;
			sensor_setting.data_type = CAMERA_SENSOR_I2C_TYPE_BYTE;
			sensor_setting.size = sensor_settings.imx355_setting.size;
			sensor_setting.delay = sensor_settings.imx355_setting.delay;
			rc = camera_io_dev_write(&(s_ctrl->io_master_info), &sensor_setting);
		}
		else if (s_ctrl->sensordata->slave_info.sensor_id == 0x481)
		{
			CAM_ERR(CAM_SENSOR, "FTM sensor setting 0x%x",s_ctrl->sensordata->slave_info.sensor_id);
			sensor_setting.reg_setting = sensor_settings.imx481_setting.reg_setting;
			sensor_setting.addr_type = CAMERA_SENSOR_I2C_TYPE_WORD;
			sensor_setting.data_type = CAMERA_SENSOR_I2C_TYPE_BYTE;
			sensor_setting.size = sensor_settings.imx481_setting.size;
			sensor_setting.delay = sensor_settings.imx481_setting.delay;
			rc = camera_io_dev_write(&(s_ctrl->io_master_info), &sensor_setting);
		}
		else if (s_ctrl->sensordata->slave_info.sensor_id == 0x689)
		{
			CAM_ERR(CAM_SENSOR, "FTM sensor setting 0x%x",s_ctrl->sensordata->slave_info.sensor_id);
			sensor_setting.reg_setting = sensor_settings.imx689_setting.reg_setting;
			sensor_setting.addr_type = CAMERA_SENSOR_I2C_TYPE_WORD;
			sensor_setting.data_type = CAMERA_SENSOR_I2C_TYPE_BYTE;
			sensor_setting.size = sensor_settings.imx689_setting.size;
			sensor_setting.delay = sensor_settings.imx689_setting.delay;
			rc = camera_io_dev_write(&(s_ctrl->io_master_info), &sensor_setting);
		}
		else if (s_ctrl->sensordata->slave_info.sensor_id == 0x2375)
		{
			CAM_ERR(CAM_SENSOR, "FTM sensor setting 0x%x",s_ctrl->sensordata->slave_info.sensor_id);
			sensor_setting.reg_setting = sensor_settings.gc2375_setting.reg_setting;
			sensor_setting.addr_type = CAMERA_SENSOR_I2C_TYPE_BYTE;
			sensor_setting.data_type = CAMERA_SENSOR_I2C_TYPE_BYTE;
			sensor_setting.size = sensor_settings.gc2375_setting.size;
			sensor_setting.delay = sensor_settings.gc2375_setting.delay;
			rc = camera_io_dev_write(&(s_ctrl->io_master_info), &sensor_setting);
		}
		else if (s_ctrl->sensordata->slave_info.sensor_id == 0x4608)
		{
			CAM_ERR(CAM_SENSOR, "FTM sensor setting 0x%x",s_ctrl->sensordata->slave_info.sensor_id);
			sensor_setting.reg_setting = sensor_settings.hi846_setting.reg_setting;
			sensor_setting.addr_type = CAMERA_SENSOR_I2C_TYPE_WORD;
			sensor_setting.data_type = CAMERA_SENSOR_I2C_TYPE_WORD;
			sensor_setting.size = sensor_settings.hi846_setting.size;
			sensor_setting.delay = sensor_settings.hi846_setting.delay;
			rc = camera_io_dev_write(&(s_ctrl->io_master_info), &sensor_setting);
		}
		else if (s_ctrl->sensordata->slave_info.sensor_id == 0x0615)
		{
			CAM_ERR(CAM_SENSOR, "FTM sensor setting 0x%x",s_ctrl->sensordata->slave_info.sensor_id);
			sensor_setting.reg_setting = sensor_settings.imx615_setting.reg_setting;
			sensor_setting.addr_type = CAMERA_SENSOR_I2C_TYPE_WORD;
			sensor_setting.data_type = CAMERA_SENSOR_I2C_TYPE_BYTE;
			sensor_setting.size = sensor_settings.imx615_setting.size;
			sensor_setting.delay = sensor_settings.imx615_setting.delay;
			rc = camera_io_dev_write(&(s_ctrl->io_master_info), &sensor_setting);
		}
		else if (s_ctrl->sensordata->slave_info.sensor_id == 0x0616)
		{
			CAM_ERR(CAM_SENSOR, "FTM sensor setting 0x%x",s_ctrl->sensordata->slave_info.sensor_id);
			sensor_setting.reg_setting = sensor_settings.imx616_setting.reg_setting;
			sensor_setting.addr_type = CAMERA_SENSOR_I2C_TYPE_WORD;
			sensor_setting.data_type = CAMERA_SENSOR_I2C_TYPE_BYTE;
			sensor_setting.size = sensor_settings.imx616_setting.size;
			sensor_setting.delay = sensor_settings.imx616_setting.delay;
			rc = camera_io_dev_write(&(s_ctrl->io_master_info), &sensor_setting);
		}
		else if (s_ctrl->sensordata->slave_info.sensor_id == 0x8054)
		{
			CAM_ERR(CAM_SENSOR, "FTM sensor setting 0x%x",s_ctrl->sensordata->slave_info.sensor_id);
			sensor_setting.reg_setting = sensor_settings.gc8054_setting.reg_setting;
			sensor_setting.addr_type = CAMERA_SENSOR_I2C_TYPE_WORD;
			sensor_setting.data_type = CAMERA_SENSOR_I2C_TYPE_BYTE;
			sensor_setting.size = sensor_settings.gc8054_setting.size;
			sensor_setting.delay = sensor_settings.gc8054_setting.delay;
			rc = camera_io_dev_write(&(s_ctrl->io_master_info), &sensor_setting);
		}
		else if (s_ctrl->sensordata->slave_info.sensor_id == 0x88)
		{
			CAM_ERR(CAM_SENSOR, "FTM sensor setting 0x%x",s_ctrl->sensordata->slave_info.sensor_id);
			sensor_setting.reg_setting = sensor_settings.imx616_setting.reg_setting;
			sensor_setting.addr_type = CAMERA_SENSOR_I2C_TYPE_WORD;
			sensor_setting.data_type = CAMERA_SENSOR_I2C_TYPE_BYTE;
			sensor_setting.size = sensor_settings.imx616_setting.size;
			sensor_setting.delay = sensor_settings.imx616_setting.delay;
			rc = camera_io_dev_write(&(s_ctrl->io_master_info), &sensor_setting);
		}
		else if (s_ctrl->sensordata->slave_info.sensor_id == 0x2b)
		{
			CAM_ERR(CAM_SENSOR, "FTM sensor setting 0x%x",s_ctrl->sensordata->slave_info.sensor_id);
			sensor_setting.reg_setting = sensor_settings.ov02b10_setting.reg_setting;
			sensor_setting.addr_type = CAMERA_SENSOR_I2C_TYPE_BYTE;
			sensor_setting.data_type = CAMERA_SENSOR_I2C_TYPE_BYTE;
			sensor_setting.size = sensor_settings.ov02b10_setting.size;
			sensor_setting.delay = sensor_settings.ov02b10_setting.delay;
			rc = camera_io_dev_write(&(s_ctrl->io_master_info), &sensor_setting);
		}
		else if (s_ctrl->sensordata->slave_info.sensor_id == 0x02 ||
			s_ctrl->sensordata->slave_info.sensor_id == 0xe000)
		{
			CAM_ERR(CAM_SENSOR, "FTM sensor setting 0x%x",s_ctrl->sensordata->slave_info.sensor_id);
			sensor_setting.reg_setting = sensor_settings.gc02m1b_setting.reg_setting;
			sensor_setting.addr_type = CAMERA_SENSOR_I2C_TYPE_BYTE;
			sensor_setting.data_type = CAMERA_SENSOR_I2C_TYPE_BYTE;
			sensor_setting.size = sensor_settings.gc02m1b_setting.size;
			sensor_setting.delay = sensor_settings.gc02m1b_setting.delay;
			rc = camera_io_dev_write(&(s_ctrl->io_master_info), &sensor_setting);
		}
		else if (s_ctrl->sensordata->slave_info.sensor_id == 0x686)
		{
			CAM_ERR(CAM_SENSOR, "FTM sensor setting 0x%x",s_ctrl->sensordata->slave_info.sensor_id);
			sensor_setting.reg_setting = sensor_settings.imx686_setting.reg_setting;
			sensor_setting.addr_type = CAMERA_SENSOR_I2C_TYPE_WORD;
			sensor_setting.data_type = CAMERA_SENSOR_I2C_TYPE_BYTE;
			sensor_setting.size = sensor_settings.imx686_setting.size;
			sensor_setting.delay = sensor_settings.imx686_setting.delay;
			rc = camera_io_dev_write(&(s_ctrl->io_master_info), &sensor_setting);
		}
		else if (s_ctrl->sensordata->slave_info.sensor_id == 0x789)
		{
			CAM_ERR(CAM_SENSOR, "FTM sensor setting 0x%x",s_ctrl->sensordata->slave_info.sensor_id);
			sensor_setting.reg_setting = sensor_settings.imx789_setting.reg_setting;
			sensor_setting.addr_type = CAMERA_SENSOR_I2C_TYPE_WORD;
			sensor_setting.data_type = CAMERA_SENSOR_I2C_TYPE_BYTE;
			sensor_setting.size = sensor_settings.imx789_setting.size;
			sensor_setting.delay = sensor_settings.imx789_setting.delay;
			rc = camera_io_dev_write(&(s_ctrl->io_master_info), &sensor_setting);
		}
		else if (s_ctrl->sensordata->slave_info.sensor_id == 0x841)
		{
			CAM_ERR(CAM_SENSOR, "FTM sensor setting 0x%x",s_ctrl->sensordata->slave_info.sensor_id);
			sensor_setting.reg_setting = sensor_settings.ov08a10_setting.reg_setting;
			sensor_setting.addr_type = CAMERA_SENSOR_I2C_TYPE_WORD;
			sensor_setting.data_type = CAMERA_SENSOR_I2C_TYPE_BYTE;
			sensor_setting.size = sensor_settings.ov08a10_setting.size;
			sensor_setting.delay = sensor_settings.ov08a10_setting.delay;
			rc = camera_io_dev_write(&(s_ctrl->io_master_info), &sensor_setting);
		}
		else if (s_ctrl->sensordata->slave_info.sensor_id == 0x766 ||
			s_ctrl->sensordata->slave_info.sensor_id == 0x766E ||
			s_ctrl->sensordata->slave_info.sensor_id == 0x766F)
		{
			CAM_ERR(CAM_SENSOR, "FTM sensor setting 0x%x",s_ctrl->sensordata->slave_info.sensor_id);
			sensor_setting.reg_setting = sensor_settings.imx766_setting.reg_setting;
			sensor_setting.addr_type = CAMERA_SENSOR_I2C_TYPE_WORD;
			sensor_setting.data_type = CAMERA_SENSOR_I2C_TYPE_BYTE;
			sensor_setting.size = sensor_settings.imx766_setting.size;
			sensor_setting.delay = sensor_settings.imx766_setting.delay;
			rc = camera_io_dev_write(&(s_ctrl->io_master_info), &sensor_setting);
		}
		else if (s_ctrl->sensordata->slave_info.sensor_id == 0x682)
		{
			CAM_ERR(CAM_SENSOR, "FTM sensor setting 0x%x",s_ctrl->sensordata->slave_info.sensor_id);
			sensor_setting.reg_setting = sensor_settings.imx682_setting.reg_setting;
			sensor_setting.addr_type = CAMERA_SENSOR_I2C_TYPE_WORD;
			sensor_setting.data_type = CAMERA_SENSOR_I2C_TYPE_BYTE;
			sensor_setting.size = sensor_settings.imx682_setting.size;
			sensor_setting.delay = sensor_settings.imx682_setting.delay;
			rc = camera_io_dev_write(&(s_ctrl->io_master_info), &sensor_setting);
		}
		else if (s_ctrl->sensordata->slave_info.sensor_id == 0x709)
		{
			CAM_ERR(CAM_SENSOR, "FTM sensor setting 0x%x",s_ctrl->sensordata->slave_info.sensor_id);
			sensor_setting.reg_setting = sensor_settings.imx709_setting.reg_setting;
			sensor_setting.addr_type = CAMERA_SENSOR_I2C_TYPE_WORD;
			sensor_setting.data_type = CAMERA_SENSOR_I2C_TYPE_BYTE;
			sensor_setting.size = sensor_settings.imx709_setting.size;
			sensor_setting.delay = sensor_settings.imx709_setting.delay;
			rc = camera_io_dev_write(&(s_ctrl->io_master_info), &sensor_setting);
		}
		else if (s_ctrl->sensordata->slave_info.sensor_id == 0x3243)
		{
			oplus_shift_sensor_mode(s_ctrl);
			CAM_ERR(CAM_SENSOR, "FTM sensor setting 0x%x",s_ctrl->sensordata->slave_info.sensor_id);
			sensor_setting.reg_setting = sensor_settings.ov32c_setting.reg_setting;
			sensor_setting.addr_type = CAMERA_SENSOR_I2C_TYPE_WORD;
			sensor_setting.data_type = CAMERA_SENSOR_I2C_TYPE_BYTE;
			sensor_setting.size = sensor_settings.ov32c_setting.size;
			sensor_setting.delay = sensor_settings.ov32c_setting.delay;
			rc = camera_io_dev_write(&(s_ctrl->io_master_info), &sensor_setting);
		}
		else if (s_ctrl->sensordata->slave_info.sensor_id == 0x5647)
		{
			CAM_ERR(CAM_SENSOR, "FTM sensor setting 0x%x",s_ctrl->sensordata->slave_info.sensor_id);
			sensor_setting.reg_setting = sensor_settings.ov08d10_setting.reg_setting;
			sensor_setting.addr_type = CAMERA_SENSOR_I2C_TYPE_BYTE;
			sensor_setting.data_type = CAMERA_SENSOR_I2C_TYPE_BYTE;
			sensor_setting.size = sensor_settings.ov08d10_setting.size;
			sensor_setting.delay = sensor_settings.ov08d10_setting.delay;
			rc = camera_io_dev_write(&(s_ctrl->io_master_info), &sensor_setting);
		}
		else if (s_ctrl->sensordata->slave_info.sensor_id == 0x38e1) {
			CAM_ERR(CAM_SENSOR, "FTM sensor setting 0x%x",s_ctrl->sensordata->slave_info.sensor_id);
			sensor_setting.reg_setting = sensor_settings.s5kjn1sq03_setting.reg_setting;
			sensor_setting.addr_type = CAMERA_SENSOR_I2C_TYPE_WORD;
			sensor_setting.data_type = CAMERA_SENSOR_I2C_TYPE_WORD;
			sensor_setting.size = sensor_settings.s5kjn1sq03_setting.size;
			sensor_setting.delay = sensor_settings.s5kjn1sq03_setting.delay;
			rc = camera_io_dev_write(&(s_ctrl->io_master_info), &sensor_setting);
		}
		else if (s_ctrl->sensordata->slave_info.sensor_id == 0x5647) {
			CAM_ERR(CAM_SENSOR, "FTM sensor setting 0x%x",s_ctrl->sensordata->slave_info.sensor_id);
			sensor_setting.reg_setting = sensor_settings.ov08d_setting.reg_setting;
			sensor_setting.addr_type = CAMERA_SENSOR_I2C_TYPE_BYTE;
			sensor_setting.data_type = CAMERA_SENSOR_I2C_TYPE_BYTE;
			sensor_setting.size = sensor_settings.ov08d_setting.size;
			sensor_setting.delay = sensor_settings.ov08d_setting.delay;
			rc = camera_io_dev_write(&(s_ctrl->io_master_info), &sensor_setting);
		}
		else if (s_ctrl->sensordata->slave_info.sensor_id == 0x3109) {
			CAM_ERR(CAM_SENSOR, "FTM sensor setting 0x%x",s_ctrl->sensordata->slave_info.sensor_id);
			sensor_setting.reg_setting = sensor_settings.s5k3p9_setting.reg_setting;
			sensor_setting.addr_type = CAMERA_SENSOR_I2C_TYPE_WORD;
			sensor_setting.data_type = CAMERA_SENSOR_I2C_TYPE_WORD;
			sensor_setting.size = sensor_settings.s5k3p9_setting.size;
			sensor_setting.delay = sensor_settings.s5k3p9_setting.delay;
			rc = camera_io_dev_write(&(s_ctrl->io_master_info), &sensor_setting);
		}
		else if (s_ctrl->sensordata->slave_info.sensor_id == 0x6442) {
			CAM_ERR(CAM_SENSOR, "FTM sensor setting 0x%x",s_ctrl->sensordata->slave_info.sensor_id);
			sensor_setting.reg_setting = sensor_settings.Sec_ov64b_setting.reg_setting;
			sensor_setting.addr_type = CAMERA_SENSOR_I2C_TYPE_WORD;
			sensor_setting.data_type = CAMERA_SENSOR_I2C_TYPE_WORD;
			sensor_setting.size = sensor_settings.Sec_ov64b_setting.size;
			sensor_setting.delay = sensor_settings.Sec_ov64b_setting.delay;
			rc = camera_io_dev_write(&(s_ctrl->io_master_info), &sensor_setting);
		}
		else if (s_ctrl->sensordata->slave_info.sensor_id == 0x890)
		{
			CAM_ERR(CAM_SENSOR, "FTM sensor setting 0x%x",s_ctrl->sensordata->slave_info.sensor_id);
			sensor_setting.reg_setting = sensor_settings.imx890_setting.reg_setting;
			sensor_setting.addr_type = CAMERA_SENSOR_I2C_TYPE_WORD;
			sensor_setting.data_type = CAMERA_SENSOR_I2C_TYPE_BYTE;
			sensor_setting.size = sensor_settings.imx890_setting.size;
			sensor_setting.delay = sensor_settings.imx890_setting.delay;
			rc = camera_io_dev_write(&(s_ctrl->io_master_info), &sensor_setting);
		}
		else if (s_ctrl->sensordata->slave_info.sensor_id == 0x5664)
		{
			CAM_ERR(CAM_SENSOR, "FTM sensor setting 0x%x",s_ctrl->sensordata->slave_info.sensor_id);
			sensor_setting.reg_setting = sensor_settings.ov64b_setting.reg_setting;
			sensor_setting.addr_type = CAMERA_SENSOR_I2C_TYPE_WORD;
			sensor_setting.data_type = CAMERA_SENSOR_I2C_TYPE_BYTE;
			sensor_setting.size = sensor_settings.ov64b_setting.size;
			sensor_setting.delay = sensor_settings.ov64b_setting.delay;
			rc = camera_io_dev_write(&(s_ctrl->io_master_info), &sensor_setting);
		}
		else
		{
			CAM_ERR(CAM_SENSOR, "FTM unknown sensor id 0x%x",s_ctrl->sensordata->slave_info.sensor_id);
			rc = -1;
		}
		if (rc < 0)
		{
			CAM_ERR(CAM_SENSOR, "FTM Failed to write sensor setting");
			goto power_down;
		}
		else
		{
			CAM_ERR(CAM_SENSOR, "FTM successfully to write sensor setting");
		}
	}
	return rc;
power_down:
	CAM_ERR(CAM_SENSOR, "FTM wirte setting failed,do power down");
	cam_sensor_power_down(s_ctrl);
	return rc;
}

bool cam_ftm_if_do(void)
{
	CAM_DBG(CAM_SENSOR, "ftm state :%d",is_ftm_current_test);
	return is_ftm_current_test;
}

int32_t cam_sensor_update_id_info(struct cam_cmd_probe_v2 *probe_info,
	struct cam_sensor_ctrl_t *s_ctrl)
{
	int32_t rc = 0;

	s_ctrl->sensordata->id_info.sensor_slave_addr =
		probe_info->pipeline_delay;
	s_ctrl->sensordata->id_info.sensor_id_reg_addr =
		probe_info->reg_addr;
	s_ctrl->sensordata->id_info.sensor_id_mask =
		probe_info->data_mask;
	s_ctrl->sensordata->id_info.sensor_id =
		probe_info->expected_data;
	s_ctrl->sensordata->id_info.sensor_addr_type =
		probe_info->addr_type;
	s_ctrl->sensordata->id_info.sensor_data_type =
		probe_info->data_type;

	CAM_ERR(CAM_SENSOR,
		"vendor_slave_addr:  0x%x, vendor_id_Addr: 0x%x, bin vendorID: 0x%x, vendor_mask: 0x%x",
		s_ctrl->sensordata->id_info.sensor_slave_addr,
		s_ctrl->sensordata->id_info.sensor_id_reg_addr,
		s_ctrl->sensordata->id_info.sensor_id,
		s_ctrl->sensordata->id_info.sensor_id_mask);
	return rc;
}

int cam_sensor_match_id_oem(struct cam_sensor_ctrl_t *s_ctrl,uint32_t chip_id)
{
	uint32_t vendor_id = 0;
	int rc=0;

	if(chip_id == CAM_IMX709_SENSOR_ID){
		rc=camera_io_dev_read(
			&(s_ctrl->io_master_info),
			s_ctrl->sensordata->id_info.sensor_id_reg_addr,
			&vendor_id,s_ctrl->sensordata->id_info.sensor_addr_type,
			CAMERA_SENSOR_I2C_TYPE_BYTE);
		if(rc < 0)
		{
			usleep_range(1000, 1010);
			cam_sensor_power_down(s_ctrl);
			usleep_range(1000, 1010);
			cam_sensor_power_up(s_ctrl);
			rc=camera_io_dev_read(
				&(s_ctrl->io_master_info),
				s_ctrl->sensordata->id_info.sensor_id_reg_addr,
				&vendor_id,s_ctrl->sensordata->id_info.sensor_addr_type,
				CAMERA_SENSOR_I2C_TYPE_BYTE);
		}

		CAM_ERR(CAM_SENSOR, "read vendor_id_addr=0x%x module vendor_id: 0x%x, rc=%d",
			s_ctrl->sensordata->id_info.sensor_id_reg_addr,
			vendor_id,
			rc);

		/*if vendor id is 0 ,it is 0.90 module if vendor_id >= 1,it is 0.91 or 1.0 module*/
		if(vendor_id == 0){
			if(s_ctrl->sensordata->id_info.sensor_id == 0)
			{
				return 0;
			}
			else
			{
				return -1;
			}
		}
		else if(vendor_id >= 1)
		{
			if(s_ctrl->sensordata->id_info.sensor_id >= 1)
			{
				return 0;
			}
			else
			{
				return -1;
			}
		}
	}

	cam_sensor_read_qsc(s_ctrl);

	return 0;
}

bool cam_sensor_need_bypass_write(struct cam_sensor_ctrl_t *s_ctrl,struct i2c_settings_list *i2c_list)
{
	bool is_need_bypass = false;
	bool is_qsc_setting = true;
	int i = 0;
	if(s_ctrl->sensor_qsc_setting.enable_qsc_write_in_advance && s_ctrl->sensor_qsc_setting.read_qsc_success)
	{
		if(s_ctrl->sensor_qsc_setting.qsc_data_size == i2c_list->i2c_settings.size)
		{
			for(i=0;i<s_ctrl->sensor_qsc_setting.qsc_data_size;i++)
			{
				if(s_ctrl->sensor_qsc_setting.qsc_setting.reg_setting[i].reg_data != i2c_list->i2c_settings.reg_setting[i].reg_data)
				{
					is_qsc_setting = false;
					break;
				}
			}
		}
		else
		{
			is_qsc_setting = false;
		}

		CAM_DBG(CAM_SENSOR,"is_qsc_setting = %d",is_qsc_setting);

		if(is_qsc_setting)
		{
			if(s_ctrl->sensor_qsc_setting.sensor_qscsetting_state == CAM_SENSOR_SETTING_WRITE_INVALID)
			{
				CAM_INFO(CAM_SENSOR," qsc setting write failed before ,need write again");
				return is_need_bypass;
			}
			else
			{
				CAM_INFO(CAM_SENSOR," qsc setting have write  before , no need write again");
				is_need_bypass = true;
				return is_need_bypass;
			}
		}
		else
		{
			CAM_DBG(CAM_SENSOR,"not qsc setting");
			return is_need_bypass;
		}
	}
	else
	{
		CAM_DBG(CAM_SENSOR,"not need to compare");
		return is_need_bypass;
	}
}

int cam_sensor_read_qsc(struct cam_sensor_ctrl_t *s_ctrl)
{
	int rc=0;

	s_ctrl->sensor_qsc_setting.read_qsc_success = false;
	if(s_ctrl->sensor_qsc_setting.enable_qsc_write_in_advance)
	{
		uint8_t      *data;
		uint16_t     temp_slave_id = 0;
		int          i=0;

		data =  kzalloc(sizeof(uint8_t)*s_ctrl->sensor_qsc_setting.qsc_data_size, GFP_KERNEL);
		if(!data)
		{
			CAM_ERR(CAM_SENSOR,"kzalloc data failed");
			s_ctrl->sensor_qsc_setting.read_qsc_success = false;
			return rc;
		}
		temp_slave_id = s_ctrl->io_master_info.cci_client->sid;
		s_ctrl->io_master_info.cci_client->sid = (s_ctrl->sensor_qsc_setting.eeprom_slave_addr >> 1);
		rc = camera_io_dev_read_seq(&s_ctrl->io_master_info,
					s_ctrl->sensor_qsc_setting.qsc_reg_addr,data,
					CAMERA_SENSOR_I2C_TYPE_WORD,
					CAMERA_SENSOR_I2C_TYPE_BYTE,
					s_ctrl->sensor_qsc_setting.qsc_data_size);
		s_ctrl->io_master_info.cci_client->sid = temp_slave_id;
		if(rc)
		{
			CAM_ERR(CAM_SENSOR,"read qsc data failed");
			s_ctrl->sensor_qsc_setting.read_qsc_success = false;
			kfree(data);
			return rc;
		}

		if(s_ctrl->sensor_qsc_setting.qsc_setting.reg_setting == NULL)
		{
			s_ctrl->sensor_qsc_setting.qsc_setting.reg_setting = kzalloc(sizeof(struct cam_sensor_i2c_reg_array)*s_ctrl->sensor_qsc_setting.qsc_data_size, GFP_KERNEL);
			if (!s_ctrl->sensor_qsc_setting.qsc_setting.reg_setting)
			{
				CAM_ERR(CAM_SENSOR,"allocate qsc data failed");
				s_ctrl->sensor_qsc_setting.read_qsc_success = false;
				kfree(data);
				return rc;
			}
		}

		for(i = 0;i < s_ctrl->sensor_qsc_setting.qsc_data_size;i++)
		{
			s_ctrl->sensor_qsc_setting.qsc_setting.reg_setting[i].reg_addr = s_ctrl->sensor_qsc_setting.write_qsc_addr;
			s_ctrl->sensor_qsc_setting.qsc_setting.reg_setting[i].reg_data = *(data+i);
			s_ctrl->sensor_qsc_setting.qsc_setting.reg_setting[i].data_mask= 0x00;
			s_ctrl->sensor_qsc_setting.qsc_setting.reg_setting[i].delay    = 0x00;
			CAM_DBG(CAM_SENSOR,"read qsc data 0x%x i=%d",s_ctrl->sensor_qsc_setting.qsc_setting.reg_setting[i].reg_data,i);
		}

		s_ctrl->sensor_qsc_setting.qsc_setting.addr_type = CAMERA_SENSOR_I2C_TYPE_WORD;
		s_ctrl->sensor_qsc_setting.qsc_setting.data_type = CAMERA_SENSOR_I2C_TYPE_BYTE;
		s_ctrl->sensor_qsc_setting.qsc_setting.size = s_ctrl->sensor_qsc_setting.qsc_data_size;
		s_ctrl->sensor_qsc_setting.qsc_setting.delay = 1;

		s_ctrl->sensor_qsc_setting.read_qsc_success = true;
		kfree(data);
	}

	return 0;
}

int cam_get_second_provison_vendor_id(struct cam_sensor_ctrl_t *s_ctrl)
{
	int vendor_id,rc=0;
	if(s_ctrl->sensordata->slave_info.sensor_id == 0x841)
	{
		rc=camera_io_dev_read(&(s_ctrl->io_master_info),0x600f,&vendor_id, CAMERA_SENSOR_I2C_TYPE_WORD, CAMERA_SENSOR_I2C_TYPE_BYTE);
		CAM_ERR(CAM_SENSOR,"ov08a10 using vedor id:%d", vendor_id);
		if(vendor_id == 1)
		{
			CAM_ERR(CAM_SENSOR,"ov08a10 using first provision");
		}
		else if(vendor_id == 3)
		{
			CAM_ERR(CAM_SENSOR,"ov08a10 using second provision");
		}
	}
	return rc;
}
int oplus_shift_sensor_mode(struct cam_sensor_ctrl_t *s_ctrl)
{
	int rc=0;
	struct cam_camera_slave_info *slave_info;
	struct cam_sensor_i2c_reg_array ov32c_array;
	struct cam_sensor_i2c_reg_setting ov32c_array_write;

	slave_info = &(s_ctrl->sensordata->slave_info);

	if(slave_info->sensor_id == 0x3243)
	{
		CAM_ERR(CAM_SENSOR, "sid: %x", s_ctrl->io_master_info.cci_client->sid);
		s_ctrl->io_master_info.cci_client->sid = 0x30 >> 1;
		ov32c_array.reg_addr = 0x1001;
		ov32c_array.reg_data = 0x4;
		ov32c_array.delay = 0x00;
		ov32c_array.data_mask = 0x00;
		ov32c_array_write.reg_setting = &ov32c_array;
		ov32c_array_write.size = 1;
		ov32c_array_write.addr_type = CAMERA_SENSOR_I2C_TYPE_WORD;
		ov32c_array_write.data_type = CAMERA_SENSOR_I2C_TYPE_BYTE;
		ov32c_array_write.delay = 1;

		rc = camera_io_dev_write(&(s_ctrl->io_master_info),&ov32c_array_write);
		CAM_INFO(CAM_SENSOR, "write result %d", rc);
		mdelay(1);
		s_ctrl->io_master_info.cci_client->sid = 0x20 >> 1;
	}
	return rc;
}

uint32_t cam_override_chipid(struct cam_sensor_ctrl_t *s_ctrl)
{
	int rc = 0;
	uint32_t ee_vcmid = 0 ;
	uint32_t chipid = 0 ;

	struct cam_sensor_cci_client ee_cci_client;
	struct cam_camera_slave_info *slave_info;

	const uint8_t IMX766_EEPROM_SID = (s_ctrl->is_read_eeprom == 2? 0xa0>>1:0xa2>>1);
	const uint8_t IMX766_EEPROM_VCMID_ADDR = 0x11;
	const uint8_t IMX766_FIRST_SOURCE_VCMID = 0xff;
	const uint8_t IMX766_SECOND_SOURCE_VCMID = 0x00;
	const uint32_t IMX766_FIRST_SOURCE_CHIPID = 0x766F;
	const uint32_t IMX766_SECOND_SOURCE_CHIPID = 0x766E;

	struct cam_sensor_cci_client ee_cci_client_day;
	uint32_t IMX766_EEPROM_PRODUCE_DAY = 0x00;
	uint32_t IMX766_EEPROM_PRODUCE_MONTH = 0x00;
	uint32_t IMX766_EEPROM_PRODUCE_YEAR = 0x00;
	uint32_t IMX766_EEPROM_PRODUCE_YEAR1 = 0x00;
	const uint8_t IMX766_EEPROM_PRODUCE_DAY_ADDR = 0x02;
	const uint8_t IMX766_EEPROM_PRODUCE_MONTH_ADDR = 0x03;
	const uint8_t IMX766_EEPROM_PRODUCE_YEAR_ADDR = 0x04;
	const uint8_t IMX766_EEPROM_PRODUCE_YEAR1_ADDR = 0x05;

	slave_info = &(s_ctrl->sensordata->slave_info);

	if (!slave_info)
	{
		CAM_ERR(CAM_SENSOR, " failed: %pK",
			 slave_info);
		return -EINVAL;
	}

	if (slave_info->sensor_id == 0x0766)
	{
		memcpy(&ee_cci_client_day, s_ctrl->io_master_info.cci_client,
			sizeof(struct cam_sensor_cci_client));
		ee_cci_client_day.sid = IMX766_EEPROM_SID;
		rc = cam_cci_i2c_read(&ee_cci_client_day,
			IMX766_EEPROM_PRODUCE_DAY_ADDR,
			&IMX766_EEPROM_PRODUCE_DAY,
			CAMERA_SENSOR_I2C_TYPE_WORD,
			CAMERA_SENSOR_I2C_TYPE_BYTE);
		rc = cam_cci_i2c_read(&ee_cci_client_day,
			IMX766_EEPROM_PRODUCE_MONTH_ADDR,
			&IMX766_EEPROM_PRODUCE_MONTH,
			CAMERA_SENSOR_I2C_TYPE_WORD,
			CAMERA_SENSOR_I2C_TYPE_BYTE);
		rc = cam_cci_i2c_read(&ee_cci_client_day,
			IMX766_EEPROM_PRODUCE_YEAR_ADDR,
			&IMX766_EEPROM_PRODUCE_YEAR,
			CAMERA_SENSOR_I2C_TYPE_WORD,
			CAMERA_SENSOR_I2C_TYPE_BYTE);
		rc = cam_cci_i2c_read(&ee_cci_client_day,
			IMX766_EEPROM_PRODUCE_YEAR1_ADDR,
			&IMX766_EEPROM_PRODUCE_YEAR1,
			CAMERA_SENSOR_I2C_TYPE_WORD,
			CAMERA_SENSOR_I2C_TYPE_BYTE);
		CAM_ERR(CAM_SENSOR, "The module produce day:%d%d/%d/%d",
			IMX766_EEPROM_PRODUCE_YEAR,
			IMX766_EEPROM_PRODUCE_YEAR1,
			IMX766_EEPROM_PRODUCE_MONTH,
			IMX766_EEPROM_PRODUCE_DAY);
	}

	oplus_shift_sensor_mode(s_ctrl);

	if (slave_info->sensor_id == IMX766_FIRST_SOURCE_CHIPID || \
		slave_info->sensor_id == IMX766_SECOND_SOURCE_CHIPID)
	{
		memcpy(&ee_cci_client, s_ctrl->io_master_info.cci_client,
			sizeof(struct cam_sensor_cci_client));
		ee_cci_client.sid = IMX766_EEPROM_SID;
		rc = cam_cci_i2c_read(&ee_cci_client,
			IMX766_EEPROM_VCMID_ADDR,
			&ee_vcmid,
			CAMERA_SENSOR_I2C_TYPE_WORD,
			CAMERA_SENSOR_I2C_TYPE_BYTE);

		CAM_ERR(CAM_SENSOR, "distinguish imx766 camera module, vcm id : 0x%x ",ee_vcmid);
		if (IMX766_FIRST_SOURCE_VCMID == ee_vcmid)
		{
			chipid = IMX766_FIRST_SOURCE_CHIPID;
		}
		else if (IMX766_SECOND_SOURCE_VCMID == ee_vcmid)
		{
			chipid = IMX766_SECOND_SOURCE_CHIPID;
		}
		else
		{
			chipid = IMX766_FIRST_SOURCE_CHIPID;
		}
	}
	else
	{
		rc = camera_io_dev_read(
			&(s_ctrl->io_master_info),
			slave_info->sensor_id_reg_addr,
			&chipid, CAMERA_SENSOR_I2C_TYPE_WORD,
			CAMERA_SENSOR_I2C_TYPE_WORD);
		if(rc < 0)
		{
			usleep_range(1000, 1010);
			cam_sensor_power_down(s_ctrl);
			usleep_range(1000, 1010);
			cam_sensor_power_up(s_ctrl);
			rc = camera_io_dev_read(
			     &(s_ctrl->io_master_info),
			     slave_info->sensor_id_reg_addr,
			     &chipid, CAMERA_SENSOR_I2C_TYPE_WORD,
			     CAMERA_SENSOR_I2C_TYPE_WORD);
               }
	}

	return chipid;
}

void cam_sensor_get_dt_data(struct cam_sensor_ctrl_t *s_ctrl)
{
		int32_t rc = 0;
		struct device_node *of_node = s_ctrl->of_node;
		rc = of_property_read_u32(of_node, "is-read-eeprom",&s_ctrl->is_read_eeprom);
		if ( rc < 0)
		{
			CAM_DBG(CAM_SENSOR, "Invalid sensor params");
			s_ctrl->is_read_eeprom = 0;
		}
		rc = of_property_read_u32(of_node, "is_power_up_advance",&s_ctrl->power_up_advance);
		if ( rc < 0)
		{
			CAM_ERR(CAM_SENSOR, "Invalid sensor params");
			s_ctrl->power_up_advance = 0;
		}
		rc = of_property_read_u32(of_node, "is_get_second_provision",&s_ctrl->get_second_provision);
		if ( rc < 0)
		{
			CAM_DBG(CAM_SENSOR, "Invalid sensor params");
			s_ctrl->get_second_provision = 0;
		}

		rc = of_property_read_u32(of_node, "enable_qsc_write_in_advance",&s_ctrl->sensor_qsc_setting.enable_qsc_write_in_advance);
		if ( rc < 0)
		{
			CAM_DBG(CAM_SENSOR, "Invalid sensor params");
			s_ctrl->sensor_qsc_setting.enable_qsc_write_in_advance = 0;
		}
		else
		{
		    CAM_INFO(CAM_SENSOR, "enable_qsc_write_in_advance = %d",s_ctrl->sensor_qsc_setting.enable_qsc_write_in_advance);
		}

		rc = of_property_read_u32(of_node, "qsc_reg_addr",&s_ctrl->sensor_qsc_setting.qsc_reg_addr);
		if ( rc < 0)
		{
			CAM_DBG(CAM_SENSOR, "Invalid sensor params");
			s_ctrl->sensor_qsc_setting.qsc_reg_addr = 0;
		}
		else
		{
		    CAM_INFO(CAM_SENSOR, "qsc_reg_addr = 0x%x",s_ctrl->sensor_qsc_setting.qsc_reg_addr);
		}

		rc = of_property_read_u32(of_node, "eeprom_slave_addr",&s_ctrl->sensor_qsc_setting.eeprom_slave_addr);
		if ( rc < 0)
		{
			CAM_DBG(CAM_SENSOR, "Invalid sensor params");
			s_ctrl->sensor_qsc_setting.eeprom_slave_addr = 0;
		}
		else
		{
			CAM_INFO(CAM_SENSOR, "eeprom_slave_addr = 0x%x",s_ctrl->sensor_qsc_setting.eeprom_slave_addr);
		}

		rc = of_property_read_u32(of_node, "qsc_data_size",&s_ctrl->sensor_qsc_setting.qsc_data_size);
		if ( rc < 0)
		{
			CAM_DBG(CAM_SENSOR, "Invalid sensor params");
			s_ctrl->sensor_qsc_setting.qsc_data_size = 0;
		}
		else
		{
			CAM_INFO(CAM_SENSOR, "qsc_data_size = %d",s_ctrl->sensor_qsc_setting.qsc_data_size);
		}

		rc = of_property_read_u32(of_node, "write_qsc_addr",&s_ctrl->sensor_qsc_setting.write_qsc_addr);
		if ( rc < 0)
		{
			CAM_DBG(CAM_SENSOR, "Invalid sensor params");
			s_ctrl->sensor_qsc_setting.write_qsc_addr = 0;
		}
		else
		{
			CAM_INFO(CAM_SENSOR, "write_qsc_addr = 0x%x",s_ctrl->sensor_qsc_setting.write_qsc_addr);
		}

}

int oplus_cam_sensor_update_setting(struct cam_sensor_ctrl_t *s_ctrl)
{
		int rc=0;
		struct cam_sensor_cci_client ee_cci_client;
		uint32_t sensor_version = 0;
		uint32_t qsc_address = 0;
		struct cam_camera_slave_info *slave_info;
		slave_info = &(s_ctrl->sensordata->slave_info);

		if (slave_info->sensor_id == 0x0766 || slave_info->sensor_id == 0x0766E ||
			slave_info->sensor_id == 0x0766F || slave_info->sensor_id == 0x0890)
		{
			memcpy(&ee_cci_client, s_ctrl->io_master_info.cci_client,
					sizeof(struct cam_sensor_cci_client));
				if (s_ctrl->is_read_eeprom == 1)
				{
 					ee_cci_client.sid = 0xA2 >> 1;
					qsc_address = 0x2BD0;
				}
				else if (s_ctrl->is_read_eeprom == 2)
				{
					ee_cci_client.sid = 0xA0 >> 1;
					qsc_address = 0x2A32;
				}
				else if (s_ctrl->is_read_eeprom == 3)
				{
					ee_cci_client.sid = 0xA2 >> 1;
					qsc_address = 0x2A32;
				}
				else if (s_ctrl->is_read_eeprom == 0)
				{
					CAM_INFO(CAM_SENSOR, "no need to update qsc tool");
					return rc;
				}

				rc = cam_cci_i2c_read(&ee_cci_client,
						qsc_address,
						&sensor_version, CAMERA_SENSOR_I2C_TYPE_WORD,
						CAMERA_SENSOR_I2C_TYPE_BYTE);
				CAM_INFO(CAM_SENSOR, "QSC tool version is %x",
							sensor_version);
				if (sensor_version == 0x03)
				{
					struct cam_sensor_i2c_reg_array qsc_tool = {
 								.reg_addr = 0x86A9,
								.reg_data = 0x4E,
								.delay = 0x00,
								.data_mask = 0x00,
 						};
					struct cam_sensor_i2c_reg_setting qsc_tool_write = {
								.reg_setting = &qsc_tool,
								.size = 1,
								.addr_type = CAMERA_SENSOR_I2C_TYPE_WORD,
								.data_type = CAMERA_SENSOR_I2C_TYPE_BYTE,
								.delay = 0x00,
						};
						rc = camera_io_dev_write(&(s_ctrl->io_master_info), &qsc_tool_write);
						CAM_INFO(CAM_SENSOR, "update the qsc tool version %d", rc);
				}
		}
		oplus_shift_sensor_mode(s_ctrl);
		return rc;
}

int sensor_start_thread(void *arg)
{
	struct cam_sensor_ctrl_t *s_ctrl = (struct cam_sensor_ctrl_t *)arg;
	int rc = 0;
	struct cam_sensor_i2c_reg_setting sensor_init_setting;

	if (!s_ctrl)
	{
		CAM_ERR(CAM_SENSOR, "s_ctrl is NULL");
		return -1;
	}
	trace_begin("%s %d do sensor power up and write initsetting",s_ctrl->sensor_name, s_ctrl->sensordata->slave_info.sensor_id);

	mutex_lock(&(s_ctrl->cam_sensor_mutex));

	//power up for sensor
	mutex_lock(&(s_ctrl->sensor_power_state_mutex));
	if(s_ctrl->sensor_power_state == CAM_SENSOR_POWER_OFF)
	{
		rc = cam_sensor_power_up(s_ctrl);
		if(rc < 0) {
			CAM_ERR(CAM_SENSOR, "sensor power up faild!");
		}
		else
		{
			CAM_INFO(CAM_SENSOR, "sensor power up success sensor id 0x%x",s_ctrl->sensordata->slave_info.sensor_id);
			s_ctrl->sensor_power_state = CAM_SENSOR_POWER_ON;
			s_ctrl->sensor_initsetting_state = CAM_SENSOR_SETTING_WRITE_INVALID;
			s_ctrl->sensor_qsc_setting.sensor_qscsetting_state = CAM_SENSOR_SETTING_WRITE_INVALID;
		}
    }
	else
	{
		CAM_INFO(CAM_SENSOR, "sensor have power up!");
	}
	mutex_unlock(&(s_ctrl->sensor_power_state_mutex));

	//write initsetting for sensor
	if (rc == 0)
	{
		mutex_lock(&(s_ctrl->sensor_initsetting_mutex));
		if(s_ctrl->sensor_initsetting_state == CAM_SENSOR_SETTING_WRITE_INVALID)
		{
			if(s_ctrl->sensordata->slave_info.sensor_id == 0x789)
			{
				sensor_init_setting.reg_setting = sensor_init_settings.imx789_setting.reg_setting;
				sensor_init_setting.addr_type = CAMERA_SENSOR_I2C_TYPE_WORD;
				sensor_init_setting.data_type = CAMERA_SENSOR_I2C_TYPE_BYTE;
				sensor_init_setting.size = sensor_init_settings.imx789_setting.size;
				sensor_init_setting.delay = sensor_init_settings.imx789_setting.delay;
				rc = camera_io_dev_write(&(s_ctrl->io_master_info), &sensor_init_setting);
				if(rc < 0)
				{
					CAM_ERR(CAM_SENSOR, "write setting failed!");
				}
				else
				{
					CAM_INFO(CAM_SENSOR, "write setting1 success!");
					s_ctrl->sensor_initsetting_state = CAM_SENSOR_SETTING_WRITE_SUCCESS;
				}
			}
			else if(s_ctrl->sensordata->slave_info.sensor_id == 0x0615)
			{
				sensor_init_setting.reg_setting = sensor_init_settings.imx615_setting.reg_setting;
				sensor_init_setting.addr_type = CAMERA_SENSOR_I2C_TYPE_WORD;
				sensor_init_setting.data_type = CAMERA_SENSOR_I2C_TYPE_BYTE;
				sensor_init_setting.size = sensor_init_settings.imx615_setting.size;
				sensor_init_setting.delay = sensor_init_settings.imx615_setting.delay;
				rc = camera_io_dev_write(&(s_ctrl->io_master_info), &sensor_init_setting);
				if(rc < 0)
				{
					CAM_ERR(CAM_SENSOR, "write setting failed!");
				}
				else
				{
					CAM_INFO(CAM_SENSOR, "write setting1 success!");
					s_ctrl->sensor_initsetting_state = CAM_SENSOR_SETTING_WRITE_SUCCESS;
				}
			}
			else if(s_ctrl->sensordata->slave_info.sensor_id == 0x0766)
			{
				if(s_ctrl->power_up_advance == 1)
				{
					sensor_init_setting.reg_setting = sensor_init_settings.imx766_ferrari_setting.reg_setting;
					sensor_init_setting.addr_type = CAMERA_SENSOR_I2C_TYPE_WORD;
					sensor_init_setting.data_type = CAMERA_SENSOR_I2C_TYPE_BYTE;
					sensor_init_setting.size = sensor_init_settings.imx766_ferrari_setting.size;
					sensor_init_setting.delay = sensor_init_settings.imx766_ferrari_setting.delay;
					rc = camera_io_dev_write(&(s_ctrl->io_master_info), &sensor_init_setting);
					if(rc < 0)
					{
						CAM_ERR(CAM_SENSOR, "write 766 setting failed!");
					}
					else
					{
						CAM_INFO(CAM_SENSOR, "write 766 setting success!");
						s_ctrl->sensor_initsetting_state = CAM_SENSOR_SETTING_WRITE_SUCCESS;
					}
				}
				else
				{
					sensor_init_setting.reg_setting = sensor_init_settings.imx766_setting.reg_setting;
					sensor_init_setting.addr_type = CAMERA_SENSOR_I2C_TYPE_WORD;
					sensor_init_setting.data_type = CAMERA_SENSOR_I2C_TYPE_BYTE;
					sensor_init_setting.size = sensor_init_settings.imx766_setting.size;
					sensor_init_setting.delay = sensor_init_settings.imx766_setting.delay;
					rc = camera_io_dev_write(&(s_ctrl->io_master_info), &sensor_init_setting);
					if(rc < 0)
					{
						CAM_ERR(CAM_SENSOR, "write 766 setting failed!");
					}
					else
					{
						CAM_INFO(CAM_SENSOR, "write 766 setting success!");
						s_ctrl->sensor_initsetting_state = CAM_SENSOR_SETTING_WRITE_SUCCESS;
					}
				}
			}
			else if(s_ctrl->sensordata->slave_info.sensor_id == 0x0890)
			{
				if (s_ctrl->power_up_advance == 1)
				{
					sensor_init_setting.reg_setting = sensor_init_settings.imx890_lz_setting.reg_setting;
					sensor_init_setting.addr_type = CAMERA_SENSOR_I2C_TYPE_WORD;
					sensor_init_setting.data_type = CAMERA_SENSOR_I2C_TYPE_BYTE;
					sensor_init_setting.size = sensor_init_settings.imx890_lz_setting.size;
					sensor_init_setting.delay = sensor_init_settings.imx890_lz_setting.delay;
					rc = camera_io_dev_write(&(s_ctrl->io_master_info), &sensor_init_setting);
					if(rc < 0)
					{
						CAM_ERR(CAM_SENSOR, "write 890 setting failed!");
					}
					else
					{
						CAM_INFO(CAM_SENSOR, "write 890 setting success!");
						s_ctrl->sensor_initsetting_state = CAM_SENSOR_SETTING_WRITE_SUCCESS;
					}
					if(rc == 0 && s_ctrl->sensor_qsc_setting.enable_qsc_write_in_advance && s_ctrl->sensor_qsc_setting.read_qsc_success)
					{
						oplus_cam_sensor_update_setting(s_ctrl);
						rc = camera_io_dev_write_continuous(&(s_ctrl->io_master_info),&(s_ctrl->sensor_qsc_setting.qsc_setting),CAM_SENSOR_I2C_WRITE_SEQ);
						if(rc < 0)
						{
							CAM_ERR(CAM_SENSOR, "write 890 qsc failed!");
						}
						else
						{
							CAM_INFO(CAM_SENSOR, "write 890 qsc success!");
							s_ctrl->sensor_qsc_setting.sensor_qscsetting_state = CAM_SENSOR_SETTING_WRITE_SUCCESS;
						}
					}
				}
				else if (s_ctrl->power_up_advance == 2)
				{
					sensor_init_setting.reg_setting = sensor_init_settings.imx890_senna_setting.reg_setting;
					sensor_init_setting.addr_type = CAMERA_SENSOR_I2C_TYPE_WORD;
					sensor_init_setting.data_type = CAMERA_SENSOR_I2C_TYPE_BYTE;
					sensor_init_setting.size = sensor_init_settings.imx890_senna_setting.size;
					sensor_init_setting.delay = sensor_init_settings.imx890_senna_setting.delay;
					rc = camera_io_dev_write(&(s_ctrl->io_master_info), &sensor_init_setting);
					if(rc < 0)
					{
						CAM_ERR(CAM_SENSOR, "write 890 setting failed!");
					}
					else
					{
						CAM_INFO(CAM_SENSOR, "write 890 setting success!");
						s_ctrl->sensor_initsetting_state = CAM_SENSOR_SETTING_WRITE_SUCCESS;
					}
					if(rc == 0 && s_ctrl->sensor_qsc_setting.enable_qsc_write_in_advance && s_ctrl->sensor_qsc_setting.read_qsc_success)
					{
						oplus_cam_sensor_update_setting(s_ctrl);
						rc = camera_io_dev_write_continuous(&(s_ctrl->io_master_info),&(s_ctrl->sensor_qsc_setting.qsc_setting),CAM_SENSOR_I2C_WRITE_SEQ);
						if(rc < 0)
						{
							CAM_ERR(CAM_SENSOR, "write 890 qsc failed!");
						}
						else
						{
							CAM_INFO(CAM_SENSOR, "write 890 qsc success!");
							s_ctrl->sensor_qsc_setting.sensor_qscsetting_state = CAM_SENSOR_SETTING_WRITE_SUCCESS;
						}
					}
				}
			        else if (s_ctrl->power_up_advance == 3)
				{
					sensor_init_setting.reg_setting = sensor_init_settings.imx890_monroe_setting.reg_setting;
					sensor_init_setting.addr_type = CAMERA_SENSOR_I2C_TYPE_WORD;
					sensor_init_setting.data_type = CAMERA_SENSOR_I2C_TYPE_BYTE;
					sensor_init_setting.size = sensor_init_settings.imx890_monroe_setting.size;
					sensor_init_setting.delay = sensor_init_settings.imx890_monroe_setting.delay;
					rc = camera_io_dev_write(&(s_ctrl->io_master_info), &sensor_init_setting);
					if(rc < 0)
					{
						CAM_ERR(CAM_SENSOR, "write monroe 890 setting failed!");
					}
					else
					{
						CAM_INFO(CAM_SENSOR, "write monroe 890 setting success!");
						s_ctrl->sensor_initsetting_state = CAM_SENSOR_SETTING_WRITE_SUCCESS;
					}
					if(rc == 0 && s_ctrl->sensor_qsc_setting.enable_qsc_write_in_advance && s_ctrl->sensor_qsc_setting.read_qsc_success)
					{
						oplus_cam_sensor_update_setting(s_ctrl);
						rc = camera_io_dev_write_continuous(&(s_ctrl->io_master_info),&(s_ctrl->sensor_qsc_setting.qsc_setting),CAM_SENSOR_I2C_WRITE_SEQ);
						if(rc < 0)
						{
							CAM_ERR(CAM_SENSOR, "write 890 qsc failed!");
						}
						else
						{
							CAM_INFO(CAM_SENSOR, "write 890 qsc success!");
							s_ctrl->sensor_qsc_setting.sensor_qscsetting_state = CAM_SENSOR_SETTING_WRITE_SUCCESS;
						}
					}
				}
				else
				{
					sensor_init_setting.reg_setting = sensor_init_settings.imx890_setting.reg_setting;
					sensor_init_setting.addr_type = CAMERA_SENSOR_I2C_TYPE_WORD;
					sensor_init_setting.data_type = CAMERA_SENSOR_I2C_TYPE_BYTE;
					sensor_init_setting.size = sensor_init_settings.imx890_setting.size;
					sensor_init_setting.delay = sensor_init_settings.imx890_setting.delay;
					rc = camera_io_dev_write(&(s_ctrl->io_master_info), &sensor_init_setting);
					if(rc < 0)
					{
						CAM_ERR(CAM_SENSOR, "write 890 setting failed!");
					}
					else
					{
						CAM_INFO(CAM_SENSOR, "write 890 setting success!");
						s_ctrl->sensor_initsetting_state = CAM_SENSOR_SETTING_WRITE_SUCCESS;
					}
				}
			}
			else if(s_ctrl->sensordata->slave_info.sensor_id == 0x5647)
			{
				sensor_init_setting.reg_setting = sensor_init_settings.ov08d10_setting.reg_setting;
				sensor_init_setting.addr_type = CAMERA_SENSOR_I2C_TYPE_BYTE;
				sensor_init_setting.data_type = CAMERA_SENSOR_I2C_TYPE_BYTE;
				sensor_init_setting.size = sensor_init_settings.ov08d10_setting.size;
				sensor_init_setting.delay = sensor_init_settings.ov08d10_setting.delay;
				rc = camera_io_dev_write(&(s_ctrl->io_master_info), &sensor_init_setting);
				if(rc < 0)
				{
					CAM_ERR(CAM_SENSOR, "write ov08d10 setting failed!");
				}
				else
				{
					CAM_INFO(CAM_SENSOR, "write ov08d10 setting success!");
					s_ctrl->sensor_initsetting_state = CAM_SENSOR_SETTING_WRITE_SUCCESS;
				}
			}
			else if(s_ctrl->sensordata->slave_info.sensor_id == 0x3109)
			{
				sensor_init_setting.reg_setting = sensor_init_settings.s5k3p9_setting.reg_setting;
				sensor_init_setting.addr_type = CAMERA_SENSOR_I2C_TYPE_WORD;
				sensor_init_setting.data_type = CAMERA_SENSOR_I2C_TYPE_WORD;
				sensor_init_setting.size  = sensor_init_settings.s5k3p9_setting.size;
				sensor_init_setting.delay = sensor_init_settings.s5k3p9_setting.delay;
				rc = camera_io_dev_write(&(s_ctrl->io_master_info), &sensor_init_setting);

				sensor_init_setting.reg_setting = sensor_init_settings.s5k3p9_setting1.reg_setting;
				sensor_init_setting.size  = sensor_init_settings.s5k3p9_setting1.size;
				sensor_init_setting.delay = sensor_init_settings.s5k3p9_setting1.delay;
				rc = camera_io_dev_write_continuous(&(s_ctrl->io_master_info), &sensor_init_setting, CAM_SENSOR_I2C_WRITE_BURST);

				sensor_init_setting.reg_setting = sensor_init_settings.s5k3p9_setting2.reg_setting;
				sensor_init_setting.size  = sensor_init_settings.s5k3p9_setting2.size;
				sensor_init_setting.delay = sensor_init_settings.s5k3p9_setting2.delay;
				rc = camera_io_dev_write(&(s_ctrl->io_master_info), &sensor_init_setting);
				if(rc < 0)
				{
					CAM_ERR(CAM_SENSOR, "write s5k3p9 setting failed!");
				}
				else
				{
					CAM_INFO(CAM_SENSOR, "write s5k3p9 setting success!");
					s_ctrl->sensor_initsetting_state = CAM_SENSOR_SETTING_WRITE_SUCCESS;
				}
			}
			else if(s_ctrl->sensordata->slave_info.sensor_id == 0x3243)
			{
				oplus_shift_sensor_mode(s_ctrl);
				sensor_init_setting.reg_setting = sensor_init_settings.ov32c_setting.reg_setting;
				sensor_init_setting.addr_type = CAMERA_SENSOR_I2C_TYPE_WORD;
				sensor_init_setting.data_type = CAMERA_SENSOR_I2C_TYPE_BYTE;
				sensor_init_setting.size = sensor_init_settings.ov32c_setting.size;
				sensor_init_setting.delay = sensor_init_settings.ov32c_setting.delay;
				rc = camera_io_dev_write(&(s_ctrl->io_master_info), &sensor_init_setting);

				sensor_init_setting.reg_setting = sensor_init_settings.ov32c_setting1.reg_setting;
				sensor_init_setting.addr_type = CAMERA_SENSOR_I2C_TYPE_WORD;
				sensor_init_setting.data_type = CAMERA_SENSOR_I2C_TYPE_BYTE;
				sensor_init_setting.size = sensor_init_settings.ov32c_setting1.size;
				sensor_init_setting.delay = sensor_init_settings.ov32c_setting1.delay;
				rc = camera_io_dev_write_continuous(&(s_ctrl->io_master_info), &sensor_init_setting,CAM_SENSOR_I2C_WRITE_SEQ);

				sensor_init_setting.reg_setting = sensor_init_settings.ov32c_setting2.reg_setting;
				sensor_init_setting.addr_type = CAMERA_SENSOR_I2C_TYPE_WORD;
				sensor_init_setting.data_type = CAMERA_SENSOR_I2C_TYPE_BYTE;
				sensor_init_setting.size = sensor_init_settings.ov32c_setting2.size;
				sensor_init_setting.delay = sensor_init_settings.ov32c_setting2.delay;
				rc = camera_io_dev_write(&(s_ctrl->io_master_info), &sensor_init_setting);
				if(rc < 0)
				{
					CAM_ERR(CAM_SENSOR, "write ov32c setting failed!");
				}
				else
				{
					CAM_INFO(CAM_SENSOR, "write ov32c setting success!");
					s_ctrl->sensor_initsetting_state = CAM_SENSOR_SETTING_WRITE_SUCCESS;
				}
			}
			else if(s_ctrl->sensordata->slave_info.sensor_id == 0x0709)
			{
				if (s_ctrl->power_up_advance == 1)
				{
					sensor_init_setting.reg_setting = sensor_init_settings.imx709_tele_setting.reg_setting;
					sensor_init_setting.addr_type = CAMERA_SENSOR_I2C_TYPE_WORD;
					sensor_init_setting.data_type = CAMERA_SENSOR_I2C_TYPE_BYTE;
					sensor_init_setting.size = sensor_init_settings.imx709_tele_setting.size;
					sensor_init_setting.delay = sensor_init_settings.imx709_tele_setting.delay;
					rc = camera_io_dev_write(&(s_ctrl->io_master_info), &sensor_init_setting);
					if(rc < 0)
					{
						CAM_ERR(CAM_SENSOR, "write 709 tele setting failed!");
					}
					else
					{
						CAM_INFO(CAM_SENSOR, "write 709 tele setting success!");
						s_ctrl->sensor_initsetting_state = CAM_SENSOR_SETTING_WRITE_SUCCESS;
					}
				}
				else if (s_ctrl->power_up_advance == 2)
				{
					sensor_init_setting.reg_setting = sensor_init_settings.imx709_daoxiang_setting.reg_setting;
					sensor_init_setting.addr_type = CAMERA_SENSOR_I2C_TYPE_WORD;
					sensor_init_setting.data_type = CAMERA_SENSOR_I2C_TYPE_BYTE;
					sensor_init_setting.size = sensor_init_settings.imx709_daoxiang_setting.size;
					sensor_init_setting.delay = sensor_init_settings.imx709_daoxiang_setting.delay;
					rc = camera_io_dev_write(&(s_ctrl->io_master_info), &sensor_init_setting);
					if(rc < 0)
					{
						CAM_ERR(CAM_SENSOR, "write 709 daoxiang front setting failed!");
					}
					else
					{
						CAM_INFO(CAM_SENSOR, "write 709 daoxiang front setting success!");
						s_ctrl->sensor_initsetting_state = CAM_SENSOR_SETTING_WRITE_SUCCESS;
					}
				}
				else
				{
					sensor_init_setting.reg_setting = sensor_init_settings.imx709_setting.reg_setting;
					sensor_init_setting.addr_type = CAMERA_SENSOR_I2C_TYPE_WORD;
					sensor_init_setting.data_type = CAMERA_SENSOR_I2C_TYPE_BYTE;
					sensor_init_setting.size = sensor_init_settings.imx709_setting.size;
					sensor_init_setting.delay = sensor_init_settings.imx709_setting.delay;
					rc = camera_io_dev_write(&(s_ctrl->io_master_info), &sensor_init_setting);
					if(rc < 0)
					{
						CAM_ERR(CAM_SENSOR, "write 709 setting failed!");
					}
					else
					{
						CAM_INFO(CAM_SENSOR, "write 709 setting success!");
						s_ctrl->sensor_initsetting_state = CAM_SENSOR_SETTING_WRITE_SUCCESS;
					}
				}
			}
			else if(s_ctrl->sensordata->slave_info.sensor_id == 0x5664)
			{
				sensor_init_setting.reg_setting = sensor_init_settings.ov64b_senna_setting.reg_setting;
				sensor_init_setting.addr_type = CAMERA_SENSOR_I2C_TYPE_WORD;
				sensor_init_setting.data_type = CAMERA_SENSOR_I2C_TYPE_BYTE;
				sensor_init_setting.size = sensor_init_settings.ov64b_senna_setting.size;
				sensor_init_setting.delay = sensor_init_settings.ov64b_senna_setting.delay;
				rc = camera_io_dev_write(&(s_ctrl->io_master_info), &sensor_init_setting);
				if(rc < 0)
				{
					CAM_INFO(CAM_SENSOR, "write ov64b setting failed!");
				}
				else
				{
					CAM_INFO(CAM_SENSOR, "write ov64b setting success!");
					s_ctrl->sensor_initsetting_state = CAM_SENSOR_SETTING_WRITE_SUCCESS;
				}
			}
			else if(s_ctrl->sensordata->slave_info.sensor_id == 0x6442)
			{
				sensor_init_setting.reg_setting = sensor_init_settings.ov64b_monroe_setting.reg_setting;
				sensor_init_setting.addr_type = CAMERA_SENSOR_I2C_TYPE_WORD;
				sensor_init_setting.data_type = CAMERA_SENSOR_I2C_TYPE_BYTE;
				sensor_init_setting.size = sensor_init_settings.ov64b_monroe_setting.size;
				sensor_init_setting.delay = sensor_init_settings.ov64b_monroe_setting.delay;
				rc = camera_io_dev_write(&(s_ctrl->io_master_info), &sensor_init_setting);
				if(rc < 0)
				{
					CAM_INFO(CAM_SENSOR, "write monroe ov64b setting failed!");
				}
				else
				{
					CAM_INFO(CAM_SENSOR, "write monroe ov64b setting success!");
					s_ctrl->sensor_initsetting_state = CAM_SENSOR_SETTING_WRITE_SUCCESS;
				}
			}

		}
		else
		{
			CAM_INFO(CAM_SENSOR, "sensor setting have write!");
		}
		mutex_unlock(&(s_ctrl->sensor_initsetting_mutex));
	}

	mutex_unlock(&(s_ctrl->cam_sensor_mutex));
	trace_end();
	return rc;
}

int cam_sensor_start(struct cam_sensor_ctrl_t *s_ctrl)
{
	int rc = 0;

	if(s_ctrl == NULL)
	{
		CAM_ERR(CAM_SENSOR, "s_ctrl is null ");
		return -1;
	}

	mutex_lock(&(s_ctrl->cam_sensor_mutex));

	mutex_lock(&(s_ctrl->sensor_power_state_mutex));
	if(s_ctrl->sensor_power_state == CAM_SENSOR_POWER_OFF)
	{
		s_ctrl->sensor_open_thread = kthread_run(sensor_start_thread, s_ctrl, s_ctrl->device_name);
		if (!s_ctrl->sensor_open_thread)
		{
			CAM_ERR(CAM_SENSOR, "create sensor start thread failed");
			rc = -1;
		}
		else
		{
			CAM_INFO(CAM_SENSOR, "create sensor start thread success");
		}
	}
	else
	{
		CAM_INFO(CAM_SENSOR, "sensor have power up");
	}
	mutex_unlock(&(s_ctrl->sensor_power_state_mutex));

	mutex_unlock(&(s_ctrl->cam_sensor_mutex));
	return rc;
}

int cam_sensor_stop(struct cam_sensor_ctrl_t *s_ctrl)
{
	int rc = 0;
	struct cam_sensor_i2c_reg_setting sensor_init_setting;

	CAM_ERR(CAM_SENSOR,"sensor do stop");
	mutex_lock(&(s_ctrl->cam_sensor_mutex));

	//power off for sensor
	mutex_lock(&(s_ctrl->sensor_power_state_mutex));
	if(s_ctrl->sensor_power_state == CAM_SENSOR_POWER_ON)
	{
		if(s_ctrl->sensordata->slave_info.sensor_id == 0x3109)
		{
			sensor_init_setting.reg_setting = sensor_init_settings.s5k3p9_streamoff_setting.reg_setting;
			sensor_init_setting.addr_type = CAMERA_SENSOR_I2C_TYPE_WORD;
			sensor_init_setting.data_type = CAMERA_SENSOR_I2C_TYPE_WORD;
			sensor_init_setting.size  = sensor_init_settings.s5k3p9_streamoff_setting.size;
			sensor_init_setting.delay = sensor_init_settings.s5k3p9_streamoff_setting.delay;
			rc = camera_io_dev_write(&(s_ctrl->io_master_info), &sensor_init_setting);
			if(rc < 0)
			{
				CAM_ERR(CAM_SENSOR, "write setting failed!");
			}
			check_streamoff(s_ctrl);
		}
		rc = cam_sensor_power_down(s_ctrl);
		if(rc < 0)
		{
			CAM_ERR(CAM_SENSOR, "sensor power down faild!");
		}
		else
		{
			CAM_INFO(CAM_SENSOR, "sensor power down success sensor id 0x%x",s_ctrl->sensordata->slave_info.sensor_id);
			s_ctrl->sensor_power_state = CAM_SENSOR_POWER_OFF;
			mutex_lock(&(s_ctrl->sensor_initsetting_mutex));
			s_ctrl->sensor_initsetting_state = CAM_SENSOR_SETTING_WRITE_INVALID;
			s_ctrl->sensor_qsc_setting.sensor_qscsetting_state = CAM_SENSOR_SETTING_WRITE_INVALID;
			mutex_unlock(&(s_ctrl->sensor_initsetting_mutex));
		}
	}
	else
	{
		CAM_INFO(CAM_SENSOR, "sensor have power down!");
		s_ctrl->sensor_power_state = CAM_SENSOR_POWER_OFF;
		mutex_lock(&(s_ctrl->sensor_initsetting_mutex));
		s_ctrl->sensor_initsetting_state = CAM_SENSOR_SETTING_WRITE_INVALID;
		s_ctrl->sensor_qsc_setting.sensor_qscsetting_state = CAM_SENSOR_SETTING_WRITE_INVALID;
		mutex_unlock(&(s_ctrl->sensor_initsetting_mutex));
	}
	mutex_unlock(&(s_ctrl->sensor_power_state_mutex));

	mutex_unlock(&(s_ctrl->cam_sensor_mutex));
	return rc;
}

int32_t oplus_cam_sensor_driver_cmd(struct cam_sensor_ctrl_t *s_ctrl,
	void *arg)
{
	int rc = 0;
	struct cam_control *cmd = (struct cam_control *)arg;
	switch (cmd->op_code) {
		case CAM_OEM_GET_ID : {
			if (copy_to_user((void __user *)cmd->handle,&s_ctrl->soc_info.index,
								sizeof(uint32_t))) {
				CAM_ERR(CAM_SENSOR,
						"copy camera id to user fail ");
			}
			break;
		case CAM_GET_DPC_DATA:
			CAM_INFO(CAM_SENSOR, "sony_dfct_tbl: fd_dfct_num=%d, sg_dfct_num=%d",
				sony_dfct_tbl.fd_dfct_num, sony_dfct_tbl.sg_dfct_num);
			if (copy_to_user((void __user *) cmd->handle, &sony_dfct_tbl,
				sizeof(struct  sony_dfct_tbl_t))) {
				CAM_ERR(CAM_SENSOR, "Failed Copy to User");
				rc = -EFAULT;
				return rc;
			}
			break;
		}
	}
	return rc;
}

int oplus_sensor_sony_get_dpc_data(struct cam_sensor_ctrl_t *s_ctrl)
{
    int i = 0, j = 0;
    int rc = 0;
    int check_reg_val, dfct_data_h, dfct_data_l;
    int dfct_data = 0;
    int fd_dfct_num = 0, sg_dfct_num = 0;
    int retry_cnt = 5;
    int data_h = 0, data_v = 0;
    int fd_dfct_addr = FD_DFCT_ADDR;
    int sg_dfct_addr = SG_DFCT_ADDR;

    CAM_INFO(CAM_SENSOR, "oplus_sensor_sony_get_dpc_data enter");
    if (s_ctrl == NULL) {
        CAM_ERR(CAM_SENSOR, "Invalid Args");
        return -EINVAL;
    }

    memset(&sony_dfct_tbl, 0, sizeof(struct sony_dfct_tbl_t));

    for (i = 0; i < retry_cnt; i++) {
        check_reg_val = 0;
        rc = camera_io_dev_read(&(s_ctrl->io_master_info),
            FD_DFCT_NUM_ADDR, &check_reg_val,
            CAMERA_SENSOR_I2C_TYPE_WORD,
            CAMERA_SENSOR_I2C_TYPE_BYTE);

        if (0 == rc) {
            fd_dfct_num = check_reg_val & 0x07;
            if (fd_dfct_num > FD_DFCT_MAX_NUM)
                fd_dfct_num = FD_DFCT_MAX_NUM;
            break;
        }
    }

    for (i = 0; i < retry_cnt; i++) {
        check_reg_val = 0;
        rc = camera_io_dev_read(&(s_ctrl->io_master_info),
            SG_DFCT_NUM_ADDR, &check_reg_val,
            CAMERA_SENSOR_I2C_TYPE_WORD,
            CAMERA_SENSOR_I2C_TYPE_WORD);

        if (0 == rc) {
            sg_dfct_num = check_reg_val & 0x01FF;
            if (sg_dfct_num > SG_DFCT_MAX_NUM)
                sg_dfct_num = SG_DFCT_MAX_NUM;
            break;
        }
    }

    CAM_INFO(CAM_SENSOR, " fd_dfct_num = %d, sg_dfct_num = %d", fd_dfct_num, sg_dfct_num);
    sony_dfct_tbl.fd_dfct_num = fd_dfct_num;
    sony_dfct_tbl.sg_dfct_num = sg_dfct_num;

    if (fd_dfct_num > 0) {
        for (j = 0; j < fd_dfct_num; j++) {
            dfct_data = 0;
            for (i = 0; i < retry_cnt; i++) {
                dfct_data_h = 0;
                rc = camera_io_dev_read(&(s_ctrl->io_master_info),
                        fd_dfct_addr, &dfct_data_h,
                        CAMERA_SENSOR_I2C_TYPE_WORD,
                        CAMERA_SENSOR_I2C_TYPE_WORD);
                if (0 == rc) {
                    break;
                }
            }
            for (i = 0; i < retry_cnt; i++) {
                dfct_data_l = 0;
                rc = camera_io_dev_read(&(s_ctrl->io_master_info),
                        fd_dfct_addr+2, &dfct_data_l,
                        CAMERA_SENSOR_I2C_TYPE_WORD,
                        CAMERA_SENSOR_I2C_TYPE_WORD);
                if (0 == rc) {
                    break;
                }
            }
            CAM_DBG(CAM_SENSOR, " dfct_data_h = 0x%x, dfct_data_l = 0x%x", dfct_data_h, dfct_data_l);
            dfct_data = (dfct_data_h << 16) | dfct_data_l;
            data_h = 0;
            data_v = 0;
            data_h = (dfct_data & (H_DATA_MASK >> j%8)) >> (19 - j%8); //19 = 32 -13;
            data_v = (dfct_data & (V_DATA_MASK >> j%8)) >> (7 - j%8);  // 7 = 32 -13 -12;
            CAM_DBG(CAM_SENSOR, "j = %d, H = %d, V = %d", j, data_h, data_v);
            sony_dfct_tbl.fd_dfct_addr[j] = ((data_h & 0x1FFF) << V_ADDR_SHIFT) | (data_v & 0x0FFF);
            CAM_DBG(CAM_SENSOR, "fd_dfct_data[%d] = 0x%08x", j, sony_dfct_tbl.fd_dfct_addr[j]);
            fd_dfct_addr = fd_dfct_addr + 3 + ((j+1)%8 == 0);
        }
    }
    if (sg_dfct_num > 0) {
        for (j = 0; j < sg_dfct_num; j++) {
            dfct_data = 0;
            for (i = 0; i < retry_cnt; i++) {
                dfct_data_h = 0;
                rc = camera_io_dev_read(&(s_ctrl->io_master_info),
                        sg_dfct_addr, &dfct_data_h,
                        CAMERA_SENSOR_I2C_TYPE_WORD,
                        CAMERA_SENSOR_I2C_TYPE_WORD);
                if (0 == rc) {
                    break;
                }
            }
            for (i = 0; i < retry_cnt; i++) {
                dfct_data_l = 0;
                rc = camera_io_dev_read(&(s_ctrl->io_master_info),
                        sg_dfct_addr+2, &dfct_data_l,
                        CAMERA_SENSOR_I2C_TYPE_WORD,
                        CAMERA_SENSOR_I2C_TYPE_WORD);
                if (0 == rc) {
                    break;
                }
            }
            CAM_DBG(CAM_SENSOR, " dfct_data_h = 0x%x, dfct_data_l = 0x%x", dfct_data_h, dfct_data_l);
            dfct_data = (dfct_data_h << 16) | dfct_data_l;
            data_h = 0;
            data_v = 0;
            data_h = (dfct_data & (H_DATA_MASK >> j%8)) >> (19 - j%8); //19 = 32 -13;
            data_v = (dfct_data & (V_DATA_MASK >> j%8)) >> (7 - j%8);  // 7 = 32 -13 -12;
            CAM_DBG(CAM_SENSOR, "j = %d, H = %d, V = %d", j, data_h, data_v);
            sony_dfct_tbl.sg_dfct_addr[j] = ((data_h & 0x1FFF) << V_ADDR_SHIFT) | (data_v & 0x0FFF);
            CAM_DBG(CAM_SENSOR, "sg_dfct_data[%d] = 0x%08x", j, sony_dfct_tbl.sg_dfct_addr[j]);
            sg_dfct_addr = sg_dfct_addr + 3 + ((j+1)%8 == 0);
        }
    }

    CAM_INFO(CAM_SENSOR, "exit");
    return rc;
}

int oplus_cam_get_sensor_temp(struct cam_sensor_ctrl_t *s_ctrl, int32_t *sensor_temp)
{
    int rc = 0, sensor_temp_enable = 0;
    struct cam_camera_slave_info *slave_info;

    struct cam_sensor_i2c_reg_array i2c_write_setting = {
	.reg_addr = 0x138,
	.reg_data = 0x1,
	.delay = 0x00,
	.data_mask = 0x00,
    };
    struct cam_sensor_i2c_reg_setting i2c_write = {
	.reg_setting = &i2c_write_setting,
	.size = 1,
	.addr_type = CAMERA_SENSOR_I2C_TYPE_WORD,
	.data_type = CAMERA_SENSOR_I2C_TYPE_BYTE,
	.delay = 0x00,
    };

    slave_info = &(s_ctrl->sensordata->slave_info);
    if (!slave_info) {
	CAM_ERR(CAM_SENSOR, " failed: %pK", slave_info);
	return -EINVAL;
    }

    rc = camera_io_dev_read(
	&(s_ctrl->io_master_info),
	0x138,
	&sensor_temp_enable, CAMERA_SENSOR_I2C_TYPE_WORD,
	CAMERA_SENSOR_I2C_TYPE_BYTE);
    if (rc < 0) {
	CAM_ERR(CAM_SENSOR, "failed to read sensor temperature enable flag, rc=%d.",rc);
	return -EINVAL;
    }

    if (!sensor_temp_enable) {
	rc = camera_io_dev_write(&(s_ctrl->io_master_info), &i2c_write);
	if(rc < 0) {
	    CAM_ERR(CAM_SENSOR, "failed to write sensor temperature enable setting, rc=%d.",rc);
	    return -EINVAL;
	} else {
	    CAM_ERR(CAM_SENSOR, "successfully to write sensor temperature enable setting");
	    sensor_temp_enable = 1;
	}
    }

    rc = camera_io_dev_read(
	&(s_ctrl->io_master_info),
	0x13A,
	sensor_temp, CAMERA_SENSOR_I2C_TYPE_WORD,
	CAMERA_SENSOR_I2C_TYPE_BYTE);
    if (rc < 0) {
	CAM_ERR(CAM_SENSOR, "failed to read sensor temperature, rc=%d.",rc);
	return -EINVAL;
    } else {
    CAM_DBG(CAM_SENSOR, "sensor temperature enable 0x%x read sensor temperature: 0x%x sensor id 0x%x ",
	sensor_temp_enable, *sensor_temp, slave_info->sensor_id);
    }

    return rc;
}

void check_streamoff(struct cam_sensor_ctrl_t *s_ctrl)
{
	unsigned int i = 0, stream_off_val = 0;
	int timeout = 100;

	for (i = 0; i < timeout; i++)
	{
		camera_io_dev_read(&(s_ctrl->io_master_info),0x0005, &stream_off_val,CAMERA_SENSOR_I2C_TYPE_WORD,CAMERA_SENSOR_I2C_TYPE_BYTE);
		if (stream_off_val == 0xFF)
		{
			return;
		}
		usleep_range(1000, 1010);
	}
	CAM_ERR(CAM_SENSOR," Stream Off Fail1!\n");
}


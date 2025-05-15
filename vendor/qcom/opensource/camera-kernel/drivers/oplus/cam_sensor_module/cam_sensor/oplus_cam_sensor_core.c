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

bool is_ftm_current_test = false;

struct cam_sensor_i2c_reg_setting sensor_setting;

struct cam_sensor_settings sensor_settings =
{
#include "cam_sensor_settings.h"
};

struct cam_sensor_settings sensor_init_settings = {
#include "cam_sensor_initsettings.h"
};

bool chip_version_old = FALSE;

int cam_ftm_power_down(struct cam_sensor_ctrl_t *s_ctrl)
{
	int rc = 0;
	CAM_ERR(CAM_SENSOR,"FTM stream off");
	if (s_ctrl->sensordata->slave_info.sensor_id == 0x586 ||
		s_ctrl->sensordata->slave_info.sensor_id == 0x30d5||
		s_ctrl->sensordata->slave_info.sensor_id == 0x471 ||
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
		s_ctrl->sensordata->slave_info.sensor_id == 0x38e1||
		s_ctrl->sensordata->slave_info.sensor_id == 0xe000)
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
	if (s_ctrl->sensordata->slave_info.sensor_id == 0x586)
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
		else if (s_ctrl->sensordata->slave_info.sensor_id == 0x38e1) {
			CAM_ERR(CAM_SENSOR, "FTM sensor setting 0x%x",s_ctrl->sensordata->slave_info.sensor_id);
			sensor_setting.reg_setting = sensor_settings.s5kjn1sq03_setting.reg_setting;
			sensor_setting.addr_type = CAMERA_SENSOR_I2C_TYPE_WORD;
			sensor_setting.data_type = CAMERA_SENSOR_I2C_TYPE_WORD;
			sensor_setting.size = sensor_settings.s5kjn1sq03_setting.size;
			sensor_setting.delay = sensor_settings.s5kjn1sq03_setting.delay;
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

	rc=camera_io_dev_read(
		&(s_ctrl->io_master_info),
		s_ctrl->sensordata->id_info.sensor_id_reg_addr,
		&vendor_id,s_ctrl->sensordata->id_info.sensor_addr_type,
		CAMERA_SENSOR_I2C_TYPE_BYTE);
	CAM_ERR(CAM_SENSOR, "read vendor_id_addr=0x%x module vendor_id: 0x%x, rc=%d",
		s_ctrl->sensordata->id_info.sensor_id_reg_addr,
		vendor_id,
		rc);
	if(chip_id == CAM_IMX709_SENSOR_ID){
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
	return 0;
}

uint32_t cam_override_chipid(struct cam_sensor_ctrl_t *s_ctrl)
{
	int rc = 0;
	uint32_t ee_vcmid = 0 ;
	uint32_t chipid = 0 ;

	struct cam_sensor_cci_client ee_cci_client;
	struct cam_camera_slave_info *slave_info;

	const uint8_t IMX766_EEPROM_SID = (0xA2 >> 1);
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
			slave_info->sensor_id == 0x0766F)
		{
			memcpy(&ee_cci_client, s_ctrl->io_master_info.cci_client,
					sizeof(struct cam_sensor_cci_client));
				if(s_ctrl->is_read_eeprom == 1)
				{
 					ee_cci_client.sid = 0xA2 >> 1;
					qsc_address = 0x2BD0;
				}else if(s_ctrl->is_read_eeprom == 2)
				{
					ee_cci_client.sid = 0xA0 >> 1;
					qsc_address = 0x2A32;
				}else if(s_ctrl->is_read_eeprom == 0)
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
	CAM_ERR(CAM_SENSOR,"sensor do stop");
	mutex_lock(&(s_ctrl->cam_sensor_mutex));

	//power off for sensor
	mutex_lock(&(s_ctrl->sensor_power_state_mutex));
	if(s_ctrl->sensor_power_state == CAM_SENSOR_POWER_ON)
	{
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
			mutex_unlock(&(s_ctrl->sensor_initsetting_mutex));
		}
	}
	else
	{
		CAM_INFO(CAM_SENSOR, "sensor have power down!");
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
		}
	}
	return rc;
}



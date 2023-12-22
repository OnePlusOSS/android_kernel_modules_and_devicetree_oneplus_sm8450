#include <linux/module.h>
#include "cam_sensor_cmn_header.h"
#include "cam_actuator_core.h"
#include "cam_sensor_util.h"
#include "cam_trace.h"
#include "cam_common_util.h"
#include "cam_packet_util.h"

#include "oplus_cam_actuator_core.h"
#include "oplus_cam_actuator_dev.h"

#define ACTUATOR_REGISTER_SIZE 100
#define DW9718P_DAC_ADDR 0x03
#define DW9718P_BUSY_ADDR 0x05

uint32_t DW9817P[ACTUATOR_REGISTER_SIZE][2] = {
	{0x02, 0x01},
	{0x02, 0x00},
	{0x02, 0x02},
	{0x02, 0x00},
	{0x06, 0x40},
	{0x07, 0x39},
	{0xff, 0xff},
};

uint32_t DW9817P_EEPROM[ACTUATOR_REGISTER_SIZE][2] = {
	{0x0092, 0x00},
	{0x0093, 0x00},
	{0x0094, 0x00},
	{0x0095, 0x00},
	{0x0096, 0x00},
	{0x0097, 0x00},
	{0xffff, 0xff},
};

uint32_t DW9718P_PARKLENS_UP[ACTUATOR_REGISTER_SIZE][2] = {
	{0x03, 0x00c8},
	{0x03, 0x0108},
	{0x03, 0x0148},
	{0x03, 0x0188},
	{0x03, 0x01c8},
	{0xff, 0xff},
};

uint32_t DW9718P_PARKLENS_DOWN[ACTUATOR_REGISTER_SIZE][2] = {
	{0x03, 0x0168},
	{0x03, 0x0103},
	{0x03, 0x00c8},
	{0x03, 0x00a0},
	{0x03, 0x0090},
	{0x03, 0x0080},
	{0x03, 0x0070},
	{0x03, 0x0060},
	{0x03, 0x0050},
	{0x03, 0x0040},
	{0x03, 0x0030},
	{0x03, 0x0020},
	{0x03, 0x0010},
	{0xff, 0xff},
};

#define DW9827C_DAC_ADDR 0x84

uint32_t DW9827C_PARKLENS_UP[ACTUATOR_REGISTER_SIZE][2] = {
    {0xff, 0xff},
    {0x00, 0x0A},
    {0x00, 0x19},
    {0x00, 0x21},
    {0x00, 0x29},
    {0x00, 0x31},
    {0x00, 0x39},
    {0xff, 0xff},
};

uint32_t DW9827C_PARKLENS_DOWN[ACTUATOR_REGISTER_SIZE][2] = {
	{0x00, 0x4B},
	{0x00, 0x2D},
	{0x00, 0x19},
	{0x00, 0x14},
	{0x00, 0x10},
	{0x00, 0x0A},
	{0x00, 0x06},
	{0x00, 0x04},
	{0x00, 0x02},
	{0xff, 0xff},
};

#define SEM1217S_DAC_ADDR 0x0206

uint32_t SEM1217S_PARKLENS_UP[ACTUATOR_REGISTER_SIZE][2] = {
    {0xff, 0xff},
    {0x0204, 0x0200},
    {0x0204, 0x0600},
    {0x0204, 0x0A00},
    {0x0204, 0x1000},
    {0x0204, 0x1400},
    {0x0204, 0x1900},
    {0xff, 0xff},
};

uint32_t SEM1217S_PARKLENS_DOWN[ACTUATOR_REGISTER_SIZE][2] = {
	{0x0204, 0x3FFF},
	{0x0204, 0x2D00},
	{0x0204, 0x1900},
	{0x0204, 0x1400},
	{0x0204, 0x1000},
	{0x0204, 0x0A00},
	{0x0204, 0x0600},
	{0x0204, 0x0400},
	{0x0204, 0x0200},
	{0xff, 0xff},
};

int actuator_power_down_thread(void *arg)
{
	int rc = 0;
	int i;
	uint32_t read_val = 0;
	uint32_t is_actuator_busy = 0;
	uint32_t busy_count = 0;
	struct cam_actuator_ctrl_t *a_ctrl = (struct cam_actuator_ctrl_t *)arg;
	struct cam_actuator_soc_private *soc_private;
	struct cam_sensor_power_ctrl_t *power_info;

	msleep(5);
	if (!a_ctrl) {
		CAM_ERR(CAM_ACTUATOR, "failed: a_ctrl %pK", a_ctrl);
		return -EINVAL;
	}

	soc_private = (struct cam_actuator_soc_private *)a_ctrl->soc_info.soc_private;
	if (!soc_private) {
		CAM_ERR(CAM_ACTUATOR, "failed:soc_private %pK", soc_private);
		return -EINVAL;
	}
	else{
		power_info  = &soc_private->power_info;
		if (!power_info){
			CAM_ERR(CAM_ACTUATOR, "failed: power_info %pK", a_ctrl, power_info);
			return -EINVAL;
		}
	}

	if(strstr(a_ctrl->aon_af_name, "dw9718p"))
	{
		CAM_INFO(CAM_ACTUATOR,"actuator_power_down_thread start ");
		read_val = oplus_cam_actuator_read_vaule(a_ctrl, DW9718P_DAC_ADDR);

		for (i = 0; i < ACTUATOR_REGISTER_SIZE; i++) {
			if ((DW9718P_PARKLENS_DOWN[i][0] != 0xff) && (DW9718P_PARKLENS_DOWN[i][1] != 0xff)) {
				if(read_val >= DW9718P_PARKLENS_DOWN[i][1]) {
					break;
				}
			} else {
				break;
			}
		}

		for (; i < ACTUATOR_REGISTER_SIZE; i++)
		{
			if ( (DW9718P_PARKLENS_DOWN[i][0] != 0xff) && (DW9718P_PARKLENS_DOWN[i][1] != 0xff) )
			{
				while(busy_count < 10) {
					rc = camera_io_dev_read(&(a_ctrl->io_master_info),
								DW9718P_BUSY_ADDR,
								&is_actuator_busy,
								CAMERA_SENSOR_I2C_TYPE_BYTE,
								CAMERA_SENSOR_I2C_TYPE_BYTE);
					if(rc < 0) {
						CAM_ERR(CAM_ACTUATOR,"read failed ret : %d", rc);
						goto err_handle;
					}
					CAM_INFO(CAM_ACTUATOR,"read DW9718P_BUSY_ADDR data : %d busy_count ：%d", is_actuator_busy ,busy_count);
					if(is_actuator_busy == 0) {
						break;
					}
					msleep(1);
					busy_count++;
				}
				busy_count = 0;
				rc = cam_actuator_ramwrite(a_ctrl,
					(uint32_t)DW9718P_PARKLENS_DOWN[i][0],
					(uint32_t)DW9718P_PARKLENS_DOWN[i][1],
					1,
					CAMERA_SENSOR_I2C_TYPE_BYTE,
					CAMERA_SENSOR_I2C_TYPE_WORD);
				if(rc < 0)
				{
					CAM_ERR(CAM_ACTUATOR,"write failed ret : %d", rc);
					goto err_handle;
				}
				msleep(2);
			}
			else
			{
				CAM_INFO(CAM_ACTUATOR,"set parklens success ");
				break;
			}
			mutex_lock(&(a_ctrl->actuator_parklens_mutex));
			if (a_ctrl->actuator_power_down_thread_exit) {
				mutex_unlock(&(a_ctrl->actuator_parklens_mutex));
				CAM_ERR(CAM_ACTUATOR,"actuator_power_down_thread break actuator_power_down_thread_exit: %d", a_ctrl->actuator_power_down_thread_exit);
				break;
			}
			mutex_unlock(&(a_ctrl->actuator_parklens_mutex));
		}
	}else if(strstr(a_ctrl->aon_af_name, "dw9827c")){

		CAM_INFO(CAM_ACTUATOR,"actuator_power_down_thread start ");
		read_val = oplus_cam_actuator_read_vaule(a_ctrl, DW9827C_DAC_ADDR);
		for (i = 0; i < ACTUATOR_REGISTER_SIZE; i++) {
			if ((DW9827C_PARKLENS_DOWN[i][0] != 0xff) && (DW9827C_PARKLENS_DOWN[i][1] != 0xff)) {
				if((read_val >> 8) >= DW9827C_PARKLENS_DOWN[i][1]) {
					break;
				}
			} else {
				break;
			}
		}

		for (; i < ACTUATOR_REGISTER_SIZE; i++)
		{
			if ( (DW9827C_PARKLENS_DOWN[i][0] != 0xff) && (DW9827C_PARKLENS_DOWN[i][1] != 0xff) )
			{
				rc = cam_actuator_ramwrite(a_ctrl,
					(uint32_t)DW9827C_PARKLENS_DOWN[i][0],
					(uint32_t)DW9827C_PARKLENS_DOWN[i][1],
					1,
					CAMERA_SENSOR_I2C_TYPE_BYTE,
					CAMERA_SENSOR_I2C_TYPE_BYTE);
				if(rc < 0)
				{
					CAM_ERR(CAM_ACTUATOR,"write failed ret : %d", rc);
					goto err_handle;
				}
				msleep(1);
			}
			else
			{
				CAM_INFO(CAM_ACTUATOR,"set parklens success ");
				break;
			}

			mutex_lock(&(a_ctrl->actuator_parklens_mutex));
			if (a_ctrl->actuator_power_down_thread_exit) {
				mutex_unlock(&(a_ctrl->actuator_parklens_mutex));
				CAM_ERR(CAM_ACTUATOR,"actuator_power_down_thread break actuator_power_down_thread_exit: %d", a_ctrl->actuator_power_down_thread_exit);
				break;
			}
			mutex_unlock(&(a_ctrl->actuator_parklens_mutex));
		}
	}
	else
	{
		CAM_INFO(CAM_ACTUATOR,"not support this actuator ");
	}

err_handle:
	mutex_lock(&(a_ctrl->actuator_parklens_mutex));
	if ((!a_ctrl->actuator_power_down_thread_exit) && (a_ctrl->cam_atc_power_state == CAM_ACTUATOR_POWER_ON)) {
		rc = cam_actuator_power_down(a_ctrl);
  		if (rc < 0) {
			CAM_ERR(CAM_ACTUATOR, "Actuator Power down failed");
  		} else {
			kfree(power_info->power_setting);
			kfree(power_info->power_down_setting);
			power_info->power_setting = NULL;
			power_info->power_down_setting = NULL;
			power_info->power_setting_size = 0;
			power_info->power_down_setting_size = 0;
			a_ctrl->cam_atc_power_state = CAM_ACTUATOR_POWER_OFF;
  		}
	} else {
		CAM_ERR(CAM_ACTUATOR, "No need to do power down, actuator_power_down_thread_exit %d, actuator_power_state %d",a_ctrl->actuator_power_down_thread_exit, a_ctrl->cam_atc_power_state);
	}
	a_ctrl->actuator_power_down_thread_exit = true;
	CAM_INFO(CAM_ACTUATOR, "actuator_power_down_thread exit");
	mutex_unlock(&(a_ctrl->actuator_parklens_mutex));
	return rc;
}

int actuator_power_down_second_thread(void *arg)
{
	int rc = 0;
	int i;
	uint32_t read_val = 0;
	uint32_t write_val = 0;
	struct cam_actuator_ctrl_t *a_ctrl = (struct cam_actuator_ctrl_t *)arg;
	struct cam_actuator_soc_private *soc_private;
	struct cam_sensor_power_ctrl_t *power_info;

	msleep(5);
	if (!a_ctrl) {
		CAM_ERR(CAM_ACTUATOR, "failed: a_ctrl %pK", a_ctrl);
		return -EINVAL;
	}

	soc_private = (struct cam_actuator_soc_private *)a_ctrl->soc_info.soc_private;
	if (!soc_private) {
		CAM_ERR(CAM_ACTUATOR, "failed:soc_private %pK", soc_private);
		return -EINVAL;
	}
	else{
		power_info  = &soc_private->power_info;
		if (!power_info){
			CAM_ERR(CAM_ACTUATOR, "failed: power_info %pK", a_ctrl, power_info);
			return -EINVAL;
		}
	}

	/*tele af powerdown*/
	camera_io_dev_read(&(a_ctrl->io_master_info), SEM1217S_DAC_ADDR, &read_val,
					CAMERA_SENSOR_I2C_TYPE_WORD, CAMERA_SENSOR_I2C_TYPE_WORD);
	read_val = (((read_val & 0x00FF) << 8) | ((read_val & 0xFF00) >> 8));
	CAM_INFO(CAM_ACTUATOR,"read success addr = 0x%x data=0x%x", SEM1217S_DAC_ADDR, read_val);
	for (i = 0; i < ACTUATOR_REGISTER_SIZE; i++) {
		if ((SEM1217S_PARKLENS_DOWN[i][0] != 0xff) && (SEM1217S_PARKLENS_DOWN[i][1] != 0xff)) {
			if(read_val >= SEM1217S_PARKLENS_DOWN[i][1])
		    {
				break;
			}
		} else {
			break;
		}
	}

	for (; i < ACTUATOR_REGISTER_SIZE; i++)
	{
		if ( (SEM1217S_PARKLENS_DOWN[i][0] != 0xff) && (SEM1217S_PARKLENS_DOWN[i][1] != 0xff) )
		{
			write_val = (SEM1217S_PARKLENS_DOWN[i][1] & 0xFF); /* Set AF Target : Low Byte */
			rc = cam_actuator_ramwrite(a_ctrl,
				(uint32_t)SEM1217S_PARKLENS_DOWN[i][0],
				write_val,
				1,
				CAMERA_SENSOR_I2C_TYPE_WORD,
				CAMERA_SENSOR_I2C_TYPE_BYTE);

			write_val = ( (SEM1217S_PARKLENS_DOWN[i][1] >> 8) & 0xFF ); /* Set AF Position : High Byte */
			rc = cam_actuator_ramwrite(a_ctrl,
				((uint32_t)SEM1217S_PARKLENS_DOWN[i][0] + 1),
				write_val,
				1,
				CAMERA_SENSOR_I2C_TYPE_WORD,
				CAMERA_SENSOR_I2C_TYPE_BYTE);
			if(rc < 0)
			{
				CAM_ERR(CAM_ACTUATOR,"write failed ret : %d", rc);
				goto err_handle;
			}
			msleep(15);
		}
		else
		{
			CAM_INFO(CAM_ACTUATOR,"set parklens success ");
			break;
		}

		mutex_lock(&(a_ctrl->actuator_parklens_second_mutex));
		if (a_ctrl->actuator_power_down_second_thread_exit) {
			mutex_unlock(&(a_ctrl->actuator_parklens_second_mutex));
			CAM_ERR(CAM_ACTUATOR,"sem1217s actuator_power_down_second_thread_exit break actuator_power_down_thread_exit: %d", a_ctrl->actuator_power_down_second_thread_exit);
			break;
		}
		mutex_unlock(&(a_ctrl->actuator_parklens_second_mutex));
	}

err_handle:
	mutex_lock(&(a_ctrl->actuator_parklens_second_mutex));
	if ((!a_ctrl->actuator_power_down_second_thread_exit) && (a_ctrl->cam_atc_power_second_state == CAM_ACTUATOR_POWER_ON)) {
		rc = cam_actuator_power_down(a_ctrl);
        if (rc < 0) {
			CAM_ERR(CAM_ACTUATOR, "Actuator Power down failed");
        } else {
			kfree(power_info->power_setting);
			kfree(power_info->power_down_setting);
			power_info->power_setting = NULL;
			power_info->power_down_setting = NULL;
			power_info->power_setting_size = 0;
			power_info->power_down_setting_size = 0;
			a_ctrl->cam_atc_power_second_state = CAM_ACTUATOR_POWER_OFF;
        }
	} else {
		CAM_ERR(CAM_ACTUATOR, "No need to do power down, actuator_power_down_thread_exit %d, actuator_power_state %d",a_ctrl->actuator_power_down_second_thread_exit, a_ctrl->cam_atc_power_second_state);
	}
	a_ctrl->actuator_power_down_second_thread_exit = true;
	CAM_INFO(CAM_ACTUATOR, "actuator_power_down_second_thread_exit exit");
	mutex_unlock(&(a_ctrl->actuator_parklens_second_mutex));
	return rc;
}

int actuator_power_up_parklens_thread(void *arg)
{
	int rc = 0;
	int i;
	uint32_t read_val = 0;
	uint32_t is_actuator_busy = 0;
	uint32_t busy_count = 0;
	struct cam_actuator_ctrl_t *a_ctrl = (struct cam_actuator_ctrl_t *)arg;
	struct cam_actuator_soc_private *soc_private;
	struct cam_sensor_power_ctrl_t *power_info;

	msleep(5);
	if (!a_ctrl) {
		CAM_ERR(CAM_ACTUATOR, "failed: a_ctrl %pK", a_ctrl);
		return -EINVAL;
	}

	soc_private = (struct cam_actuator_soc_private *)a_ctrl->soc_info.soc_private;
	if (!soc_private) {
		CAM_ERR(CAM_ACTUATOR, "failed:soc_private %pK", soc_private);
		return -EINVAL;
	} else {
		power_info  = &soc_private->power_info;
		if (!power_info) {
			CAM_ERR(CAM_ACTUATOR, "failed: power_info %pK", a_ctrl, power_info);
			return -EINVAL;
		}
	}

	if(strstr(a_ctrl->aon_af_name, "dw9718p")) {
		CAM_INFO(CAM_ACTUATOR,"actuator_power_up_parklens_thread start ");
		read_val = oplus_cam_actuator_read_vaule(a_ctrl, DW9718P_DAC_ADDR);

		for (i = 0; i < ACTUATOR_REGISTER_SIZE; i++) {
			if ((DW9718P_PARKLENS_UP[i][0] != 0xff) && (DW9718P_PARKLENS_UP[i][1] != 0xff)) {
				if(read_val <= DW9718P_PARKLENS_UP[i][1]) {
					break;
				}
			} else {
				break;
			}
		}

		for (; i < ACTUATOR_REGISTER_SIZE; i++) {
			if ((DW9718P_PARKLENS_UP[i][0] != 0xff) && (DW9718P_PARKLENS_UP[i][1] != 0xff)) {
				while(busy_count < 10) {
					rc = camera_io_dev_read(&(a_ctrl->io_master_info),
								DW9718P_BUSY_ADDR,
								&is_actuator_busy,
								CAMERA_SENSOR_I2C_TYPE_BYTE,
								CAMERA_SENSOR_I2C_TYPE_BYTE);
					if(rc < 0) {
						CAM_ERR(CAM_ACTUATOR,"read failed ret : %d", rc);
						return rc;
					}
					CAM_INFO(CAM_ACTUATOR,"read DW9718P_BUSY_ADDR data : %d busy_count ：%d", is_actuator_busy ,busy_count);
					if(is_actuator_busy == 0) {
						break;
					}
					msleep(1);
					busy_count++;
				}
				busy_count = 0;
				rc = cam_actuator_ramwrite(a_ctrl,
					(uint32_t)DW9718P_PARKLENS_UP[i][0],
					(uint32_t)DW9718P_PARKLENS_UP[i][1],
					1,
					CAMERA_SENSOR_I2C_TYPE_BYTE,
					CAMERA_SENSOR_I2C_TYPE_WORD);
				if(rc < 0) {
					CAM_ERR(CAM_ACTUATOR,"write failed ret : %d", rc);
					return rc;
				}
				msleep(2);
			} else {
				CAM_ERR(CAM_ACTUATOR,"set parklens setting success ");
				break;
			}
			mutex_lock(&(a_ctrl->actuator_parklens_mutex));
			if (a_ctrl->actuator_parklens_thread_exit) {
				mutex_unlock(&(a_ctrl->actuator_parklens_mutex));
				CAM_ERR(CAM_ACTUATOR,"actuator_power_down_thread break actuator_parklens_thread_exit: %d", a_ctrl->actuator_parklens_thread_exit);
				break;
			}
			mutex_unlock(&(a_ctrl->actuator_parklens_mutex));
		}
	} else if(strstr(a_ctrl->aon_af_name, "dw9827c")) {
		CAM_INFO(CAM_ACTUATOR,"actuator_power_up_parklens_thread start ");
		read_val = oplus_cam_actuator_read_vaule(a_ctrl, DW9827C_DAC_ADDR);

		for (i = 0; i < ACTUATOR_REGISTER_SIZE; i++) {
			if ((DW9827C_PARKLENS_UP[i][0] != 0xff) && (DW9827C_PARKLENS_UP[i][1] != 0xff)) {
				if((read_val >> 8) <= DW9827C_PARKLENS_UP[i][1]) {
					break;
				}
			} else {
				break;
			}
		}

		for (; i < ACTUATOR_REGISTER_SIZE; i++) {
			if ((DW9827C_PARKLENS_UP[i][0] != 0xff) && (DW9827C_PARKLENS_UP[i][1] != 0xff)) {
				rc = cam_actuator_ramwrite(a_ctrl,
					(uint32_t)DW9827C_PARKLENS_UP[i][0],
					(uint32_t)DW9827C_PARKLENS_UP[i][1],
					1,
					CAMERA_SENSOR_I2C_TYPE_BYTE,
					CAMERA_SENSOR_I2C_TYPE_BYTE);
				if(rc < 0) {
					CAM_ERR(CAM_ACTUATOR,"write failed ret : %d", rc);
					return rc;
				}
				msleep(2);
			} else {
				CAM_ERR(CAM_ACTUATOR,"set parklens setting success ");
				break;
			}
			mutex_lock(&(a_ctrl->actuator_parklens_mutex));
			if (a_ctrl->actuator_parklens_thread_exit) {
				mutex_unlock(&(a_ctrl->actuator_parklens_mutex));
				CAM_ERR(CAM_ACTUATOR,"actuator_power_down_thread break actuator_parklens_thread_exit: %d", a_ctrl->actuator_parklens_thread_exit);
				break;
			}
			mutex_unlock(&(a_ctrl->actuator_parklens_mutex));
		}
	}
        else {
		CAM_ERR(CAM_ACTUATOR,"not support this actuator ");
	}

	mutex_lock(&(a_ctrl->actuator_parklens_mutex));
	a_ctrl->actuator_parklens_thread_exit = true;
	mutex_unlock(&(a_ctrl->actuator_parklens_mutex));

	CAM_INFO(CAM_ACTUATOR, "cam_actuator_parklens_thread exit");

	return rc;
}

int actuator_power_up_parklens_second_thread(void *arg)
{
	int rc = 0;
	int i;
	uint32_t read_val = 0;
	uint32_t write_val = 0;
	struct cam_actuator_ctrl_t *a_ctrl = (struct cam_actuator_ctrl_t *)arg;
	struct cam_actuator_soc_private *soc_private;
	struct cam_sensor_power_ctrl_t *power_info;

	msleep(5);
	if (!a_ctrl) {
		CAM_ERR(CAM_ACTUATOR, "failed: a_ctrl %pK", a_ctrl);
		return -EINVAL;
	}

	soc_private = (struct cam_actuator_soc_private *)a_ctrl->soc_info.soc_private;
	if (!soc_private) {
		CAM_ERR(CAM_ACTUATOR, "failed:soc_private %pK", soc_private);
		return -EINVAL;
	} else {
		power_info  = &soc_private->power_info;
		if (!power_info) {
			CAM_ERR(CAM_ACTUATOR, "failed: power_info %pK", a_ctrl, power_info);
			return -EINVAL;
		}
	}

	CAM_INFO(CAM_ACTUATOR,"actuator_power_up_parklens_second_thread start ");
	camera_io_dev_read(&(a_ctrl->io_master_info), SEM1217S_DAC_ADDR, &read_val,
					CAMERA_SENSOR_I2C_TYPE_WORD, CAMERA_SENSOR_I2C_TYPE_WORD);
	read_val = (((read_val & 0x00FF) << 8) | ((read_val & 0xFF00) >> 8));
	CAM_INFO(CAM_ACTUATOR,"read success addr = 0x%x data=0x%x", SEM1217S_DAC_ADDR, read_val);
	for (i = 0; i < ACTUATOR_REGISTER_SIZE; i++) {
		if ((SEM1217S_PARKLENS_UP[i][0] != 0xff) && (SEM1217S_PARKLENS_UP[i][1] != 0xff)) {
			if(read_val <= SEM1217S_PARKLENS_UP[i][1]) {
				break;
			}
		} else {
			break;
		}
	}

	for (; i < ACTUATOR_REGISTER_SIZE; i++) {
		if ((SEM1217S_PARKLENS_UP[i][0] != 0xff) && (SEM1217S_PARKLENS_UP[i][1] != 0xff)) {
			write_val = (SEM1217S_PARKLENS_UP[i][1] & 0xFF); /* Set AF Target : Low Byte */
			rc = cam_actuator_ramwrite(a_ctrl,
				(uint32_t)SEM1217S_PARKLENS_UP[i][0],
				write_val,
				1,
				CAMERA_SENSOR_I2C_TYPE_WORD,
				CAMERA_SENSOR_I2C_TYPE_BYTE);

			write_val = ( (SEM1217S_PARKLENS_UP[i][1] >> 8) & 0xFF ); /* Set AF Position : High Byte */
			rc = cam_actuator_ramwrite(a_ctrl,
				((uint32_t)SEM1217S_PARKLENS_UP[i][0] + 1),
				write_val,
				1,
				CAMERA_SENSOR_I2C_TYPE_WORD,
				CAMERA_SENSOR_I2C_TYPE_BYTE);
			if(rc < 0) {
				CAM_ERR(CAM_ACTUATOR,"write failed ret : %d", rc);
				return rc;
			}
			msleep(3);
		} else {
			CAM_ERR(CAM_ACTUATOR,"set parklens setting success ");
			break;
		}
		mutex_lock(&(a_ctrl->actuator_parklens_second_mutex));
		if (a_ctrl->actuator_parklens_second_thread_exit) {
			mutex_unlock(&(a_ctrl->actuator_parklens_second_mutex));
			CAM_ERR(CAM_ACTUATOR,"actuator_power_down_thread break actuator_parklens_thread_exit: %d", a_ctrl->actuator_parklens_second_thread_exit);
			break;
		}
		mutex_unlock(&(a_ctrl->actuator_parklens_second_mutex));
	}

	mutex_lock(&(a_ctrl->actuator_parklens_second_mutex));
	a_ctrl->actuator_parklens_second_thread_exit = true;
	mutex_unlock(&(a_ctrl->actuator_parklens_second_mutex));

	CAM_INFO(CAM_ACTUATOR, "cam_actuator_parklens_second_thread exit");

	return rc;
}

uint32_t oplus_cam_actuator_read_vaule(struct cam_actuator_ctrl_t *a_ctrl, uint32_t addr)
{
	int rc = 0;
	uint32_t read_val = 0;
	uint32_t check_reg_val_h = 0;
	uint32_t check_reg_val_l = 0;

	rc = camera_io_dev_read(&(a_ctrl->io_master_info),
		addr,
		&check_reg_val_h,
		CAMERA_SENSOR_I2C_TYPE_BYTE,
		CAMERA_SENSOR_I2C_TYPE_BYTE);
	if(rc < 0) {
		CAM_ERR(CAM_ACTUATOR,"read failed rc : %d ", rc);
	} else {
		CAM_INFO(CAM_ACTUATOR,"read success addr = 0x%x data=0x%x", addr, check_reg_val_h);
	}
	rc = camera_io_dev_read(&(a_ctrl->io_master_info),
		addr+1,
		&check_reg_val_l,
		CAMERA_SENSOR_I2C_TYPE_BYTE,
		CAMERA_SENSOR_I2C_TYPE_BYTE);
	if(rc < 0) {
		CAM_ERR(CAM_ACTUATOR,"read failed ret : %d ", rc);
	} else {
		CAM_INFO(CAM_ACTUATOR,"read success addr = 0x%x data=0x%x", addr + 1, check_reg_val_l);
	}

	read_val = (check_reg_val_h << 8) | (check_reg_val_l);
	CAM_INFO(CAM_ACTUATOR,"read read_val success read_val = 0x%x", read_val);
	return read_val;
}

void oplus_cam_actuator_power_down(struct cam_actuator_ctrl_t *a_ctrl)
{
	if(!strstr(a_ctrl->aon_af_name, "sem1217s"))
	{
		mutex_lock(&(a_ctrl->actuator_parklens_mutex));
		if (a_ctrl->cam_atc_power_state == CAM_ACTUATOR_POWER_ON && a_ctrl->actuator_power_down_thread_exit == true) {
			a_ctrl->actuator_power_down_thread_exit = false;
			a_ctrl->actuator_parklens_thread_exit = true;
			kthread_run(actuator_power_down_thread, a_ctrl, "actuator_power_down_thread");
			CAM_INFO(CAM_ACTUATOR, "actuator_power_down_thread created");
		} else {
			CAM_ERR(CAM_ACTUATOR, "no need to create actuator_power_down_thread, actuator_power_state %d, actuator_power_down_thread_exit %d",
				a_ctrl->cam_atc_power_state, a_ctrl->actuator_power_down_thread_exit);
		}
		mutex_unlock(&(a_ctrl->actuator_parklens_mutex));
	}else{
		mutex_lock(&(a_ctrl->actuator_parklens_second_mutex));
		if (a_ctrl->cam_atc_power_second_state == CAM_ACTUATOR_POWER_ON && a_ctrl->actuator_power_down_second_thread_exit == true) {
			a_ctrl->actuator_power_down_second_thread_exit = false;
			a_ctrl->actuator_parklens_second_thread_exit = true;
			kthread_run(actuator_power_down_second_thread, a_ctrl, "actuator_power_down_second_thread");
			CAM_INFO(CAM_ACTUATOR, "actuator_power_down_second_thread created");
		} else {
			CAM_ERR(CAM_ACTUATOR, "no need to create actuator_power_down_second_thread, actuator_power_state %d, actuator_power_down_second_thread %d",
				a_ctrl->cam_atc_power_second_state, a_ctrl->actuator_power_down_second_thread_exit);
		}
		mutex_unlock(&(a_ctrl->actuator_parklens_second_mutex));
	}
}

void oplus_cam_actuator_parklens(struct cam_actuator_ctrl_t *a_ctrl)
{
	if(a_ctrl->is_only_powerdown != 1)
	{
		if(!strstr(a_ctrl->aon_af_name, "sem1217s"))
		{
			mutex_lock(&(a_ctrl->actuator_parklens_mutex));
			if (a_ctrl->cam_atc_power_state == CAM_ACTUATOR_POWER_ON && a_ctrl->actuator_parklens_thread_exit == true) {
				a_ctrl->actuator_parklens_thread_exit = false;
				a_ctrl->actuator_power_down_thread_exit = true;
				kthread_run(actuator_power_up_parklens_thread, a_ctrl, "actuator_power_up_parklens_thread");
				CAM_INFO(CAM_ACTUATOR, "actuator_power_up_parklens_thread created");
			} else {
				CAM_ERR(CAM_ACTUATOR, "no need to create actuator_power_up_parklens_thread, actuator_power_state %d, actuator_power_down_thread_exit %d,cam_act_state : %d",
					a_ctrl->cam_atc_power_state, a_ctrl->actuator_parklens_thread_exit, a_ctrl->cam_act_state);
			}
			mutex_unlock(&(a_ctrl->actuator_parklens_mutex));
		}
		else
		{
			mutex_lock(&(a_ctrl->actuator_parklens_second_mutex));
			if (a_ctrl->cam_atc_power_second_state == CAM_ACTUATOR_POWER_ON && a_ctrl->actuator_parklens_second_thread_exit == true) {
				a_ctrl->actuator_parklens_second_thread_exit = false;
				a_ctrl->actuator_power_down_second_thread_exit = true;
				kthread_run(actuator_power_up_parklens_second_thread, a_ctrl, "actuator_power_up_parklens_second_thread");
				CAM_INFO(CAM_ACTUATOR, "actuator_power_up_parklens_thread created");
			} else {
				CAM_ERR(CAM_ACTUATOR, "no need to create actuator_power_up_parklens_thread, cam_atc_power_second_state %d, actuator_power_down_thread_exit %d,cam_act_state : %d",
					a_ctrl->cam_atc_power_second_state, a_ctrl->actuator_parklens_second_thread_exit, a_ctrl->cam_act_state);
			}
			mutex_unlock(&(a_ctrl->actuator_parklens_second_mutex));
   		}
	}
}

void oplus_cam_parklens_init(struct cam_actuator_ctrl_t *a_ctrl)
{
	CAM_INFO(CAM_ACTUATOR, "oplus_cam_parklens_init");
	a_ctrl->cam_atc_power_state = CAM_ACTUATOR_POWER_OFF;
	a_ctrl->actuator_power_down_thread_exit = true;
	a_ctrl->actuator_parklens_thread_exit = true;

	a_ctrl->actuator_power_down_second_thread_exit = true;
	a_ctrl->actuator_parklens_second_thread_exit = true;
	a_ctrl->cam_atc_power_second_state = CAM_ACTUATOR_POWER_OFF;
	mutex_init(&(a_ctrl->actuator_parklens_mutex));
	mutex_init(&(a_ctrl->actuator_parklens_second_mutex));
}

int32_t oplus_cam_actuator_power_up(struct cam_actuator_ctrl_t *a_ctrl, int32_t rc)
{
	if(!strstr(a_ctrl->aon_af_name, "sem1217s"))
	{
	    mutex_lock(&(a_ctrl->actuator_parklens_mutex));
		a_ctrl->actuator_power_down_thread_exit = true;
		if (a_ctrl->cam_atc_power_state == CAM_ACTUATOR_POWER_OFF) {
			rc = cam_actuator_power_up(a_ctrl);
			if (!rc){
				a_ctrl->cam_atc_power_state = CAM_ACTUATOR_POWER_ON;
				CAM_ERR(CAM_ACTUATOR, "cam_actuator_power_up successfully");
			}
		} else {
			CAM_ERR(CAM_ACTUATOR, "actuator already power on, no need to power on again");
		}
		mutex_unlock(&(a_ctrl->actuator_parklens_mutex));
	}else{
		mutex_lock(&(a_ctrl->actuator_parklens_second_mutex));
		a_ctrl->actuator_power_down_second_thread_exit = true;
		if (a_ctrl->cam_atc_power_second_state == CAM_ACTUATOR_POWER_OFF) {
			rc = cam_actuator_power_up(a_ctrl);
			if (!rc){
				a_ctrl->cam_atc_power_second_state = CAM_ACTUATOR_POWER_ON;
				CAM_ERR(CAM_ACTUATOR, "second cam_actuator_power_up successfully");
			}
		} else {
			CAM_ERR(CAM_ACTUATOR, "second actuator already power on, no need to power on again");
		}
		mutex_unlock(&(a_ctrl->actuator_parklens_second_mutex));
	}
	return rc;
}


void cam_actuator_poll_setting_update(struct cam_actuator_ctrl_t *a_ctrl) {

        struct i2c_settings_list *i2c_list = NULL;

        a_ctrl->is_actuator_ready = true;
        memset(&(a_ctrl->poll_register), 0, sizeof(struct cam_sensor_i2c_reg_array));
        list_for_each_entry(i2c_list,
                &(a_ctrl->i2c_data.init_settings.list_head), list) {
                if (i2c_list->op_code == CAM_SENSOR_I2C_POLL) {
                        a_ctrl->poll_register.reg_addr = i2c_list->i2c_settings.reg_setting[0].reg_addr;
                        a_ctrl->poll_register.reg_data = i2c_list->i2c_settings.reg_setting[0].reg_data;
                        a_ctrl->poll_register.data_mask = i2c_list->i2c_settings.reg_setting[0].data_mask;
                        a_ctrl->poll_register.delay = 100; //i2c_list->i2c_settings.reg_setting[0].delay; // The max delay should be 100
                        a_ctrl->addr_type = i2c_list->i2c_settings.addr_type;
                        a_ctrl->data_type = i2c_list->i2c_settings.data_type;
                }
        }
}

void cam_actuator_poll_setting_apply(struct cam_actuator_ctrl_t *a_ctrl) {
        int ret = 0;
        if (!a_ctrl->is_actuator_ready) {
                if (a_ctrl->poll_register.reg_addr || a_ctrl->poll_register.reg_data) {
                        ret = camera_io_dev_poll(
                                &(a_ctrl->io_master_info),
                                a_ctrl->poll_register.reg_addr,
                                a_ctrl->poll_register.reg_data,
                                a_ctrl->poll_register.data_mask,
                                a_ctrl->addr_type,
                                a_ctrl->data_type,
                                a_ctrl->poll_register.delay);
                        if (ret < 0) {
                                CAM_ERR(CAM_ACTUATOR,"i2c poll apply setting Fail: %d, is_actuator_ready %d", ret, a_ctrl->is_actuator_ready);
                        } else {
                                CAM_DBG(CAM_ACTUATOR,"is_actuator_ready %d, ret %d", a_ctrl->is_actuator_ready, ret);
                        }
                        a_ctrl->is_actuator_ready = true; //Just poll one time
                }
        }
}

int32_t cam_actuator_ramwrite(struct cam_actuator_ctrl_t *a_ctrl,
	uint32_t addr, uint32_t data, unsigned short mdelay,enum camera_sensor_i2c_type addr_type,enum camera_sensor_i2c_type data_type)
{
	int32_t rc = 0;
	int retry = 3;
	int i = 0;
	struct cam_sensor_i2c_reg_array i2c_write_setting = {
		.reg_addr = addr,
		.reg_data = data,
		.delay = mdelay,
		.data_mask = 0x00,
	};
	struct cam_sensor_i2c_reg_setting i2c_write = {
		.reg_setting = &i2c_write_setting,
		.size = 1,
		.addr_type = addr_type,
		.data_type = data_type,
		.delay = 0,
	};
	if (a_ctrl == NULL) {
		CAM_ERR(CAM_ACTUATOR, "Invalid Args");
		return -EINVAL;
	}

	for(i = 0; i < retry; i++)
	{
		rc = camera_io_dev_write(&(a_ctrl->io_master_info), &i2c_write);
		if (rc < 0) {
			CAM_ERR(CAM_ACTUATOR, "write 0x%x failed, retry:%d device=%d master=%d", addr, i+1,a_ctrl->io_master_info.cci_client->cci_device,a_ctrl->io_master_info.cci_client->cci_i2c_master);
		} else {
			CAM_INFO(CAM_ACTUATOR, " write data addr:%x data:%x retry=%d",addr, data,i);
			break;
		}
	}

	return rc;
}

int32_t cam_actuator_construct_default_power_setting_oem(
	struct cam_sensor_power_ctrl_t *power_info)
{
	int rc = 0;

	power_info->power_setting_size = 2;
	power_info->power_setting =
		kzalloc(sizeof(struct cam_sensor_power_setting)*2,
			GFP_KERNEL);
	if (!power_info->power_setting)
		return -ENOMEM;

	power_info->power_setting[0].seq_type = SENSOR_VIO;
	power_info->power_setting[0].seq_val = CAM_VIO;
	power_info->power_setting[0].config_val = 1;
	power_info->power_setting[0].delay = 1;

	power_info->power_setting[1].seq_type = SENSOR_VAF;
	power_info->power_setting[1].seq_val = CAM_VAF;
	power_info->power_setting[1].config_val = 1;
	power_info->power_setting[1].delay = 10;

	power_info->power_down_setting_size = 2;
	power_info->power_down_setting =
		kzalloc(sizeof(struct cam_sensor_power_setting)*2,
			GFP_KERNEL);
	if (!power_info->power_down_setting) {
		rc = -ENOMEM;
		goto free_power_settings;
	}

	power_info->power_down_setting[0].seq_type = SENSOR_VAF;
	power_info->power_down_setting[0].seq_val = CAM_VAF;
	power_info->power_down_setting[0].config_val = 0;

	power_info->power_down_setting[1].seq_type = SENSOR_VIO;
	power_info->power_down_setting[1].seq_val = CAM_VIO;
	power_info->power_down_setting[1].config_val = 0;

	return rc;

free_power_settings:
	kfree(power_info->power_setting);
	power_info->power_setting = NULL;
	power_info->power_setting_size = 0;
	return rc;
}


int cam_actuator_slaveInfo_pkt_parser_oem(struct cam_actuator_ctrl_t *a_ctrl)
{
	struct device_node     *of_node = NULL;
	int                    ret = 0;
	const char             *p = NULL;
	struct cam_hw_soc_info *soc_info = &a_ctrl->soc_info;
	of_node = soc_info->dev->of_node;

	ret = of_property_read_u32(of_node, "is_aon_af", &a_ctrl->is_aon_af);
	if (ret)
	{
		a_ctrl->is_aon_af = 0;
		CAM_ERR(CAM_ACTUATOR, "get failed for is_aon_af = %d",a_ctrl->is_aon_af);
	}
	else
	{
		CAM_INFO(CAM_ACTUATOR, "read is_aon_af success, value:%d", a_ctrl->is_aon_af);
	}

	if(a_ctrl->is_aon_af == 0)
	{
		CAM_INFO(CAM_ACTUATOR, "not support aon af");
		return 0;
	}

	ret = of_property_read_u32(of_node, "aon_af_slave", &a_ctrl->aon_af_slave);
	if (ret)
	{
		a_ctrl->aon_af_slave = 0;
		CAM_ERR(CAM_ACTUATOR, "get failed for aon_af_slave = 0x%x",a_ctrl->aon_af_slave);
	}
	else
	{
		CAM_INFO(CAM_ACTUATOR, "read aon_af_slave success, value:0x%x", a_ctrl->aon_af_slave);
		a_ctrl->io_master_info.cci_client->sid = (a_ctrl->aon_af_slave >> 1);
	}

	ret = of_property_read_u32(of_node, "aon_eeprom_slave", &a_ctrl->aon_eeprom_slave);
	if (ret)
	{
		a_ctrl->aon_eeprom_slave = 0;
		CAM_ERR(CAM_ACTUATOR, "get failed for aon_eeprom_slave = 0x%x",a_ctrl->aon_eeprom_slave);
	}
	else
	{
		CAM_INFO(CAM_ACTUATOR, "read aon_eeprom_slave success, value:0x%x", a_ctrl->aon_eeprom_slave);
		a_ctrl->io_master_info.cci_client->sid = (a_ctrl->aon_af_slave >> 1);
	}

	ret = of_property_read_string_index(of_node, "actuator,name", 0, (const char **)&p);
	if (ret)
	{
		memcpy(a_ctrl->aon_af_name, "aon_af_name", sizeof(a_ctrl->aon_af_name));
		CAM_ERR(CAM_ACTUATOR, "get actuator,name failed rc:%d, set default value to %s", ret, a_ctrl->aon_af_name);
	}
	else
	{
		memcpy(a_ctrl->aon_af_name, p, sizeof(a_ctrl->aon_af_name));
		CAM_INFO(CAM_ACTUATOR, "read actuator,name success, value:%s", a_ctrl->aon_af_name);
	}

	a_ctrl->io_master_info.cci_client->i2c_freq_mode = I2C_FAST_PLUS_MODE;
	a_ctrl->io_master_info.cci_client->retries = 3;
	a_ctrl->io_master_info.cci_client->id_map = 0;
	a_ctrl->aon_af_power = CAM_ACTUATOR_POWER_OFF;
	a_ctrl->io_master_info.cci_client->cci_i2c_master = a_ctrl->cci_i2c_master;

	return 0;
}

int cam_actuator_set_initsetting(struct cam_actuator_ctrl_t *a_ctrl)
{
	int ret = 0;
	int i = 0;

	if(strstr(a_ctrl->aon_af_name, "dw9718p"))
	{
		for (i = 0; i < ACTUATOR_REGISTER_SIZE; i++)
		{
			if ( (DW9817P[i][0] != 0xff) && (DW9817P[i][1] != 0xff) )
			{
				ret = cam_actuator_ramwrite(a_ctrl,
					(uint32_t)DW9817P[i][0],
					(uint32_t)DW9817P[i][1],
					1,
					CAMERA_SENSOR_I2C_TYPE_BYTE,
					CAMERA_SENSOR_I2C_TYPE_BYTE);
				if(ret < 0)
				{
					CAM_ERR(CAM_ACTUATOR,"write failed ret : %d", ret);
					return ret;
				}
			}
			else
			{
				CAM_INFO(CAM_ACTUATOR,"set initting setting success ");
				break;
			}
		}
	}
	else
	{
		CAM_ERR(CAM_ACTUATOR,"not support this aon_af ");
	}

	return ret;
}

int cam_actuator_get_infinity_postion(struct cam_actuator_ctrl_t *a_ctrl)
{
	uint32_t a_sid = 0;
	int i = 0;
	int ret = 0;
	a_sid = a_ctrl->io_master_info.cci_client->sid;
	a_ctrl->io_master_info.cci_client->sid = (a_ctrl->aon_eeprom_slave >> 1);

	if(strstr(a_ctrl->aon_af_name, "dw9718p"))
	{
		for (i = 0; i < ACTUATOR_REGISTER_SIZE; i++)
		{
			if (DW9817P_EEPROM[i][0] != 0xffff)
			{
				ret = camera_io_dev_read(&(a_ctrl->io_master_info),
					(uint32_t)DW9817P_EEPROM[i][0],
					(uint32_t *)&DW9817P_EEPROM[i][1],
					CAMERA_SENSOR_I2C_TYPE_WORD,
					CAMERA_SENSOR_I2C_TYPE_BYTE);
				if(ret < 0)
				{
					CAM_ERR(CAM_ACTUATOR,"read failed ret : %d addr = 0x%x", ret, DW9817P_EEPROM[i][0]);
				}
				else
				{
					CAM_INFO(CAM_ACTUATOR,"read success addr = 0x%x data=0x%x", DW9817P_EEPROM[i][0],DW9817P_EEPROM[i][1]);
				}
			}
			else
			{
				CAM_ERR(CAM_ACTUATOR,"get infinity_postion success ");
				break;
			}
		}
		a_ctrl->macroPos = DW9817P_EEPROM[1][1] << 8 | DW9817P_EEPROM[0][1];
		a_ctrl->infPos   = DW9817P_EEPROM[3][1] << 8 | DW9817P_EEPROM[2][1];
		a_ctrl->middPos  = DW9817P_EEPROM[5][1] << 8 | DW9817P_EEPROM[4][1];
		CAM_INFO(CAM_ACTUATOR,"a_ctrl->macroPos = %d ; infPos =%d ; middPos=%d",a_ctrl->macroPos,a_ctrl->infPos,a_ctrl->middPos);

	}
	else
	{
		CAM_ERR(CAM_ACTUATOR,"not support this aon_af ");
	}

	a_ctrl->io_master_info.cci_client->sid = a_sid;
	return ret;
}

int cam_actuator_enble(struct cam_actuator_ctrl_t *a_ctrl)
{
	int rc = 0;

	if(a_ctrl->is_aon_af != 0)
	{
		if(a_ctrl->aon_af_power == CAM_ACTUATOR_POWER_OFF)
		{
			rc = cam_actuator_power_up(a_ctrl);
			if(rc)
			{
				CAM_ERR(CAM_ACTUATOR,"cam_actuator_power up failed");
				mutex_unlock(&(a_ctrl->actuator_mutex));
				return rc;
			}
			else
			{
				CAM_INFO(CAM_ACTUATOR,"cam_actuator_power up success");
				a_ctrl->aon_af_power = CAM_ACTUATOR_POWER_ON;
			}
		}

		rc = cam_actuator_set_initsetting(a_ctrl);
		if(rc)
		{
			CAM_ERR(CAM_ACTUATOR,"cam_actuator_set_initsetting up failed");
			mutex_unlock(&(a_ctrl->actuator_mutex));
			return rc;
		}
		else
		{
			CAM_INFO(CAM_ACTUATOR,"cam_actuator_set_initsetting  success");
		}
		CAM_ERR(CAM_ACTUATOR,"cam_actuator_enble success");

		rc = cam_actuator_get_infinity_postion(a_ctrl);
		if(rc)
		{
			CAM_ERR(CAM_ACTUATOR,"cam_actuator_get_infinity_postion failed ret : %d", rc);
		}

	}
	else
	{
		CAM_ERR(CAM_ACTUATOR,"this sensor not surport aon af");
	}

	return rc;
}

int cam_actuator_setfocus(struct cam_actuator_ctrl_t *a_ctrl)
{
	int ret = 0;
	int i;

	if(a_ctrl->is_aon_af != 0)
	{
		if(strstr(a_ctrl->aon_af_name, "dw9718p"))
		{
		    for (i = 0; (uint32_t)DW9718P_PARKLENS_UP[i][1] < a_ctrl->middPos; i++)
			{
				if ((DW9718P_PARKLENS_UP[i][0] != 0xff) && (DW9718P_PARKLENS_UP[i][1] != 0xff))
				{
					ret = cam_actuator_ramwrite(a_ctrl,
						(uint32_t)DW9718P_PARKLENS_UP[i][0],
						(uint32_t)DW9718P_PARKLENS_UP[i][1],
						1,
						CAMERA_SENSOR_I2C_TYPE_BYTE,
						CAMERA_SENSOR_I2C_TYPE_WORD);
					if(ret < 0)
					{
						CAM_ERR(CAM_ACTUATOR,"write failed ret : %d", ret);
						return ret;
					}
					msleep(3);
				}
				else
				{
				    return ret;
				}
			}
			ret = cam_actuator_ramwrite(a_ctrl,
					0x03,
					a_ctrl->middPos,
					1,
					CAMERA_SENSOR_I2C_TYPE_BYTE,
					CAMERA_SENSOR_I2C_TYPE_WORD);
			if(ret < 0)
			{
				CAM_ERR(CAM_ACTUATOR,"write failed ret : %d", ret);
				return ret;
			}
		}
	}
	return ret;
}

int cam_actuator_disable(struct cam_actuator_ctrl_t *a_ctrl)
{
	int rc = 0;
	int i;
	int read_val = 0;
	CAM_INFO(CAM_ACTUATOR,"a_ctrl->is_aon_af=%d a_ctrl->aon_af_power=%d",a_ctrl->is_aon_af,a_ctrl->aon_af_power);
	if(a_ctrl->is_aon_af != 0 && a_ctrl->aon_af_power==CAM_ACTUATOR_POWER_ON)
	{
		if(strstr(a_ctrl->aon_af_name, "dw9718p"))
		{
			read_val = oplus_cam_actuator_read_vaule(a_ctrl, DW9718P_DAC_ADDR);

			for (i = 0; i < ACTUATOR_REGISTER_SIZE; i++)
			{
				if ((DW9718P_PARKLENS_DOWN[i][0] != 0xff) && (DW9718P_PARKLENS_DOWN[i][1] != 0xff))
				{
					if(read_val >= DW9718P_PARKLENS_DOWN[i][1])
					{
						break;
					}
				}
				else
				{
					break;
				}
			}
			for (; i < ACTUATOR_REGISTER_SIZE; i++)
			{
				if ( (DW9718P_PARKLENS_DOWN[i][0] != 0xff) && (DW9718P_PARKLENS_DOWN[i][1] != 0xff) )
				{
					rc = cam_actuator_ramwrite(a_ctrl,
						(uint32_t)DW9718P_PARKLENS_DOWN[i][0],
						(uint32_t)DW9718P_PARKLENS_DOWN[i][1],
						1,
						CAMERA_SENSOR_I2C_TYPE_BYTE,
						CAMERA_SENSOR_I2C_TYPE_WORD);
					if(rc < 0)
					{
						CAM_ERR(CAM_ACTUATOR,"write failed ret : %d", rc);
						return rc;
					}
					msleep(2);
				}
				else
				{
					CAM_INFO(CAM_ACTUATOR,"set parklens success ");
					break;
				}
			}
		}
		rc = cam_actuator_power_down(a_ctrl);
		if(rc)
		{
			CAM_ERR(CAM_ACTUATOR,"cam_actuator_power_down failed");
			mutex_unlock(&(a_ctrl->actuator_mutex));
			return rc;
		}
		else
		{
			CAM_ERR(CAM_ACTUATOR,"cam_actuator_power_down success");
			a_ctrl->aon_af_power = CAM_ACTUATOR_POWER_OFF;
		}
	}
	else
	{
		CAM_ERR(CAM_ACTUATOR,"this sensor not surport aon af");
	}
	CAM_ERR(CAM_ACTUATOR,"cam_actuator_disable success");
	return rc;
}

int32_t oplus_cam_actuator_driver_cmd(struct cam_actuator_ctrl_t *a_ctrl, void *arg)
{
	int rc = 0;
	struct cam_control *cmd = (struct cam_control *)arg;
	switch (cmd->op_code) {
		case CAM_OEM_ACTUATOR_SETFOCUS: {
			rc = cam_actuator_setfocus(a_ctrl);
			if(rc)
			{
				CAM_ERR(CAM_ACTUATOR,"cam_actuator_enble failed");
			}
			break;
		}
		case CAM_OEM_ACTUATOR_GET_ID: {
			rc = cam_actuator_slaveInfo_pkt_parser_oem(a_ctrl);
			if(rc)
			{
				CAM_ERR(CAM_ACTUATOR,"cam_actuator_get_infinity_postion failed ret : %d", rc);
			}
			if (copy_to_user((void __user *)cmd->handle,&a_ctrl->is_aon_af,sizeof(uint32_t)))
			{
				CAM_ERR(CAM_ACTUATOR,"copy camera id to user fail ");
			}
			break;
		}
		case CAM_OEM_ACTUATOR_STATR: {
			rc = cam_actuator_enble(a_ctrl);
			if(rc)
			{
				CAM_ERR(CAM_ACTUATOR,"cam_actuator_enble failed");
			}
			break;
		}
		case CAM_OEM_ACTUATOR_STOP: {
			rc = cam_actuator_disable(a_ctrl);
			if(rc)
			{
				CAM_ERR(CAM_ACTUATOR,"cam_actuator_disable failed");
			}
			break;
		}
	}
	return rc;
}

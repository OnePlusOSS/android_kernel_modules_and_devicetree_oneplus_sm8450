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

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/dma-buf.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/module.h>
#include <linux/clk.h>

#include "include/aon_sensor_core.h"
#include "include/aon_uapi.h"
#include "include/aon_sensor_io.h"
#include "include/aon_soc_util.h"

static s32 aon_cci_is_on = -1;

static struct aon_sensor_ctrl_t *g_ctrl = NULL;

static s32 aon_cci_release(struct aon_sensor_ctrl_t *s_ctrl) {
	s32 rc = 0;

	if (!s_ctrl)
		return -EINVAL;

	if (aon_cci_is_on == 1) {
		rc = aon_io_release(s_ctrl->client);
		if (rc)
			pr_err("%s aon_io_release failed", __func__);
		else
			aon_cci_is_on = 0;
	}
	return rc;
}

static s32 aon_cci_init(struct aon_sensor_ctrl_t *s_ctrl) {
	s32 rc = 0;

	if (!s_ctrl)
		return -EINVAL;

	if (aon_cci_is_on != 1) {
		rc = aon_io_init(s_ctrl->client);
		if (rc < 0)
			pr_err("%s cci_init failed: rc: %d", __func__, rc);
		else
			aon_cci_is_on = 1;
	}
	return rc;
}

static s32 aon_sensor_update_i2c_info(struct aon_cmd_i2c_info *i2c_info,
	struct aon_sensor_ctrl_t *s_ctrl)
{
	s32 rc = 0;
	struct cam_sensor_cci_client *cci_client = NULL;

	if (!i2c_info || !s_ctrl)
		return -EINVAL;

	cci_client = s_ctrl->client;
	if (!cci_client) {
		pr_err("failed: cci_client %pK", cci_client);
		return -EINVAL;
	}
	cci_client->cci_i2c_master = s_ctrl->cci_i2c_master;
	cci_client->sid = i2c_info->slave_addr >> 1;
	cci_client->retries = 3;
	cci_client->id_map = 0;
	cci_client->i2c_freq_mode = i2c_info->i2c_freq_mode;
	pr_info(" Master: %d sid: %d freq_mode: %d",
		cci_client->cci_i2c_master, i2c_info->slave_addr,
		i2c_info->i2c_freq_mode);

	s_ctrl->sensordata->slave_info.sensor_slave_addr = i2c_info->slave_addr;
	return rc;
}

static s32 aon_sensor_update_slave_info(struct aon_cmd_probe *probe_info,
	struct aon_sensor_ctrl_t *s_ctrl)
{
	s32 rc = 0;

	if (!probe_info || !s_ctrl) {
		pr_err("%s invalid params", __func__);
		return -EINVAL;
	}

	s_ctrl->sensordata->slave_info.sensor_id_reg_addr = probe_info->reg_addr;
	s_ctrl->sensordata->slave_info.sensor_id = probe_info->expected_data;
	s_ctrl->sensordata->slave_info.sensor_id_mask = probe_info->data_mask;

#ifdef OPLUS_FEATURE_CAMERA_COMMON
	s_ctrl->sensordata->slave_info.addr_type = probe_info->addr_type;
	s_ctrl->sensordata->slave_info.data_type = probe_info->data_type;
#endif

	pr_info("%s Sensor Addr: 0x%x sensor_id: 0x%x sensor_mask: 0x%x",
		__func__,
		s_ctrl->sensordata->slave_info.sensor_id_reg_addr,
		s_ctrl->sensordata->slave_info.sensor_id,
		s_ctrl->sensordata->slave_info.sensor_id_mask);
	return rc;
}

static s32 aon_sensor_bob_pwm_mode_switch(struct aon_hw_soc_info *soc_info,
	s32 bob_reg_idx, bool flag)
{
	s32 rc = 0;
	u32 op_current = 0;

	if (!soc_info)
		return -EINVAL;

	op_current = (flag == true) ? soc_info->rgltr_op_mode[bob_reg_idx] : 0;

	if (soc_info->rgltr[bob_reg_idx] != NULL) {
		rc = regulator_set_load(soc_info->rgltr[bob_reg_idx],
			op_current);
		if (rc)
			pr_err("%s BoB PWM SetLoad failed rc: %d", __func__, rc);
	}

	return rc;
}

static s32 aon_sensor_power_up(struct aon_sensor_ctrl_t *s_ctrl)
{
	s32 rc = 0;
	struct aon_sensor_power_ctrl_t *power_info = NULL;
	struct aon_camera_slave_info *slave_info = NULL;
	struct aon_hw_soc_info *soc_info = NULL;

	if (!s_ctrl) {
		pr_err("%s s_ctrl is null: %pK", __func__, s_ctrl);
		return -EINVAL;
	}

	soc_info = &s_ctrl->soc_info;
	if (!s_ctrl->sensordata) {
		pr_err("%s sensordata is null: %pK", __func__, s_ctrl->sensordata);
		return -EINVAL;
	}
	power_info = &s_ctrl->sensordata->power_info;
	slave_info = &(s_ctrl->sensordata->slave_info);

	if (!power_info || !slave_info) {
		pr_err("%s failed: %pK %pK", __func__, power_info, slave_info);
		return -EINVAL;
	}

	if (s_ctrl->bob_pwm_switch) {
		pr_info("%s before aon_sensor_bob_pwm_mode_switch", __func__);
		rc = aon_sensor_bob_pwm_mode_switch(soc_info,
			s_ctrl->bob_reg_index, true);
		if (rc) {
			pr_err("%s BoB PWM setup failed rc: %d", __func__, rc);
			rc = 0;
		}
	}

	rc = aon_sensor_core_power_up(power_info, soc_info);
	if (rc < 0) {
		pr_err("%s power up the core is failed:%d", __func__, rc);
		return rc;
	}
	return rc;
}

static u16 aon_sensor_id_by_mask(struct aon_sensor_ctrl_t *s_ctrl,
	u32 chipid)
{
	u16 sensor_id = (u16)(chipid & 0xFFFF);
	s16 sensor_id_mask = 0;

	if (!s_ctrl) {
		pr_err("%s invalid param", __func__);
		return -EINVAL;
	}

	sensor_id_mask = s_ctrl->sensordata->slave_info.sensor_id_mask;
	pr_info("%s sensor_id_mask = 0x%x", __func__, sensor_id_mask);
	if (!sensor_id_mask)
		sensor_id_mask = ~sensor_id_mask;

	sensor_id &= sensor_id_mask;
	sensor_id_mask &= -sensor_id_mask;
	sensor_id_mask -= 1;
	while (sensor_id_mask) {
		sensor_id_mask >>= 1;
		sensor_id >>= 1;
	}
	pr_info("%s sensor_id = %d", __func__, sensor_id);
	return sensor_id;
}

static s32 aon_sensor_match_id(struct aon_sensor_ctrl_t *s_ctrl)
{
	s32 rc = 0;
	u32 chipid = 0;
	struct aon_camera_slave_info *slave_info = NULL;

	if (!s_ctrl) {
		pr_err("%s invalid param", __func__);
		return -EINVAL;
	}

	slave_info = &(s_ctrl->sensordata->slave_info);

	if (!slave_info) {
		pr_err("%s failed: %pK", __func__, slave_info);
		return -EINVAL;
	}
	rc = aon_io_dev_read(
		s_ctrl->client,
		slave_info->sensor_id_reg_addr,
		&chipid, CAMERA_SENSOR_I2C_TYPE_WORD,
		CAMERA_SENSOR_I2C_TYPE_WORD);
	pr_info("%s read id: 0x%x expected id 0x%x",
			 __func__, chipid, slave_info->sensor_id);
	if (aon_sensor_id_by_mask(s_ctrl, chipid) != slave_info->sensor_id) {
		pr_err("%s chip id %x does not match %x", __func__,
				chipid, slave_info->sensor_id);
		return -ENODEV;
	}
	return rc;
}

static s32 aon_sensor_read_register(struct aon_sensor_ctrl_t *s_ctrl,
	struct aon_sensor_cmd_buf_desc *buf_desc, struct aon_reg_setting *reg_settings)
{
	s32 rc = 0;
	s32 i = 0;
	u32 *regs_val_buf = NULL;
	s32 count = 0;

	if (!s_ctrl || !buf_desc || !reg_settings) {
		pr_err("%s invalid param", __func__);
		return -EINVAL;
	}

	count = buf_desc->read_regs_count;
	if (count <= 0) {
		pr_err("%s register count = %d", __func__, count);
		return -EINVAL;
	}

	regs_val_buf = (u32 *)
		kzalloc(count * sizeof(u32), GFP_KERNEL);
	if (!regs_val_buf) {
		pr_err("%s, alloc aon buffer data memory failed", __func__);
		return -ENOMEM;
	}
	pr_info("%s regs_val_buf mm-kzalloc-size: %lld",
		__func__, count * sizeof(u32));

	for (i = 0; i < count; i++) {
		rc = aon_io_dev_read(s_ctrl->client,
			reg_settings[i].register_addr,
			&regs_val_buf[i],
			reg_settings[i].regAddr_type,
			reg_settings[i].regData_type);
		if (rc < 0) {
			pr_err("%s aon_io_dev_read error, return code %d", __func__, rc);
			goto out;
		}
		pr_debug("%s, register addr %d vals %d", __func__, reg_settings[i].register_addr,
			regs_val_buf[i]);
	}

	rc = copy_to_user((char __user *)(buf_desc->read_vals_addr),
		regs_val_buf, count * sizeof(u32));
	if (rc) {
		pr_err("%s, copy_to_user failed, ret = 0x%x", __func__, rc);
		goto out;
	}

out:
	kfree(regs_val_buf);
	pr_info("%s regs_val_buf mm-kfree", __func__);
	return rc;
}

static s32 aon_sensor_power_down(struct aon_sensor_ctrl_t *s_ctrl)
{
	struct aon_sensor_power_ctrl_t *power_info = NULL;
	struct aon_hw_soc_info *soc_info = NULL;
	s32 rc = 0;

	if (!s_ctrl) {
		pr_err("%s invalid param", __func__);
		return -EINVAL;
	}

	power_info = &s_ctrl->sensordata->power_info;
	soc_info = &s_ctrl->soc_info;

	if (!power_info) {
		pr_err("%s failed: power_info %pK", __func__, power_info);
		return -EINVAL;
	}

	rc = aon_sensor_util_power_down(power_info, soc_info);
	if (rc < 0) {
		pr_err("%s power down the core is failed:%d", __func__, rc);
		return rc;
	}

	if (s_ctrl->bob_pwm_switch) {
		pr_info("%s before aon_sensor_bob_pwm_mode_switch", __func__);
		rc = aon_sensor_bob_pwm_mode_switch(soc_info,
			s_ctrl->bob_reg_index, false);
		if (rc) {
			pr_err("%s BoB PWM setup failed rc: %d", __func__, rc);
			rc = 0;
		}
	}

	rc = aon_cci_release(s_ctrl);
	pr_info("%s aon_cci_release rc = %d", __func__, rc);


	return rc;
}

static s32 aon_sensor_i2c_modes_util(
	struct cam_sensor_cci_client *client,
	struct i2c_settings_list *i2c_list)
{
	s32 rc = 0;
	u32 i = 0, size = 0;

	if (!client || !i2c_list)
		return -EINVAL;

	if (i2c_list->op_code == CAM_SENSOR_I2C_WRITE_RANDOM) {
		pr_info("%s CAM_SENSOR_I2C_WRITE_RANDOM", __func__);
		rc = aon_io_dev_write(client,
			&(i2c_list->i2c_settings));
		if (rc < 0) {
			pr_err("%s Failed to random write I2C settings: %d", __func__, rc);
			return rc;
		}
	} else if (i2c_list->op_code == CAM_SENSOR_I2C_WRITE_SEQ) {
		pr_info("%s CAM_SENSOR_I2C_WRITE_SEQ", __func__);
		rc = aon_io_dev_write_continuous(
			client,
			&(i2c_list->i2c_settings),
			0);
		if (rc < 0) {
			pr_err("%s Failed to seq write I2C settings: %d", __func__,
				rc);
			return rc;
		}
	} else if (i2c_list->op_code == CAM_SENSOR_I2C_WRITE_BURST) {
		pr_info("%s CAM_SENSOR_I2C_WRITE_BURST", __func__);
		rc = aon_io_dev_write_continuous(
			client,
			&(i2c_list->i2c_settings),
			1);
		if (rc < 0) {
			pr_err("%s Failed to burst write I2C settings: %d", __func__,
				rc);
			return rc;
		}
	} else if (i2c_list->op_code == CAM_SENSOR_I2C_POLL) {
		pr_info("%s CAM_SENSOR_I2C_POLL", __func__);
		size = i2c_list->i2c_settings.size;
		for (i = 0; i < size; i++) {
			rc = aon_io_dev_poll(
			client,
			i2c_list->i2c_settings.reg_setting[i].reg_addr,
			i2c_list->i2c_settings.reg_setting[i].reg_data,
			i2c_list->i2c_settings.reg_setting[i].data_mask,
			i2c_list->i2c_settings.addr_type,
			i2c_list->i2c_settings.data_type,
			i2c_list->i2c_settings.reg_setting[i].delay);
			if (rc < 0) {
				pr_err("%s i2c poll apply setting Fail: %d", __func__, rc);
				return rc;
			}
		}
	}

	return rc;
}

static s32 aon_sensor_apply_settings(struct aon_sensor_ctrl_t *s_ctrl,
	enum aon_sensor_packet_opcodes opcode)
{
	s32 rc = 0;
	struct i2c_settings_array *i2c_set = NULL;
	struct i2c_settings_list *i2c_list = NULL;

	if (!s_ctrl)
		return -EINVAL;

	switch (opcode) {
	case AON_SENSOR_PACKET_OPCODE_STREAMON: {
		i2c_set = &s_ctrl->i2c_data.streamon_settings;
		pr_info("%s AON_SENSOR_PACKET_OPCODE_STREAMON", __func__);
		break;
	}
	case AON_SENSOR_PACKET_OPCODE_INITIAL_CONFIG: {
		i2c_set = &s_ctrl->i2c_data.init_settings;
		pr_info("%s AON_SENSOR_PACKET_OPCODE_INITIAL_CONFIG", __func__);
		break;
	}
	case AON_SENSOR_PACKET_OPCODE_CONFIG: {
		i2c_set = &s_ctrl->i2c_data.config_settings;
		pr_info("%s AON_SENSOR_PACKET_OPCODE_CONFIG", __func__);
		break;
	}
	case AON_SENSOR_PACKET_OPCODE_STREAMOFF: {
		i2c_set = &s_ctrl->i2c_data.streamoff_settings;
		pr_info("%s AON_SENSOR_PACKET_OPCODE_STREAMOFF", __func__);
		break;
	}
	case AON_SENSOR_PACKET_OPCODE_UPDATE:
	case AON_SENSOR_PACKET_OPCODE_PROBE:
	default:
		pr_info("%s opcode=%d", __func__, opcode);
		return 0;
	}
	if (i2c_set->is_settings_valid == 1) {
		list_for_each_entry(i2c_list,
			&(i2c_set->list_head), list) {
			rc = aon_sensor_i2c_modes_util(s_ctrl->client,
					i2c_list);
			if (rc < 0) {
				pr_err("%s Failed to apply settings: %d", __func__, rc);
				return rc;
			}
		}
	}
	return rc;
}

static void aon_sensor_release_stream_resource(struct aon_sensor_ctrl_t *s_ctrl)
{
	struct i2c_settings_array *i2c_set = NULL;
	s32 rc = 0;

	if (!s_ctrl) {
		pr_info("%s s_ctrl is already null, nothing to do.", __func__);
		return;
	}

	i2c_set = &(s_ctrl->i2c_data.streamoff_settings);
	if (i2c_set->is_settings_valid == 1) {
		i2c_set->is_settings_valid = -1;
		rc = delete_request(i2c_set);
		if (rc < 0)
			pr_err("%s failed while deleting Streamoff settings", __func__);
	}

	i2c_set = &(s_ctrl->i2c_data.streamon_settings);
	if (i2c_set->is_settings_valid == 1) {
		i2c_set->is_settings_valid = -1;
		rc = delete_request(i2c_set);
		if (rc < 0)
			pr_err("%s failed while deleting Streamon settings", __func__);
	}
}

static s32 aon_sensor_update_power_on_settings(s32 count,
	struct aon_power_setting *pwr_settings,
	struct aon_sensor_power_ctrl_t *power_info)
{
	s32 rc = 0;
	s32 i = 0;

	if (!pwr_settings || !power_info || count <= 0) {
		pr_err("%s Invalid Args, %d", __func__, count);
		return -EINVAL;
	}

	power_info->power_setting_size = 0;
	power_info->power_setting =
		(struct aon_sensor_power_setting *)
		kzalloc(sizeof(struct aon_sensor_power_setting) *
			MAX_POWER_CONFIG, GFP_KERNEL);
	if (!power_info->power_setting) {
		pr_err("%s power_setting kzalloc failed", __func__);
		return -ENOMEM;
	}
	pr_info("%s power_setting mm-kzalloc-size: %lld",
		__func__, sizeof(struct aon_sensor_power_setting) * MAX_POWER_CONFIG);

	for (i = 0; i < count; i++) {
		power_info->power_setting[i].seq_type = pwr_settings[i].config_type;
		power_info->power_setting[i].config_val = pwr_settings[i].config_value;
		power_info->power_setting[i].delay = pwr_settings[i].delayMs;
		pr_info("%s [%d], seq_type: %d, config_val: %ld, delay:%d", __func__, i,
			power_info->power_setting[i].seq_type,
			power_info->power_setting[i].config_val,
			power_info->power_setting[i].delay);
	}
	power_info->power_setting_size = count;
	return rc;
}

static s32 aon_sensor_update_power_down_settings(s32 count,
	struct aon_power_setting *pwr_settings,
	struct aon_sensor_power_ctrl_t *power_info)
{
	s32 rc = 0;
	s32 i = 0;

	if (!pwr_settings || !power_info || count <= 0) {
		pr_err("%s Invalid Args, %d", __func__, count);
		return -EINVAL;
	}

	power_info->power_down_setting_size = 0;
	power_info->power_down_setting =
		(struct aon_sensor_power_setting *)
		kzalloc(sizeof(struct aon_sensor_power_setting) *
			MAX_POWER_CONFIG, GFP_KERNEL);
	if (!power_info->power_down_setting) {
		pr_err("%s power_down_setting kzalloc failed", __func__);
		return -ENOMEM;
	}
	pr_info("%s power_down_setting mm-kzalloc-size: %lld",
		__func__, sizeof(struct aon_sensor_power_setting) * MAX_POWER_CONFIG);

	for (i = 0; i < count; i++) {
		power_info->power_down_setting[i].seq_type = pwr_settings[i].config_type;
		power_info->power_down_setting[i].config_val = pwr_settings[i].config_value;
		power_info->power_down_setting[i].delay = pwr_settings[i].delayMs;
		pr_info("%s [%d], seq_type: %d, config_val: %ld, delay:%d", __func__, i,
			power_info->power_down_setting[i].seq_type,
			power_info->power_down_setting[i].config_val,
			power_info->power_down_setting[i].delay);
	}
	power_info->power_down_setting_size = count;
	return rc;
}

static struct i2c_settings_list *aon_sensor_get_i2c_ptr(
	struct i2c_settings_array *i2c_reg_settings, u32 size)
{
	struct i2c_settings_list *tmp = NULL;

	if (!i2c_reg_settings) {
		pr_err("%s i2c_reg_settings is null", __func__);
		return NULL;
	}

	tmp = (struct i2c_settings_list *)
		kzalloc(sizeof(struct i2c_settings_list), GFP_KERNEL);
	if (tmp != NULL)
		list_add_tail(&(tmp->list),
			&(i2c_reg_settings->list_head));
	else {
		pr_err("%s kzalloc failed", __func__);
		return NULL;
	}
	pr_info("%s tmp mm-kzalloc-size: %lld",
		__func__, sizeof(struct i2c_settings_list));

	tmp->i2c_settings.reg_setting = (struct cam_sensor_i2c_reg_array *)
		vzalloc(size * sizeof(struct cam_sensor_i2c_reg_array));
	pr_info("%s tmp->i2c_settings.reg_setting mm-vzalloc-size: %lld",
		__func__, size * sizeof(struct cam_sensor_i2c_reg_array));
	if (tmp->i2c_settings.reg_setting == NULL) {
		pr_err("%s vzalloc failed", __func__);
		list_del(&(tmp->list));
		kfree(tmp);
		pr_info("%s tmp mm-kfree", __func__);
		return NULL;
	}
	tmp->i2c_settings.size = size;

	return tmp;
}

static s32 aon_sensor_handle_random_write(struct i2c_rdwr_header hr,
	struct aon_reg_setting *configs,
	struct i2c_settings_array *i2c_reg_settings)
{
	struct i2c_settings_list *i2c_list = NULL;
	s32 rc = 0, cnt = 0;

	if (!configs || !i2c_reg_settings)
		return -EINVAL;

	i2c_list = aon_sensor_get_i2c_ptr(i2c_reg_settings, hr.count);
	if (i2c_list == NULL || i2c_list->i2c_settings.reg_setting == NULL) {
		pr_err("%s Failed in allocating i2c_list", __func__);
		return -ENOMEM;
	}

	i2c_list->op_code = CAM_SENSOR_I2C_WRITE_RANDOM;
	i2c_list->i2c_settings.addr_type = hr.addr_type;
	i2c_list->i2c_settings.data_type = hr.data_type;

	for (cnt = 0; cnt < hr.count; cnt++) {
		i2c_list->i2c_settings.reg_setting[cnt].reg_addr =
			configs[cnt].register_addr;
		i2c_list->i2c_settings.reg_setting[cnt].reg_data =
			configs[cnt].register_data;
		i2c_list->i2c_settings.reg_setting[cnt].data_mask = 0;
	}

	return rc;
}

static s32 aon_prepare_config_cmd(u32 op_code,
	struct aon_sensor_ctrl_t *s_ctrl, s32 count,
	struct aon_reg_setting *configs)
{
	s32 rc = 0;
	struct i2c_data_settings *i2c_data = NULL;
	struct i2c_settings_array *i2c_reg_settings = NULL;
	struct i2c_rdwr_header hr = {0};

	if (!s_ctrl || !configs) {
		pr_err("%s invalid param", __func__);
		return -EINVAL;
	}

	i2c_data = &(s_ctrl->i2c_data);
	pr_info("%s OpCode: %d", __func__, op_code);
	switch (op_code & 0xFFFFFF) {
	case AON_SENSOR_PACKET_OPCODE_INITIAL_CONFIG: {
		pr_info("%s AON_SENSOR_PACKET_OPCODE_INITIAL_CONFIG", __func__);
		i2c_reg_settings = &i2c_data->init_settings;
		i2c_reg_settings->is_settings_valid = 1;
		break;
	}
	case AON_SENSOR_PACKET_OPCODE_CONFIG: {
		pr_info("%s AON_SENSOR_PACKET_OPCODE_CONFIG", __func__);
		i2c_reg_settings = &i2c_data->config_settings;
		i2c_reg_settings->is_settings_valid = 1;
		break;
	}
	case AON_SENSOR_PACKET_OPCODE_STREAMON: {
		pr_info("%s AON_SENSOR_PACKET_OPCODE_STREAMON", __func__);
		if (s_ctrl->streamon_count > 0) {
			pr_info("%s s_ctrl->streamon_count > 0", __func__);
			return rc;
		}

		s_ctrl->streamon_count = s_ctrl->streamon_count + 1;
		i2c_reg_settings = &i2c_data->streamon_settings;
		i2c_reg_settings->is_settings_valid = 1;
		break;
	}
	case AON_SENSOR_PACKET_OPCODE_STREAMOFF: {
		pr_info("%s AON_SENSOR_PACKET_OPCODE_STREAMOFF", __func__);
		if (s_ctrl->streamoff_count > 0) {
			pr_info("%s s_ctrl->streamon_count > 0", __func__);
			return rc;
		}

		s_ctrl->streamoff_count = s_ctrl->streamoff_count + 1;
		i2c_reg_settings = &i2c_data->streamoff_settings;
		i2c_reg_settings->is_settings_valid = 1;
		break;
	}

	case AON_SENSOR_PACKET_OPCODE_UPDATE: {
		pr_info("%s AON_SENSOR_PACKET_OPCODE_UPDATE", __func__);
		if ((s_ctrl->sensor_state == AON_SENSOR_INIT) ||
			(s_ctrl->sensor_state == AON_SENSOR_ACQUIRE)) {
			pr_err("%s Rxed Update packets without linking", __func__);
			return rc;
		}
		break;
	}
	case AON_SENSOR_PACKET_OPCODE_NOP: {
		pr_info("%s AON_SENSOR_PACKET_OPCODE_NOP", __func__);
		if ((s_ctrl->sensor_state == AON_SENSOR_INIT) ||
			(s_ctrl->sensor_state == AON_SENSOR_ACQUIRE)) {
			pr_err("%s Rxed NOP packets without linking", __func__);
			return rc;
		}
		return rc;
	}
	default:
		pr_err("%s Invalid Packet Header", __func__);
		rc = -EINVAL;
		return rc;
	}

	hr.count = count;
	hr.data_type = configs[0].regData_type;
	hr.addr_type = configs[0].regAddr_type;

	rc = aon_sensor_handle_random_write(hr,
		configs, i2c_reg_settings);
	return rc;
}

static void aon_free_power_mem(struct aon_sensor_power_ctrl_t *power_info)
{
	if (!power_info)
		return;

	kfree(power_info->power_setting);
	pr_info("%s power_setting mm-kfree", __func__);
	power_info->power_setting = NULL;
	power_info->power_setting_size = 0;

	kfree(power_info->power_down_setting);
	pr_info("%s power_down_setting mm-kfree", __func__);
	power_info->power_down_setting = NULL;
	power_info->power_down_setting_size = 0;
}


int explorer_aon_drv_cmd(void **pp_param, void *arg)
{
	struct aon_sensor_ctrl_t **pp_ctrl = (struct aon_sensor_ctrl_t **)pp_param;
	struct aon_sensor_cmd *aon_data = NULL;
	struct aon_sensor_power_ctrl_t *power_info = NULL;
	int rc = 0;
	void *buf_data = NULL;
	struct aon_sensor_cmd_buf_desc *buf_desc = NULL;
	struct aon_slave_info_data *pslave_info = NULL;
	struct aon_power_setting *paon_powerup_setting = NULL;
	struct aon_power_setting *paon_powerdown_setting = NULL;
	struct aon_reg_setting *paon_reg_setting = NULL;
	u32 op_code = AON_SENSOR_PACKET_OPCODE_CONFIG;

	pr_info("%s *pp_ctrl: %p\n", __func__, *pp_ctrl);
	if (!arg) {
		pr_err("%s invalid args\n", __func__);
		return -EINVAL;
	}

	aon_data = (struct aon_sensor_cmd *)arg;
	buf_desc = &aon_data->aon_cmd_bufdesc;
	pr_info("aon cmd buf desc: slave_offset: %d, "
		"powerup_count: %d, powerup_offset: %d, "
		"powerdown_count: %d, powerdown_offset: %d, "
		"readregs_offset: %d, readregs_count: %d, readvals_addr: %lld, "
		"init_valid: %u, streamon_valid: %u, streamoff_valid: %u, "
		"res_valid: %u, mclk_enabled: %u",
		buf_desc->slave_info_offset,
		buf_desc->powerup_setting_count,
		buf_desc->powerup_setting_offset,
		buf_desc->powerdown_setting_count,
		buf_desc->powerdown_setting_offset,
		buf_desc->read_regs_offset,
		buf_desc->read_regs_count,
		buf_desc->read_vals_addr,
		buf_desc->is_init_config_valid,
		buf_desc->is_streamon_config_valid,
		buf_desc->is_streamoff_config_valid,
		buf_desc->is_res_config_valid,
		buf_desc->mclk_enabled);
	buf_data = kzalloc(aon_data->aon_cmd_bufsize, GFP_KERNEL);
	if (!buf_data) {
		pr_err("%s, alloc aon buffer data memory failed.\n", __func__);
		return -ENOMEM;
	}
	pr_info("%s buf_data mm-kzalloc-size: %lld",
		__func__, aon_data->aon_cmd_bufsize);
	rc = copy_from_user(buf_data, (char __user *)(aon_data->aon_cmd_bufhandle),
		aon_data->aon_cmd_bufsize);
	if (rc) {
		pr_err("%s, copy_from_user failed, ret = 0x%x.\n", __func__, rc);
		kfree(buf_data);
		buf_data = NULL;
		pr_info("%s buf_data mm-kfree", __func__);
		return rc;
	}

	switch (aon_data->aon_cmd_opcode) {
	case AON_SENSOR_CMD_PROBE: {
		pr_info("%s, AON_SENSOR_CMD_PROBE.\n", __func__);

		if (!(*pp_ctrl)) {
			rc = explorer_aon_init(pp_ctrl);
			if (rc || !(*pp_ctrl)) {
				pr_err("%s explorer_aon_init failed", __func__);
				break;
			}
		}

		mutex_lock(&((*pp_ctrl)->aon_sensor_mutex));

		if ((*pp_ctrl)->is_power_on == 1) {
			pr_info("%s Already Sensor power on, to power down\n", __func__);
			rc = aon_sensor_power_down((*pp_ctrl));
			if (rc < 0) {
				pr_err("fail in Sensor Power Down");
				goto free_power_settings;
			}
			(*pp_ctrl)->is_power_on = 0;
		}

		if (buf_desc->slave_info_offset >= 0) {
			struct aon_cmd_i2c_info i2c_info = {0};
			struct aon_cmd_probe probe_info = {0};

			pslave_info = (struct aon_slave_info_data *)
				(buf_data + buf_desc->slave_info_offset);
			pr_info("aon slaveinfo: slaveaddr: 0x%x, sensor_id_regaddr: 0x%x,"
				" sensor_id: 0x%x, sensor_id_mask: 0x%x, "
				"i2c_frequency_mode: %d, reg_addrtype: %d, reg_datatype: %d\n",
				pslave_info->slave_address,
				pslave_info->sensor_id_regaddr,
				pslave_info->sensor_id,
				pslave_info->sensor_id_mask,
				(int)(pslave_info->i2c_frequency_mode),
				(int)(pslave_info->reg_addrtype),
				(int)(pslave_info->reg_datatype));

			i2c_info.slave_addr = pslave_info->slave_address;
			i2c_info.i2c_freq_mode = pslave_info->i2c_frequency_mode;

			probe_info.data_type = pslave_info->reg_datatype;
			probe_info.addr_type = pslave_info->reg_addrtype;
			probe_info.reg_addr = pslave_info->sensor_id_regaddr;
			probe_info.expected_data = pslave_info->sensor_id;
			probe_info.data_mask = pslave_info->sensor_id_mask;

			rc = aon_sensor_update_i2c_info(&i2c_info, (*pp_ctrl));
			pr_info("%s aon_sensor_update_i2c_info rc=%d", __func__, rc);
			if (rc)
				goto release_mutex;

			rc |= aon_sensor_update_slave_info(&probe_info, (*pp_ctrl));
			pr_info("%s aon_sensor_update_slave_info rc=%d", __func__, rc);
			if (rc)
				goto release_mutex;

			paon_powerup_setting = (struct aon_power_setting *)
				(buf_data + buf_desc->powerup_setting_offset);
			rc = aon_sensor_update_power_on_settings(
					buf_desc->powerup_setting_count,
					paon_powerup_setting,
					&(*pp_ctrl)->sensordata->power_info);
			if (rc) {
				pr_info("aon_sensor_update_power_on_settings failed");
				goto release_mutex;
			}

			paon_powerdown_setting = (struct aon_power_setting *)
				(buf_data + buf_desc->powerdown_setting_offset);
			rc = aon_sensor_update_power_down_settings(
					buf_desc->powerdown_setting_count,
					paon_powerdown_setting,
					&(*pp_ctrl)->sensordata->power_info);
			if (rc) {
				pr_info("aon_sensor_update_power_down_settings failed");
				kfree((*pp_ctrl)->sensordata->power_info.power_setting);
				pr_info("%s power_setting mm-kfree",__func__);
				(*pp_ctrl)->sensordata->power_info.power_setting = NULL;
				(*pp_ctrl)->sensordata->power_info.power_setting_size = 0;
				goto release_mutex;
			}
		}

		/* Parse and fill vreg params for powerup settings */
		rc = aon_fill_vreg_params(
			&(*pp_ctrl)->soc_info,
			(*pp_ctrl)->sensordata->power_info.power_setting,
			(*pp_ctrl)->sensordata->power_info.power_setting_size);
		if (rc < 0) {
			pr_err("Fail in filling vreg params for PUP rc %d\n", rc);
			goto free_power_settings;
		}

		/* Parse and fill vreg params for powerdown settings*/
		rc = aon_fill_vreg_params(
			&(*pp_ctrl)->soc_info,
			(*pp_ctrl)->sensordata->power_info.power_down_setting,
			(*pp_ctrl)->sensordata->power_info.power_down_setting_size);
		if (rc < 0) {
			pr_err("Fail in filling vreg params for PDOWN rc %d\n", rc);
			goto free_power_settings;
		}

		/* Power up and probe sensor */
		rc = aon_sensor_power_up((*pp_ctrl));
		if (rc < 0) {
			pr_err("power up failed\n");
			goto free_power_settings;
		} else {
			pr_info("aon_sensor_power_up success\n");
			(*pp_ctrl)->is_power_on = 1;
		}

		rc = aon_cci_init((*pp_ctrl));
		if (rc < 0)
			goto free_power_settings;

		/* Match sensor ID */
		rc = aon_sensor_match_id((*pp_ctrl));
		if (rc < 0) {
			aon_sensor_power_down((*pp_ctrl));
			(*pp_ctrl)->is_power_on = 0;
			msleep(20);
			rc = aon_cci_release((*pp_ctrl));
			pr_info("%s aon_cci_release rc = %d", __func__);
			goto free_power_settings;
		}

		pr_info("Probe success,slot:%d,slave_addr:0x%x,sensor_id:0x%x",
			(*pp_ctrl)->soc_info.index,
			(*pp_ctrl)->sensordata->slave_info.sensor_slave_addr,
			(*pp_ctrl)->sensordata->slave_info.sensor_id);

		rc = aon_sensor_power_down((*pp_ctrl));
		if (rc < 0) {
			pr_err("fail in Sensor Power Down");
			rc = aon_cci_release((*pp_ctrl));
			pr_info("%s aon_cci_release rc = %d", __func__);
			goto free_power_settings;
		}
		rc = aon_cci_release((*pp_ctrl));
		pr_info("%s aon_cci_release rc = %d", __func__);
		(*pp_ctrl)->is_probe_succeed = 1;
		(*pp_ctrl)->is_power_on = 0;
		(*pp_ctrl)->sensor_state = AON_SENSOR_INIT;
	}
	break;
	case AON_SENSOR_CMD_ACQUIRE: {
		pr_info("%s, AON_SENSOR_CMD_ACQUIRE.\n", __func__);
		if (!(*pp_ctrl)) {
			pr_err("%s, s_ctrl is null", __func__);
			rc = -EINVAL;
			goto free_bufdata;
		}
		mutex_lock(&((*pp_ctrl)->aon_sensor_mutex));

		if (((*pp_ctrl)->is_probe_succeed == 0) ||
			((*pp_ctrl)->sensor_state != AON_SENSOR_INIT)) {
			pr_err("Not in right state to aquire %d", (*pp_ctrl)->sensor_state);
			rc = -EINVAL;
			goto release_mutex;
		}

		rc = aon_sensor_power_up((*pp_ctrl));
		if (rc < 0) {
			pr_err("Sensor Power up failed");
			goto release_mutex;
		}
		(*pp_ctrl)->is_power_on = 1;
		(*pp_ctrl)->sensor_state = AON_SENSOR_ACQUIRE;
	}
	break;

	case AON_SENSOR_CMD_CONFIG: {
		pr_info("%s, AON_SENSOR_CMD_CONFIG.\n", __func__);
		if (!(*pp_ctrl) || ((*pp_ctrl)->sensor_state == AON_SENSOR_INIT)) {
			pr_err("%s, s_ctrl is null", __func__);
			rc = -EINVAL;
			goto free_bufdata;
		}
		mutex_lock(&((*pp_ctrl)->aon_sensor_mutex));

		/* there is only one config valid at one time */
		if (buf_desc->is_init_config_valid) {
			op_code = AON_SENSOR_PACKET_OPCODE_INITIAL_CONFIG;
			pr_info("%s INITIAL CONFIG", __func__);
		} else if (buf_desc->is_res_config_valid) {
			op_code = AON_SENSOR_PACKET_OPCODE_CONFIG;
			pr_info("%s RES CONFIG", __func__);
		} else if (buf_desc->is_streamon_config_valid) {
			op_code = AON_SENSOR_PACKET_OPCODE_STREAMON;
			pr_info("%s STREAMON CONFIG", __func__);
		} else if (buf_desc->is_streamoff_config_valid) {
			op_code = AON_SENSOR_PACKET_OPCODE_STREAMOFF;
			pr_info("%s STREAMOFF CONFIG", __func__);
		} else {
			pr_info("%s NOT supported CONFIG", __func__);
			goto release_mutex;
		}

		rc = aon_cci_init((*pp_ctrl));
		pr_info("%s, aon_cci_init rc = %d", __func__);
		if (rc < 0)
			goto release_mutex;

		paon_reg_setting = (struct aon_reg_setting *)
			(buf_data + buf_desc->config_regsetting_offset);
		aon_prepare_config_cmd(op_code, (*pp_ctrl),
			buf_desc->config_regsetting_count, paon_reg_setting);

		if ((*pp_ctrl)->i2c_data.init_settings.is_settings_valid) {

			rc = aon_sensor_apply_settings((*pp_ctrl),
				AON_SENSOR_PACKET_OPCODE_INITIAL_CONFIG);

			if (rc < 0) {
				pr_err("cannot apply init settings");
				delete_request(&(*pp_ctrl)->i2c_data.init_settings);
				rc = aon_cci_release((*pp_ctrl));
				pr_info("%s, aon_cci_release rc = %d", __func__);
				goto release_mutex;
			}
			rc = delete_request(&(*pp_ctrl)->i2c_data.init_settings);
			if (rc < 0) {
				pr_err("Fail in deleting the Init settings");
				rc = aon_cci_release((*pp_ctrl));
				pr_info("%s, aon_cci_release rc = %d", __func__);
				goto release_mutex;
			}
		}

		if ((*pp_ctrl)->i2c_data.config_settings.is_settings_valid) {
			pr_info("%s to config resolution settings", __func__);
			rc = aon_sensor_apply_settings((*pp_ctrl),
				AON_SENSOR_PACKET_OPCODE_CONFIG);

			if (rc < 0) {
				pr_err("cannot apply config settings");
				delete_request(&(*pp_ctrl)->i2c_data.config_settings);
				rc = aon_cci_release((*pp_ctrl));
				pr_info("%s, aon_cci_release rc = %d", __func__);
				goto release_mutex;
			}
			rc = delete_request(&(*pp_ctrl)->i2c_data.config_settings);
			if (rc < 0) {
				pr_err("Fail in deleting the config settings");
				rc = aon_cci_release((*pp_ctrl));
				pr_info("%s, aon_cci_release rc = %d", __func__);
				goto release_mutex;
			}
			if ((*pp_ctrl)->sensor_state == AON_SENSOR_START) {
				pr_info("%s keep AON_SENSOR_START after config cmd", __func__);
			} else {
				pr_info("%s enter state AON_SENSOR_CONFIG", __func__);
				(*pp_ctrl)->sensor_state = AON_SENSOR_CONFIG;
			}
		}
		rc = aon_cci_release((*pp_ctrl));
		pr_info("%s, aon_cci_release rc = %d", __func__);
	}
	break;
	case AON_SENSOR_CMD_START: {
		pr_info("%s, AON_SENSOR_CMD_START.\n", __func__);
		if (!(*pp_ctrl)) {
			pr_err("%s, s_ctrl is null", __func__);
			rc = -EINVAL;
			goto free_bufdata;
		}
		mutex_lock(&((*pp_ctrl)->aon_sensor_mutex));
		if (((*pp_ctrl)->sensor_state == AON_SENSOR_INIT) ||
			((*pp_ctrl)->sensor_state == AON_SENSOR_START)) {
			rc = -EINVAL;
			pr_err("%s Not in right state to start : %d", __func__,
			(*pp_ctrl)->sensor_state);
			goto release_mutex;
		}

		rc = aon_cci_init((*pp_ctrl));
		pr_info("%s, aon_cci_init rc = %d", __func__);
		if (rc < 0)
			goto release_mutex;

		if ((*pp_ctrl)->i2c_data.streamon_settings.is_settings_valid) {
			pr_info("%s to apply streamon setting", __func__);
			rc = aon_sensor_apply_settings((*pp_ctrl),
				AON_SENSOR_PACKET_OPCODE_STREAMON);
			if (rc < 0) {
				pr_err("aon cannot apply streamon settings");
				rc = aon_cci_release((*pp_ctrl));
				pr_info("%s, aon_cci_release rc = %d", __func__);
				goto release_mutex;
			}
		}
		(*pp_ctrl)->sensor_state = AON_SENSOR_START;
		pr_info("%s START Success, sensor_id:0x%x,sensor_slave_addr:0x%x",
			__func__,
			(*pp_ctrl)->sensordata->slave_info.sensor_id,
			(*pp_ctrl)->sensordata->slave_info.sensor_slave_addr);
		rc = aon_cci_release((*pp_ctrl));
		pr_info("%s, aon_cci_release rc = %d", __func__);
	}
	break;
	case AON_SENSOR_CMD_STOP: {
		pr_info("%s, AON_SENSOR_CMD_STOP.\n", __func__);
		if (!(*pp_ctrl)) {
			pr_err("%s, s_ctrl is null", __func__);
			rc = -EINVAL;
			goto free_bufdata;
		}
		mutex_lock(&((*pp_ctrl)->aon_sensor_mutex));
		if ((*pp_ctrl)->sensor_state != AON_SENSOR_START) {
			rc = -EINVAL;
			pr_err("%s Not in right state to stop : %d", __func__,
			(*pp_ctrl)->sensor_state);
			goto release_mutex;
		}

		rc = aon_cci_init((*pp_ctrl));
		pr_info("%s, aon_cci_init rc = %d", __func__);
		if (rc < 0)
			goto release_mutex;

		if ((*pp_ctrl)->i2c_data.streamoff_settings.is_settings_valid) {
			pr_info("%s to apply streamoff setting", __func__);
			rc = aon_sensor_apply_settings((*pp_ctrl),
				AON_SENSOR_PACKET_OPCODE_STREAMOFF);
			if (rc < 0) {
				pr_err("%s cannot apply streamoff settings, rc = %d", __func__, rc);
				rc = aon_cci_release((*pp_ctrl));
				pr_info("%s, aon_cci_release rc = %d", __func__);
				goto release_mutex;
			}
		}

		(*pp_ctrl)->sensor_state = AON_SENSOR_ACQUIRE;

		pr_info("%s STOP Success, sensor_id:0x%x,sensor_slave_addr:0x%x",
			__func__,
			(*pp_ctrl)->sensordata->slave_info.sensor_id,
			(*pp_ctrl)->sensordata->slave_info.sensor_slave_addr);
		rc = aon_cci_release((*pp_ctrl));
		pr_info("%s, aon_cci_release rc = %d", __func__);
	}
	break;
	case AON_SENSOR_CMD_RELEASE: {
		pr_info("%s, AON_SENSOR_CMD_RELEASE.\n", __func__);
		if (!(*pp_ctrl)) {
			pr_err("%s, s_ctrl is null", __func__);
			goto free_bufdata;
		}

		mutex_lock(&((*pp_ctrl)->aon_sensor_mutex));

		if ((*pp_ctrl)->is_power_on == 1) {
			pr_info("%s, to power down", __func__);
			rc = aon_sensor_power_down((*pp_ctrl));
			if (rc < 0) {
				pr_err("%s Sensor Power Down failed", __func__);
				goto release_mutex;
			}
			(*pp_ctrl)->is_power_on = 0;
			pr_info("%s, power down OK", __func__);
		}

		aon_sensor_release_stream_resource((*pp_ctrl));

		(*pp_ctrl)->sensor_state = AON_SENSOR_INIT;
		pr_info("%s RELEASE Success, sensor_id:0x%x, sensor_slave_addr:0x%x",
			__func__,
			(*pp_ctrl)->sensordata->slave_info.sensor_id,
			(*pp_ctrl)->sensordata->slave_info.sensor_slave_addr);
		(*pp_ctrl)->streamon_count = 0;
		(*pp_ctrl)->streamoff_count = 0;
		kfree(buf_data);
		pr_info("%s buf_data mm-kfree", __func__);
		aon_dinit_variables();
		aon_cci_is_on = -1;
		if ((*pp_ctrl))
			mutex_unlock(&((*pp_ctrl)->aon_sensor_mutex));
		explorer_aon_exit(pp_ctrl);
		return rc;
	}
	break;
	case AON_SENSOR_CMD_READREG: {
		pr_info("%s, AON_SENSOR_CMD_READREG.\n", __func__);
		if (!(*pp_ctrl)) {
			pr_err("%s, s_ctrl is null", __func__);
			rc = -EINVAL;
			goto free_bufdata;
		}
		mutex_lock(&((*pp_ctrl)->aon_sensor_mutex));

		rc = aon_cci_init((*pp_ctrl));
		pr_info("%s, aon_cci_init rc = %d", __func__);
		if (rc < 0)
			goto release_mutex;

		if (buf_desc->read_regs_offset >= 0) {
			paon_reg_setting = (struct aon_reg_setting *)
				(buf_data + buf_desc->read_regs_offset);

			rc = aon_sensor_read_register((*pp_ctrl), buf_desc, paon_reg_setting);
			if (rc) {
				pr_err("%s aon_sensor_read_register error", __func__);
				rc = aon_cci_release((*pp_ctrl));
				pr_info("%s, aon_cci_release rc = %d", __func__);
				goto release_mutex;
			}
		} else {
			pr_err("%s read regs offset has wrong value %d", __func__,
				buf_desc->read_regs_offset);
			rc = aon_cci_release((*pp_ctrl));
			pr_info("%s, aon_cci_release rc = %d", __func__);
			goto release_mutex;
		}
		pr_info("%s read regs end", __func__);
		rc = aon_cci_release((*pp_ctrl));
		pr_info("%s, aon_cci_release rc = %d", __func__);
	}
	break;
	case AON_SENSOR_CMD_OPMCLK: {
		buf_desc->mclk_enabled = !!(buf_desc->mclk_enabled);
		pr_info("%s, AON_SENSOR_CMD_OPMCLK: %s", __func__,
			(buf_desc->mclk_enabled == 1) ? "enable" : "disable");
		if (!(*pp_ctrl)) {
			pr_err("%s, s_ctrl is null", __func__);
			rc = -EINVAL;
			goto free_bufdata;
		}
		mutex_lock(&((*pp_ctrl)->aon_sensor_mutex));

		if (buf_desc->mclk_enabled == 1) {
			rc = aon_enable_mclk(&(*pp_ctrl)->sensordata->power_info,
			&(*pp_ctrl)->soc_info);
			if (rc < 0) {
				pr_err("%s aon_enable_mclk failed, rc = %d", __func__, rc);
				goto release_mutex;
			}
		}
		if (buf_desc->mclk_enabled == 0) {
			rc = aon_disable_mclk(&(*pp_ctrl)->sensordata->power_info,
				&(*pp_ctrl)->soc_info);
			if (rc < 0) {
				pr_err("%s aon_disable_mclk failed, rc = %d", __func__, rc);
				goto release_mutex;
			}
		}
		pr_info("%s AON_SENSOR_CMD_OPMCLK end", __func__);

	}
	break;
	default: {
		pr_info("%s, unsupported cmd.\n", __func__);
		rc = -EINVAL;
		goto free_bufdata;
	}
	break;
	}

release_mutex:
	if ((*pp_ctrl))
		mutex_unlock(&((*pp_ctrl)->aon_sensor_mutex));
free_bufdata:
	kfree(buf_data);
	pr_info("%s buf_data mm-kfree", __func__);
	buf_data = NULL;
	return rc;

free_power_settings:
	power_info = &(*pp_ctrl)->sensordata->power_info;
	aon_free_power_mem(power_info);
	mutex_unlock(&((*pp_ctrl)->aon_sensor_mutex));
	kfree(buf_data);
	pr_info("%s buf_data mm-kfree", __func__);
	return rc;
}

static void aon_sensor_shutdown(struct aon_sensor_ctrl_t *s_ctrl)
{
	struct aon_sensor_power_ctrl_t *power_info = NULL;

	if (!s_ctrl) {
		pr_info("%s s_ctrl is already null, nothing to do.", __func__);
		return;
	}

	if ((s_ctrl->sensor_state == AON_SENSOR_INIT) &&
		(s_ctrl->is_probe_succeed == 0)) {
		pr_info("stat is AON_SENSOR_INIT && is_probe_succeed == 0");
		return;
	}

	aon_sensor_release_stream_resource(s_ctrl);

	if (s_ctrl->sensor_state != AON_SENSOR_INIT)
		aon_sensor_power_down(s_ctrl);

	power_info = &s_ctrl->sensordata->power_info;
	aon_free_power_mem(power_info);

	s_ctrl->streamon_count = 0;
	s_ctrl->streamoff_count = 0;
	s_ctrl->is_probe_succeed = 0;
	s_ctrl->is_power_on = 0;
	s_ctrl->sensor_state = AON_SENSOR_INIT;
}

static void aon_sensor_platform_remove(void)
{
	s32 i = 0;
	struct aon_sensor_ctrl_t *s_ctrl = NULL;
	struct aon_hw_soc_info *soc_info = NULL;

	s_ctrl = g_ctrl;
	if (!s_ctrl) {
		pr_err("%s aon sensor device is NULL, nothing to do.", __func__);
		return;
	}

	mutex_lock(&(s_ctrl->aon_sensor_mutex));
	aon_sensor_shutdown(s_ctrl);

	kfree(s_ctrl->sensordata->power_info.gpio_num_info);
	pr_info("%s sensordata->power_info.gpio_num_info mm-kfree", __func__);
	s_ctrl->sensordata->power_info.gpio_num_info = NULL;
	kfree(s_ctrl->sensordata);
	s_ctrl->sensordata = NULL;
	pr_info("%s s_ctrl->sensordata mm-kfree", __func__);

	soc_info = &s_ctrl->soc_info;
	for (i = 0; i < soc_info->num_clk; i++)
		devm_clk_put(soc_info->dev, soc_info->clk[i]);
	pr_info("%s after devm_clk_put", __func__);

	if (soc_info->gpio_data) {
		kfree(soc_info->gpio_data->cam_gpio_common_tbl);
		pr_info("%s gpio_data->cam_gpio_common_tbl mm-kfree", __func__);
		soc_info->gpio_data->cam_gpio_common_tbl = NULL;
		soc_info->gpio_data->cam_gpio_common_tbl_size = 0;

		kfree(soc_info->gpio_data->cam_gpio_req_tbl);
		pr_info("%s gpio_data->cam_gpio_req_tbl mm-kfree", __func__);
		soc_info->gpio_data->cam_gpio_req_tbl = NULL;
		soc_info->gpio_data->cam_gpio_req_tbl_size = 0;

		kfree(soc_info->gpio_data);
		pr_info("%s soc_info->gpio_data mm-kfree", __func__);
		soc_info->gpio_data = NULL;
	}

	kfree(s_ctrl->client);
	pr_info("%s s_ctrl->client mm-kfree", __func__);
	s_ctrl->client = NULL;
	mutex_unlock(&(s_ctrl->aon_sensor_mutex));

	kfree(s_ctrl);
	pr_info("%s s_ctrl mm-kfree", __func__);
	s_ctrl = NULL;
	g_ctrl = s_ctrl;
}

static s32 aon_sensor_driver_platform_probe(void)
{
	s32 rc = 0;
	struct aon_sensor_ctrl_t *s_ctrl = NULL;
	struct aon_hw_soc_info *soc_info = NULL;

	s_ctrl = kzalloc(sizeof(struct aon_sensor_ctrl_t), GFP_KERNEL);
	if (!s_ctrl) {
		pr_err("%s kzalloc failed", __func__);
		return -ENOMEM;
	}
	pr_info("%s s_ctrl mm-kzalloc-size: %lld",
		__func__, sizeof(struct aon_sensor_ctrl_t));

	s_ctrl->is_probe_succeed = 0;
	s_ctrl->is_power_on = 0;

	rc = aon_sensor_parse_dt(s_ctrl);
	if (rc < 0) {
		pr_err("aon failed: cam_sensor_parse_dt rc %d", rc);
		goto free_s_ctrl;
	}

	/* Fill platform device id*/
	soc_info = &s_ctrl->soc_info;
	s_ctrl->pdev->id = soc_info->index;

	INIT_LIST_HEAD(&(s_ctrl->i2c_data.init_settings.list_head));
	INIT_LIST_HEAD(&(s_ctrl->i2c_data.config_settings.list_head));
	INIT_LIST_HEAD(&(s_ctrl->i2c_data.streamon_settings.list_head));
	INIT_LIST_HEAD(&(s_ctrl->i2c_data.streamoff_settings.list_head));

	if (s_ctrl->sensordata)
		s_ctrl->sensordata->power_info.dev = &s_ctrl->pdev->dev;
	s_ctrl->sensor_state = AON_SENSOR_INIT;

	g_ctrl = s_ctrl;

	return rc;
free_s_ctrl:
	kfree(s_ctrl);
	pr_info("%s s_ctrl mm-kfree", __func__);
	s_ctrl = NULL;
	g_ctrl = s_ctrl;
	return rc;
}

int explorer_aon_init(struct aon_sensor_ctrl_t **ctrl)
{
	int rc = 0;

	rc = aon_sensor_driver_platform_probe();
	*ctrl = g_ctrl;

	return rc;
}

void explorer_aon_exit(struct aon_sensor_ctrl_t **ctrl)
{
	aon_sensor_platform_remove();
	*ctrl = g_ctrl;
}

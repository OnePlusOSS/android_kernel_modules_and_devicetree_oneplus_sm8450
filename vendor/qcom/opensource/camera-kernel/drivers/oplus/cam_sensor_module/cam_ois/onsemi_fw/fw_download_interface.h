#ifndef _DOWNLOAD_OIS_FW_H_
#define _DOWNLOAD_OIS_FW_H_

#include <linux/module.h>
#include <linux/firmware.h>
#include <cam_sensor_cmn_header.h>
#include "cam_ois_dev.h"
#include "cam_ois_core.h"
#include "cam_ois_soc.h"
#include "cam_sensor_util.h"
#include "cam_debug_util.h"
#include "cam_res_mgr_api.h"
#include "cam_common_util.h"

#include <linux/string.h>
#include <linux/time.h>
#include <linux/types.h>

//int RamWrite32A(uint32_t addr, uint32_t data);
//int RamRead32A(uint32_t addr, uint32_t* data);
int RamWrite32A_oneplus(struct cam_ois_ctrl_t *o_ctrl, uint32_t addr, uint32_t data);
int RamRead32A_oneplus(struct cam_ois_ctrl_t *o_ctrl, uint32_t addr, uint32_t* data);
int DownloadFW(struct cam_ois_ctrl_t *o_ctrl);
int OISControl(struct cam_ois_ctrl_t *o_ctrl);
void ReadOISHALLData(struct cam_ois_ctrl_t *o_ctrl, void *data);
void ReadOISHALLDataV2(struct cam_ois_ctrl_t *o_ctrl, void *data);
void ReadOISHALLDataV3(struct cam_ois_ctrl_t *o_ctrl, void *data);
bool IsOISReady(struct cam_ois_ctrl_t *o_ctrl);
void InitOIS(struct cam_ois_ctrl_t *o_ctrl);
void DeinitOIS(struct cam_ois_ctrl_t *o_ctrl);
void InitOISResource(struct cam_ois_ctrl_t *o_ctrl);
int OIS_READ_HALL_DATA_TO_UMD (struct cam_ois_ctrl_t *o_ctrl,struct i2c_settings_array *i2c_settings);
int WRITE_QTIMER_TO_OIS (struct cam_ois_ctrl_t *o_ctrl);
int OIS_READ_HALL_DATA_TO_UMD_NEW (struct cam_ois_ctrl_t *o_ctrl,struct i2c_settings_array *i2c_settings);
int OIS_READ_HALL_DATA_TO_UMD_TELE124 (struct cam_ois_ctrl_t *o_ctrl,struct i2c_settings_array *i2c_settings);
int OIS_READ_HALL_DATA_TO_UMD_129 (struct cam_ois_ctrl_t *o_ctrl,struct i2c_settings_array *i2c_settings);

int StoreOisGyroGian(struct cam_ois_ctrl_t *o_ctrl);
int WriteOisGyroGianToRam(struct cam_ois_ctrl_t *o_ctrl,DUAL_OIS_CALI_RESULTS* current_gyro_gain);
int DoDualOisGyroOffset(struct cam_ois_ctrl_t *o_ctrl, uint32_t *gyro_data);
int QueryDualOisSmaWireStatus(struct cam_ois_ctrl_t *o_ctrl, uint32_t *sma_wire_broken);
int GetDualOisGyroGain(struct cam_ois_ctrl_t *o_ctrl,DUAL_OIS_GYROGAIN* initial_gyro_gain);
int WriteDualOisShiftRegister(struct cam_ois_ctrl_t *o_ctrl, uint32_t distance);

int32_t oplus_cam_ois_construct_default_power_setting(struct cam_sensor_power_ctrl_t *power_info);
int32_t oplus_cam_ois_construct_default_power_setting_129(struct cam_sensor_power_ctrl_t *power_info);

int cam_ois_download_start(struct cam_ois_ctrl_t *o_ctrl);
void cam_ois_do_power_down(struct cam_ois_ctrl_t *o_ctrl);
void cam_set_ois_disable(struct cam_ois_ctrl_t *o_ctrl);

#endif
/* _DOWNLOAD_OIS_FW_H_ */


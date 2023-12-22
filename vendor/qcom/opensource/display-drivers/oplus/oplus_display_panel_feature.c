/***************************************************************
** Copyright (C), 2022, OPLUS Mobile Comm Corp., Ltd
**
** File : oplus_display_panel_feature.c
** Description : oplus display panel char dev  /dev/oplus_panel
** Version : 1.0
** Date : 2022/08/01
** Author : Display
******************************************************************/
#include <linux/ktime.h>
#include <drm/drm_mipi_dsi.h>
#include "dsi_parser.h"
#include "dsi_display.h"
#include "dsi_panel.h"
#include "dsi_clk.h"
#include "oplus_bl.h"
#include "oplus_adfr.h"
#include "oplus_display_panel_feature.h"
#include "oplus_display_panel_common.h"
#include "sde_trace.h"

#ifdef OPLUS_FEATURE_DISPLAY_TEMP_COMPENSATION
#include "oplus_display_temp_compensation.h"
#endif /* OPLUS_FEATURE_DISPLAY_TEMP_COMPENSATION */

#ifdef OPLUS_FEATURE_DISPLAY_ONSCREENFINGERPRINT
#include "oplus_onscreenfingerprint.h"
#endif /* OPLUS_FEATURE_DISPLAY_ONSCREENFINGERPRINT */

#if defined(CONFIG_PXLW_IRIS)
#include "../../msm/iris/dsi_iris_api.h"
#endif

extern int lcd_closebl_flag;
extern u32 oplus_last_backlight;
static int oplus_display_update_dbv_evt(struct dsi_panel *panel, u32 level);

static struct backlight_log oplus_bl_log[DISPLAY_MAX];

int oplus_panel_get_serial_number_info(struct dsi_panel *panel)
{
	struct dsi_parser_utils *utils = NULL;
	int ret = 0;
	const char *regs = NULL;
	u32 len, i;

	if (!panel) {
		DSI_ERR("Oplus Features config No panel device\n");
		return -ENODEV;
	}
	utils = &panel->utils;

	panel->oplus_ser.serial_number_support = utils->read_bool(utils->data,
			"oplus,dsi-serial-number-enabled");
	DSI_INFO("oplus,dsi-serial-number-enabled: %s", panel->oplus_ser.serial_number_support ? "true" : "false");

	if (panel->oplus_ser.serial_number_support) {
		panel->oplus_ser.is_reg_lock = utils->read_bool(utils->data, "oplus,dsi-serial-number-lock");
		DSI_INFO("oplus,dsi-serial-number-lock: %s", panel->oplus_ser.is_reg_lock ? "true" : "false");

		ret = utils->read_u32(utils->data, "oplus,dsi-serial-number-reg",
				&panel->oplus_ser.serial_number_reg);
		if (ret) {
			pr_info("[%s] failed to get oplus,dsi-serial-number-reg\n", __func__);
			panel->oplus_ser.serial_number_reg = 0xA1;
		}

		ret = utils->read_u32(utils->data, "oplus,dsi-serial-number-index",
				&panel->oplus_ser.serial_number_index);
		if (ret) {
			pr_info("[%s] failed to get oplus,dsi-serial-number-index\n", __func__);
			/* Default sync start index is set 5 */
			panel->oplus_ser.serial_number_index = 7;
		}

		ret = utils->read_u32(utils->data, "oplus,dsi-serial-number-read-count",
				&panel->oplus_ser.serial_number_conut);
		if (ret) {
			pr_info("[%s] failed to get oplus,dsi-serial-number-read-count\n", __func__);
			/* Default  read conut 5 */
			panel->oplus_ser.serial_number_conut = 5;
		}

		regs = utils->get_property(utils->data, "oplus,dsi-serial-number-multi-regs",
				&len);
		if (!regs) {
			pr_err("[%s] failed to get oplus,dsi-serial-number-multi-regs\n", __func__);
		} else {
			panel->oplus_ser.serial_number_multi_regs =
				kzalloc((sizeof(u32) * len), GFP_KERNEL);
			if (!panel->oplus_ser.serial_number_multi_regs)
				return -EINVAL;
			for (i = 0; i < len; i++) {
				panel->oplus_ser.serial_number_multi_regs[i] = regs[i];
			}
		}

		panel->oplus_ser.is_switch_page = utils->read_bool(utils->data,
			"oplus,dsi-serial-number-switch-page");
		DSI_INFO("oplus,dsi-serial-number-switch-page: %s", panel->oplus_ser.is_switch_page ? "true" : "false");

		panel->oplus_ser.is_multi_reg = utils->read_bool(utils->data,
			"oplus,dsi-serial-number-multi-reg");
		DSI_INFO("oplus,dsi-serial-number-multi-reg: %s", panel->oplus_ser.is_multi_reg ? "true" : "false");
	}
	return 0;
}

int oplus_panel_features_config(struct dsi_panel *panel)
{
	int rc = 0;
	u32 val = 0;
	struct dsi_parser_utils *utils = NULL;

	if (!panel) {
		DSI_ERR("Oplus Features config No panel device\n");
		return -ENODEV;
	}
	utils = &panel->utils;
	panel->oplus_priv.dp_support = utils->get_property(utils->data,
			"oplus,dp-enabled", NULL);

	if (!panel->oplus_priv.dp_support) {
		pr_info("Failed to found panel dp support, using null dp config\n");
		panel->oplus_priv.dp_support = false;
	}

	panel->oplus_priv.cabc_enabled = utils->read_bool(utils->data,
			"oplus,dsi-cabc-enabled");
	DSI_INFO("oplus,dsi-cabc-enabled: %s", panel->oplus_priv.cabc_enabled ? "true" : "false");

	panel->oplus_priv.dre_enabled = utils->read_bool(utils->data,
			"oplus,dsi-dre-enabled");
	DSI_INFO("oplus,dsi-dre-enabled: %s", panel->oplus_priv.dre_enabled ? "true" : "false");

	panel->oplus_priv.crc_check_enabled = utils->read_bool(utils->data,
			"oplus,crc-check-enabled");
	DSI_INFO("oplus,crc-check-enabled: %s", panel->oplus_priv.crc_check_enabled ? "true" : "false");

	panel->oplus_priv.pwm_turbo_support = utils->read_bool(utils->data,
			"oplus,pwm-turbo-support");
	DSI_INFO("oplus,pwm-turbo-support: %s",
			panel->oplus_priv.pwm_turbo_support ? "true" : "false");

	panel->oplus_priv.pwm_turbo_ignore_set_dbv_frame = utils->read_bool(utils->data,
			"oplus,pwm-turbo-ignore-set-dbv-frame");
	if (!panel->oplus_priv.pwm_turbo_support) {
			panel->oplus_priv.pwm_turbo_ignore_set_dbv_frame = false;
	}
	DSI_INFO("oplus,pwm-turbo-ignore-set-dbv-frame: %s",
			panel->oplus_priv.pwm_turbo_ignore_set_dbv_frame ? "true" : "false");

	rc = utils->read_u32(utils->data, "oplus,pwm-turbo-gamma-bl-threshold", &val);
	if (rc) {
		panel->bl_config.pwm_turbo_gamma_bl_threshold = 1603;
		DSI_INFO("oplus,pwm-turbo-gamma-bl-threshold undefined, default to %d\n", panel->bl_config.pwm_turbo_gamma_bl_threshold);
	} else {
		panel->bl_config.pwm_turbo_gamma_bl_threshold = val;
		DSI_INFO("oplus,pwm-turbo-gamma-bl-threshold=%d\n", panel->bl_config.pwm_turbo_gamma_bl_threshold);
	}

	panel->oplus_priv.pwm_switch_support = utils->read_bool(utils->data,
			"oplus,pwm-switch-support");
	LCD_INFO("oplus,pwm-switch-support: %s\n",
			panel->oplus_priv.pwm_switch_support ? "true" : "false");

	panel->oplus_priv.power_seq_adj = utils->read_bool(utils->data,
			"oplus,power-seq-adj");
	LCD_INFO("oplus,power-seq-adj: %s\n",
			panel->oplus_priv.power_seq_adj ? "true" : "false");


	oplus_panel_get_serial_number_info(panel);

	return 0;
}

int oplus_panel_post_on_backlight(void *display, struct dsi_panel *panel, u32 bl_lvl)
{
	struct dsi_display *dsi_display = display;
	int rc = 0;
	if (!panel || !dsi_display) {
		DSI_ERR("oplus post backlight No panel device\n");
		return -ENODEV;
	}

	if ((bl_lvl == 0 && panel->bl_config.bl_level != 0) ||
		(bl_lvl != 0 && panel->bl_config.bl_level == 0)) {
		pr_info("<%s> backlight level changed %d -> %d\n",
				panel->oplus_priv.vendor_name,
				panel->bl_config.bl_level, bl_lvl);
	} else if (panel->bl_config.bl_level == 1) {
		pr_info("<%s> aod backlight level changed %d -> %d\n",
				panel->oplus_priv.vendor_name,
				panel->bl_config.bl_level, bl_lvl);
	}

	/* Add some delay to avoid screen flash */
	if (panel->need_power_on_backlight && bl_lvl) {
		panel->need_power_on_backlight = false;
		rc = dsi_display_clk_ctrl(dsi_display->dsi_clk_handle,
			DSI_CORE_CLK, DSI_CLK_ON);
		if (rc) {
			pr_err("[%s] failed to send DSI_CMD_POST_ON_BACKLIGHT cmds, rc=%d\n",
				panel->name, rc);
		}
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_POST_ON_BACKLIGHT);

		rc = dsi_display_clk_ctrl(dsi_display->dsi_clk_handle,
			DSI_CORE_CLK, DSI_CLK_OFF);

		atomic_set(&panel->esd_pending, 0);

		if (rc) {
			pr_err("[%s] failed to send DSI_CMD_POST_ON_BACKLIGHT cmds, rc=%d\n",
				panel->name, rc);
		}
		if ((!strcmp(panel->name, "BOE AB319 NT37701B UDC") || !strcmp(panel->name, "BOE AB241 NT37701A"))
			&& get_oplus_display_power_status() == OPLUS_DISPLAY_POWER_ON)
			oplus_panel_event_data_notifier_trigger(panel, DRM_PANEL_EVENT_UNBLANK, 0, true);
	}
	return 0;
}

u32 oplus_panel_silence_backlight(struct dsi_panel *panel, u32 bl_lvl)
{
	u32 bl_temp = 0;
	if (!panel) {
		DSI_ERR("Oplus Features config No panel device\n");
		return -ENODEV;
	}

	bl_temp = bl_lvl;

	if (lcd_closebl_flag) {
		pr_err("silence reboot we should set backlight to zero\n");
		bl_temp = 0;
	}

	return bl_temp;
}

void oplus_panel_update_backlight(struct dsi_panel *panel,
		struct mipi_dsi_device *dsi, u32 bl_lvl)
{
	int rc = 0;
	struct dsi_cmd_desc *cmds;
	struct mipi_dsi_msg msg;
	char *tx_buf = NULL;
	struct dsi_panel_cmd_set *cmd_sets;
	u64 inverted_dbv_bl_lvl = 0;
#ifdef OPLUS_FEATURE_DISPLAY
	struct dsi_mode_info timing;
	unsigned int refresh_rate;
#endif

	if (!panel->cur_mode || !panel->cur_mode->priv_info) {
		DSI_ERR("Oplus Features config No panel device\n");
		return;
	}

	timing = panel->cur_mode->timing;
	refresh_rate = timing.refresh_rate;

	if ((!strcmp(panel->oplus_priv.vendor_name, "TM_NT37705")) || (!strcmp(panel->oplus_priv.vendor_name, "TM_NT37705_DVT"))) {
		if (bl_lvl > 0 && bl_lvl < 8)
			bl_lvl = 8;
	}

#ifdef OPLUS_FEATURE_DISPLAY_ONSCREENFINGERPRINT
	if (oplus_ofp_is_supported()) {
		if (oplus_ofp_backlight_filter(panel, bl_lvl)) {
			return;
		}
	}
#endif /* OPLUS_FEATURE_DISPLAY_ONSCREENFINGERPRINT */
	SDE_ATRACE_BEGIN("oplus_panel_update_backlight");
	oplus_display_pwm_pulse_switch(panel, bl_lvl);

#ifdef OPLUS_FEATURE_DISPLAY_TEMP_COMPENSATION
	if (oplus_temp_compensation_is_supported()) {
		oplus_temp_compensation_cmd_set(panel, OPLUS_TEMP_COMPENSATION_BACKLIGHT_SETTING);
	}
#endif /* OPLUS_FEATURE_DISPLAY_TEMP_COMPENSATION */

	oplus_temp_compensation_wait_for_vsync_set = false;

	/*backlight value mapping */
	oplus_panel_backlight_level_mapping(panel, &bl_lvl);
	/*backlight value mapping */
	oplus_panel_global_hbm_mapping(panel, &bl_lvl);

	if (!panel->oplus_priv.need_sync && panel->cur_mode->priv_info->async_bl_delay) {
		oplus_apollo_async_bl_delay(panel);
		panel->oplus_priv.need_sync = false;
	}

	/*will inverted display brightness value */
	if (panel->bl_config.bl_inverted_dbv)
		inverted_dbv_bl_lvl = (((bl_lvl & 0xff) << 8) | (bl_lvl >> 8));
	else
		inverted_dbv_bl_lvl = bl_lvl;

	/* add switch backlight page */
	if ((!strcmp(panel->oplus_priv.vendor_name, "RM692E5")) && (bl_lvl > 1)) {
		cmd_sets = &(panel->cur_mode->priv_info->cmd_sets[DSI_CMD_SET_BACKLIGHT]);
		cmds = &(cmd_sets->cmds[cmd_sets->count - 1]);
		msg = cmds->msg;
		tx_buf = (char*)msg.tx_buf;

		tx_buf[msg.tx_len-1] = (bl_lvl & 0xff);
		tx_buf[msg.tx_len-2] = (bl_lvl >> 8);

		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_BACKLIGHT);
		if (rc < 0)
			DSI_ERR("Failed to set backlight:%d \n", bl_lvl);
		else
			return;
	}

	if (!strcmp(panel->oplus_priv.vendor_name, "TM_NT37705") && (bl_lvl > 1)) {
		oplus_display_update_dbv_evt(panel, bl_lvl);
	} else if (!strcmp(panel->oplus_priv.vendor_name, "TM_NT37705_DVT") && (bl_lvl > 1)) {
		SDE_ATRACE_BEGIN("oplus_display_update_dbv");
		oplus_display_update_dbv(panel);
		SDE_ATRACE_END("oplus_display_update_dbv");
	} else {
	if (oplus_adfr_is_support()) {
		/* if backlight cmd is set after qsync window setting and qsync is enable, filter it
			otherwise tearing issue happen */
		if ((oplus_adfr_backlight_cmd_filter_get() == true) && (inverted_dbv_bl_lvl != 0)) {
			DSI_INFO("kVRR filter backlight cmd\n");
		} else {
			mutex_lock(&panel->panel_tx_lock);
#if defined(CONFIG_PXLW_IRIS)
			if (iris_is_chip_supported() && iris_is_pt_mode(panel))
				rc = iris_update_backlight(inverted_dbv_bl_lvl);
			else
#endif
				rc = mipi_dsi_dcs_set_display_brightness(dsi, inverted_dbv_bl_lvl);
			if (rc < 0)
				DSI_ERR("failed to update dcs backlight:%d\n", bl_lvl);
			mutex_unlock(&panel->panel_tx_lock);
		}
	} else {
#if defined(CONFIG_PXLW_IRIS)
		if (iris_is_chip_supported() && iris_is_pt_mode(panel))
			rc = iris_update_backlight(inverted_dbv_bl_lvl);
		else
#endif
			rc = mipi_dsi_dcs_set_display_brightness(dsi, inverted_dbv_bl_lvl);
		if (rc < 0)
			DSI_ERR("failed to update dcs backlight:%d\n", bl_lvl);
	}
	}

#ifdef OPLUS_FEATURE_DISPLAY_TEMP_COMPENSATION
	if (oplus_temp_compensation_is_supported()) {
		oplus_temp_compensation_first_half_frame_cmd_set(panel);
	}
#endif /* OPLUS_FEATURE_DISPLAY_TEMP_COMPENSATION */

	SDE_ATRACE_INT("current_bl_lvl", bl_lvl);
	LCD_DEBUG_BACKLIGHT("<%s> Final backlight lvl:%d\n", panel->oplus_priv.vendor_name, bl_lvl);
	oplus_last_backlight = bl_lvl;
	SDE_ATRACE_END("oplus_panel_update_backlight");
}

static int oplus_display_update_dbv_evt(struct dsi_panel *panel, u32 level)
{
	int i = 0, rc = 0;
	char *tx_buf = NULL;
	unsigned char ED_120[3] = {0x24, 0x2C, 0x34};/*{36,44,52};*/
	unsigned char ED_90[3] = {0x24, 0x24};/*{36,36};*/
	unsigned char ED_144[3] = {0x24, 0x2C};/*{36,44};*/
	unsigned char Frameskip_Vset_120[3] = {0x00};
	unsigned char Frameskip_Vset_90[3] = {0x00};
	unsigned char Frameskip_Vset_144[3] = {0x00};
	uint8_t Voltage1, Voltage2, Voltage3, Voltage4;
	unsigned short vpark = 0;
	unsigned char voltage = 0;
	struct dsi_cmd_desc *cmds;
	cmds = panel->cur_mode->priv_info->cmd_sets[DSI_CMD_SKIPFRAME_DBV].cmds;

	if (level > 3515) {
		ED_120[0] = 0x24;/*36;*/
		ED_120[1] = 0x2C;/*44;*/
		ED_120[2] = 0x34;/*52;*/
		ED_90[0] = 0x24;/*36;*/
		ED_90[1] = 0x24;/*36;*/
		ED_144[0] = 0x24;/*36;*/
		ED_144[1] = 0x2C;/*44;*/
	} else if (level >= 1604) {
		ED_120[0] = 0x24;/*36;*/
		ED_120[1] = 0x2C;/*44;*/
		ED_120[2] = 0x34;/*52;*/
		ED_90[0] = 0x24;/*36;*/
		ED_90[1] = 0x24;/*36;*/
		ED_144[0] = 0x24;/*36;*/
		ED_144[1] = 0x2C;/*44;*/
	} else if (level >= 1419) {
		ED_120[0] = 8;
		ED_120[1] = 8;
		ED_120[2] = 0xC;/*12;*/
		ED_90[0] = 4;
		ED_90[1] = 4;
		ED_144[0] = 8;
		ED_144[1] = 8;
	} else if (level >= 1328) {
		ED_120[0] = 4;
		ED_120[1] = 4;
		ED_120[2] = 8;
		ED_90[0] = 4;
		ED_90[1] = 4;
		ED_144[0] = 8;
		ED_144[1] = 8;
	} else if (level >= 950) {
		ED_120[0] = 4;
		ED_120[1] = 4;
		ED_120[2] = 8;
		ED_90[0] = 4;
		ED_90[1] = 4;
		ED_144[0] = 4;
		ED_144[1] = 4;
	} else {
		ED_120[0] = 4;
		ED_120[1] = 4;
		ED_120[2] = 4;
		ED_90[0] = 4;
		ED_90[1] = 4;
		ED_144[0] = 4;
		ED_144[1] = 4;
	}

	for (i = 0; i < 3; i++) {
		tx_buf = (char*)cmds[2].msg.tx_buf;
		tx_buf[i+1+4] = ED_120[0];
		tx_buf[i+1+8] = ED_120[1];
		tx_buf[i+1+12] = ED_120[2];
		tx_buf = (char*)cmds[4].msg.tx_buf;
		tx_buf[i+1+4] = ED_90[0];
		tx_buf[i+1+8] = ED_90[1];
		tx_buf = (char*)cmds[6].msg.tx_buf;
		tx_buf[i+1+4] = ED_144[0];
		tx_buf[i+1+8] = ED_144[1];
	}

	if (level > 3515) {
		Frameskip_Vset_120[0] = 0x31;/*49;*/
		Frameskip_Vset_120[1] = 0x31;/*49;*/
		Frameskip_Vset_120[2] = 0x31;/*49;*/
		Frameskip_Vset_90[0] = 0x33;/*51;*/
		Frameskip_Vset_90[1] = 0x33;/*51;*/
		Frameskip_Vset_90[2] = 0x33;/*51;*/
		Frameskip_Vset_144[0] = 0x31;/*49;*/
		Frameskip_Vset_144[1] = 0x31;/*49;*/
		Frameskip_Vset_144[2] = 0x31;/*49;*/
	} else if (level >= 1604) {
		Frameskip_Vset_120[0] = 0x1A;/*26;*/
		Frameskip_Vset_120[1] = 0x1A;/*26;*/
		Frameskip_Vset_120[2] = 0x23;/*35;*/
		Frameskip_Vset_90[0] = 0x1C;/*28;*/
		Frameskip_Vset_90[1] = 0x1C;/*28;*/
		Frameskip_Vset_90[2] = 0x25;/*37;*/
		Frameskip_Vset_144[0] = 0x1A;/*26;*/
		Frameskip_Vset_144[1] = 0x1A;/*26;*/
		Frameskip_Vset_144[2] = 0x23;/*35;*/
	} else if (level >= 1511) {
		Frameskip_Vset_120[0] = 0x2E;/*46;*/
		Frameskip_Vset_120[1] = 0x2E;/*46;*/
		Frameskip_Vset_120[2] = 0x23;/*35;*/
		Frameskip_Vset_90[0] = 0x2F;/*47;*/
		Frameskip_Vset_90[1] = 0x2F;/*47;*/
		Frameskip_Vset_90[2] = 0x25;/*37;*/
		Frameskip_Vset_144[0] = 0x2E;/*46;*/
		Frameskip_Vset_144[1] = 0x2E;/*46;*/
		Frameskip_Vset_144[2] = 0x23;/*35;*/
	} else if (level >= 1419) {
		Frameskip_Vset_120[0] = 0x2E;/*46;*/
		Frameskip_Vset_120[1] = 0x2E;/*46;*/
		Frameskip_Vset_120[2] = 0x23;/*35;*/
		Frameskip_Vset_90[0] = 0x30;/*48;*/
		Frameskip_Vset_90[1] = 0x30;/*48;*/
		Frameskip_Vset_90[2] = 0x25;/*37;*/
		Frameskip_Vset_144[0] = 0x2E;/*46;*/
		Frameskip_Vset_144[1] = 0x2E;/*46;*/
		Frameskip_Vset_144[2] = 0x23;/*35;*/
	} else if (level >= 1328) {
		Frameskip_Vset_120[0] = 0x2F;/*47;*/
		Frameskip_Vset_120[1] = 0x2F;/*47;*/
		Frameskip_Vset_120[2] = 0x23;/*35;*/
		Frameskip_Vset_90[0] = 0x30;/*48;*/
		Frameskip_Vset_90[1] = 0x30;/*48;*/
		Frameskip_Vset_90[2] = 0x25;/*37;*/
		Frameskip_Vset_144[0] = 0x2E;/*46;*/
		Frameskip_Vset_144[1] = 0x2E;/*46;*/
		Frameskip_Vset_144[2] = 0x23;/*35;*/
	} else if (level >= 950) {
		Frameskip_Vset_120[0] = 0x2F;/*47;*/
		Frameskip_Vset_120[1] = 0x2F;/*47;*/
		Frameskip_Vset_120[2] = 0x23;/*35;*/
		Frameskip_Vset_90[0] = 0x30;/*48;*/
		Frameskip_Vset_90[1] = 0x30;/*48;*/
		Frameskip_Vset_90[2] = 0x25;/*37;*/
		Frameskip_Vset_144[0] = 0x2F;/*47;*/
		Frameskip_Vset_144[1] = 0x2F;/*47;*/
		Frameskip_Vset_144[2] = 0x23;/*35;*/
	} else if (level > 544) {
		Frameskip_Vset_120[0] = 0x2F;/*47;*/
		Frameskip_Vset_120[1] = 0x2F;/*47;*/
		Frameskip_Vset_120[2] = 0x23;/*35;*/
		Frameskip_Vset_90[0] = 0x2F;/*47;*/
		Frameskip_Vset_90[1] = 0x2F;/*47;*/
		Frameskip_Vset_90[2] = 0x25;/*37;*/
		Frameskip_Vset_144[0] = 0x2E;/*46;*/
		Frameskip_Vset_144[1] = 0x2E;/*46;*/
		Frameskip_Vset_144[2] = 0x23;/*35;*/
	} else if (level > 7) {
		Frameskip_Vset_120[0] = 0x2F;/*47;*/
		Frameskip_Vset_120[1] = 0x2F;/*47;*/
		Frameskip_Vset_120[2] = 0x23;/*35;*/
		Frameskip_Vset_90[0] = 0x2F;/*47;*/
		Frameskip_Vset_90[1] = 0x2F;/*47;*/
		Frameskip_Vset_90[2] = 0x25;/*37;*/
		Frameskip_Vset_144[0] = 0x2E;/*46;*/
		Frameskip_Vset_144[1] = 0x2E;/*46;*/
		Frameskip_Vset_144[2] = 0x23;/*35;*/
	}

	for (i = 0; i < 3; i++) {
		tx_buf = (char*)cmds[8].msg.tx_buf;
		tx_buf[i+1] = Frameskip_Vset_120[i];
		tx_buf[i+1+9] = Frameskip_Vset_90[i];
		tx_buf[i+1+18] = Frameskip_Vset_144[i];
	}

	if(level > 0x643) {
		voltage = 68;
	} else {
		voltage = 69;
	}

	vpark = (69 - voltage) * 1024 / (69 - 10);
	Voltage1 = ((vpark & 0xFF00) >> 8) + ((vpark & 0xFF00) >> 6) + ((vpark & 0xFF00) >> 4);
	Voltage2 = vpark & 0xFF;
	Voltage3 = vpark & 0xFF;
	Voltage4 = vpark & 0xFF;

	tx_buf = (char*)cmds[11].msg.tx_buf;
	tx_buf[0+1] = Voltage1;
	tx_buf = (char*)cmds[11].msg.tx_buf;
	tx_buf[1+1] = Voltage2;
	tx_buf = (char*)cmds[11].msg.tx_buf;
	tx_buf[2+1] = Voltage3;
	tx_buf = (char*)cmds[11].msg.tx_buf;
	tx_buf[3+1] = Voltage4;

	if(level > 0x643) {
		tx_buf = (char*)cmds[13].msg.tx_buf;
		tx_buf[0+1] = 0xB2;
		tx_buf = (char*)cmds[15].msg.tx_buf;
		tx_buf[0+1] = 0xB2;
		tx_buf = (char*)cmds[17].msg.tx_buf;
		tx_buf[0+1] = 0xB2;
		tx_buf = (char*)cmds[20].msg.tx_buf;
		tx_buf[0+1] = 0x02;
		tx_buf = (char*)cmds[20].msg.tx_buf;
		tx_buf[1+1] = 0x03;
		tx_buf = (char*)cmds[20].msg.tx_buf;
		tx_buf[2+1] = 0x42;
	} else {
		tx_buf = (char*)cmds[13].msg.tx_buf;
		tx_buf[0+1] = 0xD2;
		tx_buf = (char*)cmds[15].msg.tx_buf;
		tx_buf[0+1] = 0xE2;
		tx_buf = (char*)cmds[17].msg.tx_buf;
		tx_buf[0+1] = 0xD2;
		tx_buf = (char*)cmds[20].msg.tx_buf;
		tx_buf[0+1] = 0x0F;
		tx_buf = (char*)cmds[20].msg.tx_buf;
		tx_buf[1+1] = 0x17;
		tx_buf = (char*)cmds[20].msg.tx_buf;
		tx_buf[2+1] = 0x4E;
	}

	tx_buf = (char*)cmds[21].msg.tx_buf;
	tx_buf[1+1] = (level & 0xff);
	tx_buf[0+1] = (level >> 8);

	rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SKIPFRAME_DBV);
	if (rc < 0)
		DSI_ERR("Failed to set DSI_CMD_SKIPFRAME_DBV \n");

	return 0;
}

void oplus_printf_backlight_log(struct dsi_display *display, u32 bl_lvl) {
	struct timespec64 now;
	struct tm broken_time;
	static time64_t time_last = 0;
	struct backlight_log *bl_log;
	int i = 0;
	int len = 0;
	char backlight_log_buf[1024];

	ktime_get_real_ts64(&now);
	time64_to_tm(now.tv_sec, 0, &broken_time);
	if (now.tv_sec - time_last >= 60) {
		pr_info("<%s> dsi_display_set_backlight time:%02d:%02d:%02d.%03ld,bl_lvl:%d\n",
			display->panel->oplus_priv.vendor_name, broken_time.tm_hour, broken_time.tm_min,
			broken_time.tm_sec, now.tv_nsec / 1000000, bl_lvl);
		time_last = now.tv_sec;
	}

	if (!strcmp(display->display_type, "secondary")) {
		bl_log = &oplus_bl_log[DISPLAY_SECONDARY];
	} else {
		bl_log = &oplus_bl_log[DISPLAY_PRIMARY];
	}

	bl_log->backlight[bl_log->bl_count] = bl_lvl;
	bl_log->past_times[bl_log->bl_count] = now;
	bl_log->bl_count++;
	if (bl_log->bl_count >= BACKLIGHT_CACHE_MAX) {
		bl_log->bl_count = 0;
		memset(backlight_log_buf, 0, sizeof(backlight_log_buf));
		for (i = 0; i < BACKLIGHT_CACHE_MAX; i++) {
			time64_to_tm(bl_log->past_times[i].tv_sec, 0, &broken_time);
			len += snprintf(backlight_log_buf + len, sizeof(backlight_log_buf) - len,
				"%02d:%02d:%02d.%03ld:%d,", broken_time.tm_hour, broken_time.tm_min,
				broken_time.tm_sec, bl_log->past_times[i].tv_nsec / 1000000, bl_log->backlight[i]);
		}
		pr_info("<%s> len:%d dsi_display_set_backlight %s\n", display->panel->oplus_priv.vendor_name, len, backlight_log_buf);
	}
}

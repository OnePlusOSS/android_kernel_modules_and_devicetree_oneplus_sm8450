/***************************************************************
** Copyright (C),  2021,  OPLUS Mobile Comm Corp.,  Ltd
**
** File : oplus_bl.c
** Description : oplus display backlight
** Version : 1.0
** Date : 2021/02/22
**
** ------------------------------- Revision History: -----------
**  <author>        <data>        <version >        <desc>
**  kevin.liuwq    2020/02/22        1.0           Build this moudle
******************************************************************/

#include "oplus_bl.h"

char oplus_global_hbm_flags = 0x0;
static int enable_hbm_enter_dly_on_flags = 0;
static int enable_hbm_exit_dly_on_flags = 0;

int oplus_panel_parse_bl_config(struct dsi_panel *panel)
{
	int rc = 0;
	u32 val = 0;
	struct dsi_parser_utils *utils = &panel->utils;

	rc = utils->read_u32(utils->data, "qcom,mdss-dsi-bl-normal-max-level", &val);
	if (rc) {
		DSI_DEBUG("[%s] bl-max-level unspecified, defaulting to max level\n",
			 panel->name);
		panel->bl_config.bl_normal_max_level = panel->bl_config.bl_max_level;
	} else {
		panel->bl_config.bl_normal_max_level = val;
	}

	rc = utils->read_u32(utils->data, "qcom,mdss-brightness-normal-max-level",
		&val);
	if (rc) {
		DSI_DEBUG("[%s] brigheness-max-level unspecified, defaulting to 255\n",
			 panel->name);
		panel->bl_config.brightness_normal_max_level = panel->bl_config.brightness_max_level;
	} else {
		panel->bl_config.brightness_normal_max_level = val;
	}

	rc = utils->read_u32(utils->data, "qcom,mdss-brightness-default-level", &val);
	if (rc) {
		DSI_DEBUG("[%s] brightness-default-level unspecified, defaulting normal max\n",
			 panel->name);
		panel->bl_config.brightness_default_level = panel->bl_config.brightness_max_level;
	} else {
		panel->bl_config.brightness_default_level = val;
	}

	rc = utils->read_u32(utils->data, "oplus,dsi-dc-backlight-threshold", &val);
	if (rc) {
		DSI_INFO("[%s] oplus,dsi-dc-backlight-threshold undefined, default to 260\n",
				panel->name);
		panel->bl_config.dc_backlight_threshold = 260;
		panel->bl_config.oplus_dc_mode = false;
	} else {
		panel->bl_config.dc_backlight_threshold = val;
		panel->bl_config.oplus_dc_mode = true;
	}
	DSI_INFO("[%s] dc_backlight_threshold=%d, oplus_dc_mode=%d\n",
			panel->name, panel->bl_config.dc_backlight_threshold,
			panel->bl_config.oplus_dc_mode);

	return 0;
}

static int oplus_display_panel_dly(struct dsi_panel *panel, char hbm_switch)
{
	if (hbm_switch) {
		if (enable_hbm_enter_dly_on_flags)
			enable_hbm_enter_dly_on_flags++;
		if (0 == oplus_global_hbm_flags) {
			if (dsi_panel_tx_cmd_set(panel, DSI_CMD_DLY_ON)){
				DSI_ERR("Failed to send DSI_CMD_DLY_ON commands\n");
				return 0;
			}
			enable_hbm_enter_dly_on_flags = 1;
		} else if (4 == enable_hbm_enter_dly_on_flags) {
			if (dsi_panel_tx_cmd_set(panel, DSI_CMD_DLY_OFF)){
				DSI_ERR("Failed to send DSI_CMD_DLY_OFF commands\n");
				return 0;
			}
			enable_hbm_enter_dly_on_flags = 0;
		}
	} else {
		if (oplus_global_hbm_flags == 1) {
			if (dsi_panel_tx_cmd_set(panel, DSI_CMD_DLY_ON)){
				DSI_ERR("Failed to send DSI_CMD_DLY_ON commands\n");
				return 0;
			}
			enable_hbm_exit_dly_on_flags = 1;
		} else {
			if (enable_hbm_exit_dly_on_flags)
				enable_hbm_exit_dly_on_flags++;
			if (3 == enable_hbm_exit_dly_on_flags) {
				enable_hbm_exit_dly_on_flags = 0;
				if (dsi_panel_tx_cmd_set(panel, DSI_CMD_DLY_OFF)){
					DSI_ERR("Failed to send DSI_CMD_DLY_OFF commands\n");
					return 0;
				}
			}
		}
	}
	return 0;
}

int oplus_display_panel_backlight_mapping(struct dsi_panel *panel, u32 *backlight_level)
{
	u32 bl_lvl = *backlight_level;

	if (!strcmp(panel->oplus_priv.vendor_name, "S6E3HC3")) {
		if (bl_lvl <= PANEL_MAX_NOMAL_BRIGHTNESS) {
			bl_lvl = backlight_buf[bl_lvl];
			if (oplus_global_hbm_flags == 1) {
				if (dsi_panel_tx_cmd_set(panel, DSI_CMD_HBM_EXIT_SWITCH)) {
					DSI_ERR("Failed to send DSI_CMD_HBM_EXIT_SWITCH commands\n");
					return 0;
				}
				oplus_global_hbm_flags = 0;
			}
		} else if (bl_lvl > HBM_BASE_600NIT) {
			oplus_display_panel_dly(panel, 1);
			if (oplus_global_hbm_flags == 0) {
				if (dsi_panel_tx_cmd_set(panel, DSI_CMD_HBM_ENTER_SWITCH)) {
					DSI_ERR("Failed to send DSI_CMD_HBM_ENTER_SWITCH commands\n");
					return 0;
				}
				oplus_global_hbm_flags = 1;
			}
			bl_lvl = backlight_600_800nit_buf[bl_lvl - HBM_BASE_600NIT];
		} else if (bl_lvl > PANEL_MAX_NOMAL_BRIGHTNESS) {
			if (oplus_global_hbm_flags == 1) {
				if (dsi_panel_tx_cmd_set(panel, DSI_CMD_HBM_EXIT_SWITCH)) {
					DSI_ERR("Failed to send DSI_CMD_HBM_EXIT_SWITCH commands\n");
					return 0;
				}
				oplus_global_hbm_flags = 0;
			}
			bl_lvl = backlight_500_600nit_buf[bl_lvl - PANEL_MAX_NOMAL_BRIGHTNESS];
		}
	}

	*backlight_level = bl_lvl;
	return 0;
}

int oplus_display_panel_get_global_hbm_status(void)
{
	return oplus_global_hbm_flags;
}

void oplus_display_panel_set_global_hbm_status(int global_hbm_status)
{
	oplus_global_hbm_flags = global_hbm_status;
	DSI_INFO("set oplus_global_hbm_flags = %d\n", global_hbm_status);
}
/***************************************************************
** Copyright (C),  2022,  oplus Mobile Comm Corp.,  Ltd
**
** File : oplus_display_panel_feature.c
** Description : oplus display panel char dev  /dev/oplus_panel
** Version : 1.0
** Date : 2021/11/17
**
** ------------------------------- Revision History: -----------
**  <author>        <data>        <version >        <desc>
**  Li.Ping       2021/11/22        1.0           Build this moudle for panel feature
******************************************************************/
#include <drm/drm_mipi_dsi.h>
#include "dsi_parser.h"
#include "dsi_display.h"
#include "dsi_panel.h"
#include "dsi_clk.h"
#include "oplus_bl.h"
#include "oplus_adfr.h"
#include "oplus_display_panel_feature.h"
#include "oplus_onscreenfingerprint.h"
#include "oplus_display_panel_hbm.h"

extern int lcd_closebl_flag;
extern u32 oplus_last_backlight;

int oplus_panel_get_serial_number_info(struct dsi_panel *panel)
{
	struct dsi_parser_utils *utils = NULL;
	int ret = 0;
	if (!panel){
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
	}
	return 0;
}

int oplus_panel_features_config(struct dsi_panel *panel)
{
	struct dsi_parser_utils *utils = NULL;
	if (!panel){
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

	oplus_panel_get_serial_number_info(panel);

	return 0;
}

int oplus_panel_post_on_backlight(void *display, struct dsi_panel *panel, u32 bl_lvl)
{
	struct dsi_display *dsi_display = display;
	int rc = 0;
	if (!panel || !dsi_display){
		DSI_ERR("oplus post backlight No panel device\n");
		return -ENODEV;
	}

	if ((bl_lvl == 0 && panel->bl_config.bl_level != 0) ||
		(bl_lvl != 0 && panel->bl_config.bl_level == 0)) {
		pr_info("backlight level changed %d -> %d\n",
				panel->bl_config.bl_level, bl_lvl);
	} else if (panel->bl_config.bl_level == 1) {
		pr_info("aod backlight level changed %d -> %d\n",
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
	}
	return 0;
}

u32 oplus_panel_silence_backlight(struct dsi_panel *panel, u32 bl_lvl)
{
	u32 bl_temp = 0;
	if (!panel){
		DSI_ERR("Oplus Features config No panel device\n");
		return -ENODEV;
	}

	bl_temp = bl_lvl;

	if (lcd_closebl_flag) {
		pr_err("silence reboot we should set backlight to zero\n");
		bl_temp = 0;
	} else if (bl_lvl) {
		if (oplus_get_ofp_switch_mode() == OFP_SWITCH_OFF) {
			oplus_set_ofp_switch_mode(OFP_SWITCH_ON);
			DSI_DEBUG("oplus ofp switch on\n");
		}
	}
	return bl_temp;
}

void oplus_panel_update_backlight(struct mipi_dsi_device *dsi, u32 bl_lvl)
{
	int rc = 0;
	u64 inverted_dbv_bl_lvl = 0;
	struct dsi_display *dsi_display = get_main_display();

	if (!dsi_display || !dsi_display->panel){
		DSI_ERR("Oplus Features config No panel device\n");
		return;
	}

	if ((get_oplus_display_scene() == OPLUS_DISPLAY_AOD_SCENE) && ( bl_lvl == 1)) {
		pr_err("dsi_cmd AOD mode return bl_lvl:%d\n",bl_lvl);
		return;
	}

	if (dsi_display->panel->is_hbm_enabled && (bl_lvl != 0))
		return;

	if((bl_lvl == 0) && oplus_display_get_hbm_mode()) {
		pr_info("set backlight 0 and recovery hbm to 0\n");
			__oplus_display_set_hbm(0);
	}

	if (oplus_display_get_hbm_mode()) {
		return;
	}

	oplus_display_panel_backlight_mapping(dsi_display->panel, &bl_lvl);

	if (dsi_display->panel->bl_config.bl_inverted_dbv)
		inverted_dbv_bl_lvl = (((bl_lvl & 0xff) << 8) | (bl_lvl >> 8));
	else
		inverted_dbv_bl_lvl = bl_lvl;

	if (oplus_adfr_is_support()) {
		/* if backlight cmd is set after qsync window setting and qsync is enable, filter it
			otherwise tearing issue happen */
		if ((oplus_adfr_backlight_cmd_filter_get() == true) && (inverted_dbv_bl_lvl != 0)) {
			DSI_INFO("kVRR filter backlight cmd\n");
		} else {
			mutex_lock(&dsi_display->panel->panel_tx_lock);
			rc = mipi_dsi_dcs_set_display_brightness(dsi, inverted_dbv_bl_lvl);
			if (rc < 0)
				DSI_ERR("failed to update dcs backlight:%d\n", bl_lvl);
			mutex_unlock(&dsi_display->panel->panel_tx_lock);
		}
	} else {
		rc = mipi_dsi_dcs_set_display_brightness(dsi, inverted_dbv_bl_lvl);
		if (rc < 0)
			DSI_ERR("failed to update dcs backlight:%d\n", bl_lvl);
	}
	oplus_last_backlight = bl_lvl;

}

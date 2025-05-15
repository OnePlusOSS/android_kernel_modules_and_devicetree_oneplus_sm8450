/***************************************************************
** Copyright (C),  2020,  OPLUS Mobile Comm Corp.,  Ltd
**
** File : oplus_display_panel_seed.c
** Description : oplus display panel seed feature
** Version : 1.0
** Date : 2020/06/13
**
** ------------------------------- Revision History: -----------
**  <author>        <data>        <version >        <desc>
**  Li.Sheng       2020/06/13        1.0           Build this moudle
******************************************************************/
#include "oplus_display_panel_seed.h"
#include "oplus_dsi_support.h"
#include "oplus_display_private_api.h"
#include "oplus_onscreenfingerprint.h"

int seed_mode = 0;
extern int oplus_seed_backlight;
/* outdoor hbm flag*/

#define PANEL_LOADING_EFFECT_FLAG 100
#define PANEL_LOADING_EFFECT_MODE1 101
#define PANEL_LOADING_EFFECT_MODE2 102
#define PANEL_LOADING_EFFECT_OFF 100

DEFINE_MUTEX(oplus_seed_lock);

int oplus_display_get_seed_mode(void)
{
	return seed_mode;
}

int __oplus_display_set_seed(int mode)
{
	mutex_lock(&oplus_seed_lock);

	if (mode != seed_mode) {
		seed_mode = mode;
	}

	mutex_unlock(&oplus_seed_lock);
	return 0;
}

int dsi_panel_seed_mode_unlock(struct dsi_panel *panel, int mode)
{
	int rc = 0;

	if (!dsi_panel_initialized(panel)) {
		return -EINVAL;
	}

	switch (mode) {
	case 0:
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SEED_MODE0);

		if (rc) {
			pr_err("[%s] failed to send DSI_CMD_SEED_MODE0 cmds, rc=%d\n",
					panel->name, rc);
		}

		break;

	case 1:
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SEED_MODE1);

		if (rc) {
			pr_err("[%s] failed to send DSI_CMD_SEED_MODE1 cmds, rc=%d\n",
					panel->name, rc);
		}

		break;

	case 2:
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SEED_MODE2);

		if (rc) {
			pr_err("[%s] failed to send DSI_CMD_SEED_MODE2 cmds, rc=%d\n",
					panel->name, rc);
		}

		break;

	case 3:
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SEED_MODE3);

		if (rc) {
			pr_err("[%s] failed to send DSI_CMD_SEED_MODE3 cmds, rc=%d\n",
					panel->name, rc);
		}

		break;

	case 4:
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SEED_MODE4);

		if (rc) {
			pr_err("[%s] failed to send DSI_CMD_SEED_MODE4 cmds, rc=%d\n",
					panel->name, rc);
		}

		break;

	default:
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SEED_OFF);

		if (rc) {
			pr_err("[%s] failed to send DSI_CMD_SEED_OFF cmds, rc=%d\n",
					panel->name, rc);
		}

		pr_err("[%s] seed mode Invalid %d\n",
				panel->name, mode);
	}

	return rc;
}

int dsi_panel_loading_effect_mode_unlock(struct dsi_panel *panel, int mode)
{
	int rc = 0;

	if (!dsi_panel_initialized(panel)) {
		return -EINVAL;
	}

	switch (mode) {
	case PANEL_LOADING_EFFECT_MODE1:
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_LOADING_EFFECT_MODE1);

		if (rc) {
			pr_err("[%s] failed to send DSI_CMD_SEED_MODE0 cmds, rc=%d\n",
					panel->name, rc);
		}

		break;

	case PANEL_LOADING_EFFECT_MODE2:
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_LOADING_EFFECT_MODE2);

		if (rc) {
			pr_err("[%s] failed to send DSI_CMD_SEED_MODE1 cmds, rc=%d\n",
					panel->name, rc);
		}

		break;

	case PANEL_LOADING_EFFECT_OFF:
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_LOADING_EFFECT_OFF);

		if (rc) {
			pr_err("[%s] failed to send DSI_CMD_SEED_MODE2 cmds, rc=%d\n",
					panel->name, rc);
		}

		break;

	default:
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_LOADING_EFFECT_OFF);

		if (rc) {
			pr_err("[%s] failed to send DSI_CMD_LOADING_EFFECT_OFF cmds, rc=%d\n",
					panel->name, rc);
		}

		pr_err("[%s] loading effect mode Invalid %d\n",
				panel->name, mode);
	}

	return rc;
}

int dsi_panel_seed_mode(struct dsi_panel *panel, int mode)
{
	int rc = 0;

	if (!panel) {
		pr_err("Invalid params\n");
		return -EINVAL;
	}

	if (!strcmp(panel->oplus_priv.vendor_name, "S6E3HC3") && (mode >= PANEL_LOADING_EFFECT_FLAG)) {
		rc = dsi_panel_loading_effect_mode_unlock(panel, mode);
	} else if (!strcmp(panel->oplus_priv.vendor_name, "ANA6706") && (mode >= PANEL_LOADING_EFFECT_FLAG)) {
		mode = mode - PANEL_LOADING_EFFECT_FLAG;
		rc = dsi_panel_seed_mode_unlock(panel, mode);
		__oplus_display_set_seed(mode);
	} else if(!strcmp(panel->oplus_priv.vendor_name, "AMB655X") && (mode >= PANEL_LOADING_EFFECT_FLAG)){
		rc = dsi_panel_loading_effect_mode_unlock(panel, mode);
	} else if(!strcmp(panel->oplus_priv.vendor_name, "AMB670YF01") && (mode >= PANEL_LOADING_EFFECT_FLAG)){
		rc = dsi_panel_loading_effect_mode_unlock(panel, mode);
	}else {
		rc = dsi_panel_seed_mode_unlock(panel, mode);
	}

	return rc;
}

int dsi_display_seed_mode(struct dsi_display *display, int mode)
{
	int rc = 0;

	if (!display || !display->panel) {
		pr_err("Invalid params\n");
		return -EINVAL;
	}

	mutex_lock(&display->display_lock);
	mutex_lock(&display->panel->panel_lock);

	rc = dsi_panel_seed_mode(display->panel, mode);

	if (rc) {
		pr_err("[%s] failed to dsi_panel_seed_or_loading_effect_on, rc=%d\n",
				display->name, rc);
	}

	mutex_unlock(&display->panel->panel_lock);
	mutex_unlock(&display->display_lock);
	return rc;
}

int oplus_dsi_update_seed_mode(void)
{
	struct dsi_display *display = get_main_display();
	int ret = 0;

	if (!display) {
		pr_err("failed for: %s %d\n", __func__, __LINE__);
		return -EINVAL;
	}

	ret = dsi_display_seed_mode(display, seed_mode);

	return ret;
}

int oplus_display_panel_get_seed(void *data)
{
	uint32_t *temp = data;
	printk(KERN_INFO "oplus_display_get_seed = %d\n", seed_mode);

	(*temp) = seed_mode;
	return 0;
}

int oplus_display_panel_set_seed(void *data)
{
	uint32_t *temp_save = data;

	printk(KERN_INFO "%s oplus_display_set_seed = %d\n", __func__, *temp_save);

	__oplus_display_set_seed(*temp_save);

	if (get_oplus_display_power_status() == OPLUS_DISPLAY_POWER_ON) {
		if (get_main_display() == NULL) {
			printk(KERN_INFO "oplus_display_set_seed and main display is null");
			return -EINVAL;
		}

		dsi_display_seed_mode(get_main_display(), seed_mode);

	} else {
		printk(KERN_ERR
				"%s oplus_display_set_seed = %d, but now display panel status is not on\n",
				__func__, *temp_save);
	}

	return 0;
}

void oplus_ofp_handle_display_effect(struct dsi_display *display, int hbm_status)
{
	int ret;

	if (!display) {
		pr_err("failed for: %s %d\n", __func__, __LINE__);
		return;
	}
	switch(hbm_status) {
	case OFP_SYNC_DEFAULT:
			if (oplus_seed_backlight) {
				int frame_time_us;

				frame_time_us = mult_frac(1000, 1000, display->panel->cur_mode->timing.refresh_rate);
				oplus_panel_process_dimming_v2(display->panel, display->panel->bl_config.bl_level, true);
				mipi_dsi_dcs_set_display_brightness(&display->panel->mipi_device, display->panel->bl_config.bl_level);
				oplus_panel_process_dimming_v2_post(display->panel, true);
				usleep_range(frame_time_us, frame_time_us + 100);
			}
			break;
	case OFP_SYNC_BEFORE_HBM_ON:
			ret = dsi_panel_seed_mode(display->panel, PANEL_LOADING_EFFECT_OFF);
			if (ret) {
				pr_err("[%s] failed to set loading effect off, ret=%d\n",
						display->panel->name, ret);
			}
			break;
	case OFP_SYNC_BEFORE_HBM_OFF:
			ret = dsi_panel_seed_mode(display->panel, seed_mode);
			if (ret) {
				pr_err("[%s] failed to set seed mode, ret=%d\n",
						display->panel->name, ret);
			}
			break;
	default:
		break;
	}
}

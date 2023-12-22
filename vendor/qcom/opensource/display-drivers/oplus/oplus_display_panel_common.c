/***************************************************************
** Copyright (C), 2022, OPLUS Mobile Comm Corp., Ltd
** File : oplus_display_panel_common.c
** Description : oplus display panel common feature
** Version : 1.0
** Date : 2022/08/01
** Author : Display
******************************************************************/
#include "oplus_display_panel_common.h"
#include "oplus_display_panel.h"
#include "oplus_display_panel_seed.h"
#include <linux/notifier.h>
#include <linux/msm_drm_notify.h>
#include <linux/soc/qcom/panel_event_notifier.h>
#include "../msm/iris/dsi_iris_loop_back.h"
#include "oplus_display_private_api.h"
#include "sde_trace.h"
#ifdef OPLUS_FEATURE_DISPLAY_ONSCREENFINGERPRINT
#include "oplus_onscreenfingerprint.h"
#endif /* OPLUS_FEATURE_DISPLAY_ONSCREENFINGERPRINT */
#if defined(CONFIG_PXLW_IRIS)
#include "dsi_iris_api.h"
#endif

#define DSI_PANEL_OPLUS_DUMMY_VENDOR_NAME  "PanelVendorDummy"
#define DSI_PANEL_OPLUS_DUMMY_MANUFACTURE_NAME  "dummy1024"

bool oplus_temp_compensation_wait_for_vsync_set = false;
int oplus_debug_max_brightness = 0;
int oplus_dither_enable = 0;
int oplus_dre_status = 0;
int oplus_cabc_status = OPLUS_DISPLAY_CABC_UI;
extern int lcd_closebl_flag;
extern int oplus_display_audio_ready;
char oplus_rx_reg[PANEL_TX_MAX_BUF] = {0x0};
char oplus_rx_len = 0;
extern int spr_mode;
extern int dynamic_osc_clock;
int mca_mode = 1;
bool apollo_backlight_enable = false;
uint64_t serial_number0 = 0x0;
uint64_t serial_number1 = 0x0;
EXPORT_SYMBOL(oplus_debug_max_brightness);
EXPORT_SYMBOL(oplus_dither_enable);

extern int dsi_display_read_panel_reg(struct dsi_display *display, u8 cmd,
		void *data, size_t len);
extern int __oplus_display_set_spr(int mode);
extern int dsi_display_spr_mode(struct dsi_display *display, int mode);
extern int dsi_panel_spr_mode(struct dsi_panel *panel, int mode);

enum {
	REG_WRITE = 0,
	REG_READ,
	REG_X,
};

int oplus_display_panel_get_id(void *buf)
{
	struct dsi_display *display = get_main_display();
	int ret = 0;
	unsigned char read[30];
	struct panel_id *panel_rid = buf;

	if (!display || !display->panel) {
		printk(KERN_INFO "oplus_display_get_panel_id and main display is null");
		ret = -1;
		return ret;
	}

	if(display->enabled == false) {
		pr_info("%s primary display is disable, try sec display\n", __func__);
		display = get_sec_display();
		if (!display) {
			pr_info("%s sec display is null\n", __func__);
			return -1;
		}
		if (display->enabled == false) {
			pr_info("%s second panel is disabled", __func__);
			return -1;
		}
	}

	/* if (get_oplus_display_power_status() == OPLUS_DISPLAY_POWER_ON) { */
	if (display->panel->power_mode == SDE_MODE_DPMS_ON) {
		if ((display->panel) && (!strcmp(display->panel->name, "boe rm692e5 dsc cmd mode panel"))) {
			ret = dsi_panel_tx_cmd_set(display->panel, DSI_CMD_PANEL_DATE_SWITCH);
			if (ret) {
				printk(KERN_ERR"%s Failed to set DSI_CMD_PANEL_DATE_SWITCH !!\n", __func__);
				return -1;
			}

			ret = dsi_display_read_panel_reg(display, display->panel->oplus_ser.serial_number_reg,
				read, display->panel->oplus_ser.serial_number_conut);

			/* 04:ID1 05:ID2 06:ID3*/
			panel_rid->DA = read[4];
			panel_rid->DB = read[5];
			panel_rid->DC = read[6];
		} else {
			ret = dsi_display_read_panel_reg(display, 0xDA, read, 1);

			if (ret < 0) {
				pr_err("failed to read DA ret=%d\n", ret);
				return -EINVAL;
			}

			panel_rid->DA = (uint32_t)read[0];

			ret = dsi_display_read_panel_reg(display, 0xDB, read, 1);

			if (ret < 0) {
				pr_err("failed to read DB ret=%d\n", ret);
				return -EINVAL;
			}

			panel_rid->DB = (uint32_t)read[0];

			ret = dsi_display_read_panel_reg(display, 0xDC, read, 1);

			if (ret < 0) {
				pr_err("failed to read DC ret=%d\n", ret);
				return -EINVAL;
			}

			panel_rid->DC = (uint32_t)read[0];
		}

	} else {
		printk(KERN_ERR
				"%s oplus_display_get_panel_id, but now display panel status is not on\n",
				__func__);
		return -EINVAL;
	}

	return ret;
}

int oplus_display_panel_get_oplus_max_brightness(void *buf)
{
	uint32_t *max_brightness = buf;
	int panel_id = (*max_brightness >> 12);
	struct dsi_display *display = get_main_display();
	if (panel_id == 1)
		display = get_sec_display();

	(*max_brightness) = display->panel->bl_config.bl_normal_max_level;

	return 0;
}

int oplus_display_panel_get_max_brightness(void *buf)
{
	uint32_t *max_brightness = buf;
	int panel_id = (*max_brightness >> 12);
	struct dsi_display *display = get_main_display();
	if (panel_id == 1)
		display = get_sec_display();

	if (oplus_debug_max_brightness == 0) {
		(*max_brightness) = display->panel->bl_config.bl_normal_max_level;
	} else {
		(*max_brightness) = oplus_debug_max_brightness;
	}

	return 0;
}

int oplus_display_panel_set_max_brightness(void *buf)
{
	uint32_t *max_brightness = buf;

	oplus_debug_max_brightness = (*max_brightness);

	return 0;
}

int oplus_display_panel_get_lcd_max_brightness(void *buf)
{
	uint32_t *lcd_max_backlight = buf;
	int panel_id = (*lcd_max_backlight >> 12);
	struct dsi_display *display = get_main_display();
	if (panel_id == 1)
		display = get_sec_display();

	(*lcd_max_backlight) = display->panel->bl_config.bl_max_level;

	DSI_INFO("[%s] get lcd max backlight: %d\n",
			display->panel->oplus_priv.vendor_name,
			*lcd_max_backlight);

	return 0;
}

extern int dc_apollo_enable;

int oplus_display_panel_get_brightness(void *buf)
{
	uint32_t *brightness = buf;
	int panel_id = (*brightness >> 12);
	struct dsi_display *display = get_main_display();
	if (panel_id == 1)
		display = get_sec_display();

	if(!strcmp(display->panel->oplus_priv.vendor_name, "AMS643YE01")) {
		(*brightness) = display->panel->bl_config.oplus_raw_bl;
	}
	else if (!strcmp(display->panel->oplus_priv.vendor_name, "BF092_AB241")) {
                if (dc_apollo_enable) {
                        if (display->panel->bl_config.bl_level > JENNIE_DC_THRESHOLD)
                                (*brightness) = display->panel->bl_config.bl_level;
                        else
                                (*brightness) = display->panel->bl_config.bl_dc_real;
                } else
                        (*brightness) = display->panel->bl_config.bl_level;
        }
	else {
		(*brightness) = display->panel->bl_config.bl_level;
	}
	return 0;
}

int oplus_display_panel_set_brightness(void *buf)
{
	int rc = 0;
	struct dsi_display *display = oplus_display_get_current_display();
	struct dsi_panel *panel = NULL;
	uint32_t *backlight = buf;

	if (!display || !display->drm_conn || !display->panel) {
		DSI_ERR("Invalid display params\n");
		return -EINVAL;
	}
	panel = display->panel;

	if (*backlight > panel->bl_config.bl_max_level ||
			*backlight < 0) {
		DSI_WARN("[%s] falied to set backlight: %d, it is out of range!\n",
				__func__, *backlight);
		return -EFAULT;
	}

	DSI_INFO("[%s] set backlight: %d\n", panel->oplus_priv.vendor_name, *backlight);

	rc = dsi_display_set_backlight(display->drm_conn, display, *backlight);

	return rc;
}

int oplus_display_panel_get_vendor(void *buf)
{
	struct panel_info *p_info = buf;
	struct dsi_display *display = NULL;
	char *vendor = NULL;
	char *manu_name = NULL;
	int panel_id = p_info->version[0];

	display = get_main_display();
	if (1 == panel_id)
		display = get_sec_display();

	if (!display || !display->panel ||
			!display->panel->oplus_priv.vendor_name ||
			!display->panel->oplus_priv.manufacture_name) {
		LCD_ERR("failed to config lcd proc device\n");
		return -EINVAL;
	}

#if defined(CONFIG_PXLW_IRIS)
	if (iris_is_chip_supported() && (!strcmp(display->panel->type, "secondary"))) {
		LCD_INFO("iris secondary panel no need config\n");
		return -EINVAL;
	}
#endif

	vendor = (char *)display->panel->oplus_priv.vendor_name;
	manu_name = (char *)display->panel->oplus_priv.manufacture_name;

	memcpy(p_info->version, vendor,
			strlen(vendor) >= 31 ? 31 : (strlen(vendor) + 1));
	memcpy(p_info->manufacture, manu_name,
			strlen(manu_name) >= 31 ? 31 : (strlen(manu_name) + 1));

	return 0;
}

int oplus_display_panel_get_panel_name(void *buf)
{
	struct panel_name *p_name = buf;
	struct dsi_display *display = NULL;
	char *name = NULL;
	int panel_id = p_name->name[0];

	display = get_main_display();
	if (1 == panel_id)
		display = get_sec_display();

	if (!display || !display->panel ||
			!display->panel->name) {
		LCD_ERR("failed to config lcd panel name\n");
		return -EINVAL;
	}

#if defined(CONFIG_PXLW_IRIS)
	if (iris_is_chip_supported() && (!strcmp(display->panel->type, "secondary"))) {
		LCD_INFO("iris secondary panel no need config\n");
		return -EINVAL;
	}
#endif

	name = (char *)display->panel->name;

	memcpy(p_name->name, name,
			strlen(name) >= (PANEL_NAME_LENS - 1) ? (PANEL_NAME_LENS - 1) : (strlen(name) + 1));

	return 0;
}

int oplus_display_panel_get_panel_bpp(void *buf)
{
	uint32_t *panel_bpp = buf;
	int bpp = 0;
	int rc = 0;
	int panel_id = (*panel_bpp >> BPP_SHIFT);
	struct dsi_display *display = get_main_display();
	struct dsi_parser_utils *utils = NULL;

	if (panel_id == 1)
		display = get_sec_display();

	if (!display || !display->panel) {
		LCD_ERR("display or panel is null\n");
		return -EINVAL;
	}

#if defined(CONFIG_PXLW_IRIS)
	if (iris_is_chip_supported() && (!strcmp(display->panel->type, "secondary"))) {
		LCD_INFO("iris secondary panel no need config\n");
		return -EINVAL;
	}
#endif

	utils = &display->panel->utils;
	if (!utils) {
		LCD_ERR("utils is null\n");
		return -EINVAL;
	}

	rc = utils->read_u32(utils->data, "qcom,mdss-dsi-bpp", &bpp);

	if (rc) {
		LCD_INFO("failed to read qcom,mdss-dsi-bpp, rc=%d\n", rc);
		return -EINVAL;
	}

	*panel_bpp = bpp / RGB_COLOR_WEIGHT;

	return 0;
}

int oplus_display_panel_get_ccd_check(void *buf)
{
	struct dsi_display *display = get_main_display();
	struct mipi_dsi_device *mipi_device;
	int rc = 0;
	unsigned int *ccd_check = buf;

	if (!display || !display->panel) {
		pr_err("failed for: %s %d\n", __func__, __LINE__);
		return -EINVAL;
	}

	/* if (get_oplus_display_power_status() != OPLUS_DISPLAY_POWER_ON) { */
	if (display->panel->power_mode != SDE_MODE_DPMS_ON) {
		printk(KERN_ERR"%s display panel in off status\n", __func__);
		return -EFAULT;
	}

	if (display->panel->panel_mode != DSI_OP_CMD_MODE) {
		pr_err("only supported for command mode\n");
		return -EFAULT;
	}

	if (!(display && display->panel->oplus_priv.vendor_name) ||
			!strcmp(display->panel->oplus_priv.vendor_name, "NT37800")) {
		(*ccd_check) = 0;
		goto end;
	}

	mipi_device = &display->panel->mipi_device;

	mutex_lock(&display->display_lock);
	mutex_lock(&display->panel->panel_lock);

	if (!dsi_panel_initialized(display->panel)) {
		rc = -EINVAL;
		goto unlock;
	}

	rc = dsi_display_cmd_engine_enable(display);

	if (rc) {
		pr_err("%s, cmd engine enable failed\n", __func__);
		goto unlock;
	}

	/* enable the clk vote for CMD mode panels */
	if (display->config.panel_mode == DSI_OP_CMD_MODE) {
		dsi_display_clk_ctrl(display->dsi_clk_handle,
				DSI_CORE_CLK, DSI_CLK_ON);
	}

	if (!strcmp(display->panel->oplus_priv.vendor_name, "AMB655UV01")) {
		 {
			char value[] = { 0x5A, 0x5A };
			rc = mipi_dsi_dcs_write(mipi_device, 0xF0, value, sizeof(value));
		 }
		 {
			char value[] = { 0x44, 0x50 };
			rc = mipi_dsi_dcs_write(mipi_device, 0xE7, value, sizeof(value));
		 }
		usleep_range(1000, 1100);
		 {
			char value[] = { 0x03 };
			rc = mipi_dsi_dcs_write(mipi_device, 0xB0, value, sizeof(value));
		 }

	} else {
		 {
			char value[] = { 0x5A, 0x5A };
			rc = mipi_dsi_dcs_write(mipi_device, 0xF0, value, sizeof(value));
		 }
		 {
			char value[] = { 0x02 };
			rc = mipi_dsi_dcs_write(mipi_device, 0xB0, value, sizeof(value));
		 }
		 {
			char value[] = { 0x44, 0x50 };
			rc = mipi_dsi_dcs_write(mipi_device, 0xCC, value, sizeof(value));
		 }
		usleep_range(1000, 1100);
		 {
			char value[] = { 0x05 };
			rc = mipi_dsi_dcs_write(mipi_device, 0xB0, value, sizeof(value));
		 }
	}

	if (display->config.panel_mode == DSI_OP_CMD_MODE) {
		rc = dsi_display_clk_ctrl(display->dsi_clk_handle,
				DSI_CORE_CLK, DSI_CLK_OFF);
	}

	dsi_display_cmd_engine_disable(display);

	mutex_unlock(&display->panel->panel_lock);
	mutex_unlock(&display->display_lock);

	if (!strcmp(display->panel->oplus_priv.vendor_name, "AMB655UV01")) {
		 {
			unsigned char read[10];

			rc = dsi_display_read_panel_reg(display, 0xE1, read, 1);

			pr_err("read ccd_check value = 0x%x rc=%d\n", read[0], rc);
			(*ccd_check) = read[0];
		 }

	} else {
		 {
			unsigned char read[10];

			rc = dsi_display_read_panel_reg(display, 0xCC, read, 1);

			pr_err("read ccd_check value = 0x%x rc=%d\n", read[0], rc);
			(*ccd_check) = read[0];
		 }
	}

	mutex_lock(&display->display_lock);
	mutex_lock(&display->panel->panel_lock);

	if (!dsi_panel_initialized(display->panel)) {
		rc = -EINVAL;
		goto unlock;
	}

	rc = dsi_display_cmd_engine_enable(display);

	if (rc) {
		pr_err("%s, cmd engine enable failed\n", __func__);
		goto unlock;
	}

	if (display->config.panel_mode == DSI_OP_CMD_MODE) {
		dsi_display_clk_ctrl(display->dsi_clk_handle,
				DSI_CORE_CLK, DSI_CLK_ON);
	}

	 {
		char value[] = { 0xA5, 0xA5 };
		rc = mipi_dsi_dcs_write(mipi_device, 0xF0, value, sizeof(value));
	 }

	if (display->config.panel_mode == DSI_OP_CMD_MODE) {
		rc = dsi_display_clk_ctrl(display->dsi_clk_handle,
				DSI_CORE_CLK, DSI_CLK_OFF);
	}

	dsi_display_cmd_engine_disable(display);
unlock:

	mutex_unlock(&display->panel->panel_lock);
	mutex_unlock(&display->display_lock);
end:
	pr_err("[%s] ccd_check = %d\n",  display->panel->oplus_priv.vendor_name,
			(*ccd_check));
	return 0;
}

int oplus_display_panel_get_serial_number(void *buf)
{
	int ret = 0, i, j;
	int len = 0;
	unsigned char read[30] = {0};
	unsigned char ret_val[1];
	PANEL_SERIAL_INFO panel_serial_info;
	uint64_t serial_number;
	struct panel_serial_number *panel_rnum = buf;
	struct dsi_display *display = get_main_display();
	struct dsi_display_ctrl *m_ctrl = NULL;
	int panel_id = panel_rnum->serial_number[0];

	if (!display || !display->panel) {
		printk(KERN_INFO
				"oplus_display_get_panel_serial_number and main display is null");
		return -1;
	}

	if (0 == panel_id && display->enabled == false) {
		pr_err("%s main panel is disabled", __func__);
		return -1;
	}

	if (1 == panel_id) {
		display = get_sec_display();
		if (!display) {
			printk(KERN_INFO "oplus_display_get_panel_serial_number and main display is null");
			return -1;
		}
		if (display->enabled == false) {
			pr_err("%s second panel is disabled", __func__);
			return -1;
		}
	}

	/* if (get_oplus_display_power_status() != OPLUS_DISPLAY_POWER_ON) { */
	if (display->panel->power_mode != SDE_MODE_DPMS_ON) {
		printk(KERN_ERR"%s display panel in off status\n", __func__);
		return ret;
	}

	if (!display->panel->panel_initialized) {
		printk(KERN_ERR"%s  panel initialized = false\n", __func__);
		return ret;
	}

	if (!display->panel->oplus_ser.serial_number_support) {
		printk(KERN_ERR"%s display panel serial number not support\n", __func__);
		return ret;
	}

	m_ctrl = &display->ctrl[display->cmd_master_idx];

	/*
	 * To fix bug id 5489022, we do not read serial number frequently.
	 * First read, then return the saved value.
	 */
	if (1 == panel_id) {
		if (serial_number1 != 0) {
			ret = scnprintf(panel_rnum->serial_number, sizeof(panel_rnum->serial_number),
					"Get panel serial number: %llx", serial_number1);
			pr_info("%s read serial_number1 0x%llx\n", __func__, serial_number1);
			return ret;
		}
	} else {
		if (serial_number0 != 0) {
			ret = scnprintf(panel_rnum->serial_number, sizeof(panel_rnum->serial_number),
					"Get panel serial number: %llx", serial_number0);
			pr_info("%s read serial_number0 0x%llx\n", __func__, serial_number0);
			return ret;
		}
	}

	/*
	 * for some unknown reason, the panel_serial_info may read dummy,
	 * retry when found panel_serial_info is abnormal.
	 */
	for (i = 0; i < 5; i++) {
		if (display->panel->power_mode != SDE_MODE_DPMS_ON) {
			printk(KERN_ERR"%s display panel in off status\n", __func__);
			return ret;
		}
		if (!display->panel->panel_initialized) {
			printk(KERN_ERR"%s	panel initialized = false\n", __func__);
			return ret;
		}
		if ((!strcmp(display->panel->name, "tianma nt37705 dsc cmd mode panel"))
			|| (!strcmp(display->panel->name, "senna22623 ab575 tm nt37705 dsc cmd mode panel"))) {
			printk(KERN_INFO"%s skip set_page\n", __func__);
		} else if (!strcmp(display->panel->name, "boe rm692e5 dsc cmd mode panel")) {
			ret = dsi_panel_tx_cmd_set(display->panel, DSI_CMD_PANEL_DATE_SWITCH);
			if (ret) {
				printk(KERN_ERR"%s Failed to set DSI_CMD_PANEL_DATE_SWITCH !!\n", __func__);
				return -1;
			}
		} else if (display->panel->oplus_ser.is_switch_page) {
			mutex_lock(&display->display_lock);
			mutex_lock(&display->panel->panel_lock);

			ret = dsi_panel_tx_cmd_set(display->panel, DSI_CMD_PANEL_INFO_SWITCH_PAGE);
			if (ret) {
				printk(KERN_ERR"%s Failed to set DSI_CMD_PANEL_INFO_SWITCH_PAGE !!\n", __func__);
				mutex_unlock(&display->panel->panel_lock);
				mutex_unlock(&display->display_lock);
				return -1;
			}
			mutex_unlock(&display->panel->panel_lock);
			mutex_unlock(&display->display_lock);
		} else if (!display->panel->oplus_ser.is_reg_lock) {
			/* unknow what this case want to do */
		} else {
			mutex_lock(&display->display_lock);
			mutex_lock(&display->panel->panel_lock);
			if (display->config.panel_mode == DSI_OP_CMD_MODE) {
				dsi_display_clk_ctrl(display->dsi_clk_handle, DSI_ALL_CLKS, DSI_CLK_ON);
			}
			 {
				char value[] = {0x5A, 0x5A};
				ret = mipi_dsi_dcs_write(&display->panel->mipi_device, 0xF0, value, sizeof(value));
			 }
			if (display->config.panel_mode == DSI_OP_CMD_MODE) {
				dsi_display_clk_ctrl(display->dsi_clk_handle, DSI_ALL_CLKS, DSI_CLK_OFF);
			}
			mutex_unlock(&display->panel->panel_lock);
			mutex_unlock(&display->display_lock);
			if (ret < 0) {
				ret = scnprintf(buf, PAGE_SIZE, "Get panel serial number failed, reason:%d", ret);
				msleep(20);
				continue;
			}
		}

		/* read multiple regs */
		if ((!strcmp(display->panel->name, "tianma nt37705 dsc cmd mode panel"))
			|| (!strcmp(display->panel->name, "senna22623 ab575 tm nt37705 dsc cmd mode panel"))) {
			printk(KERN_INFO"%s skip read_multiple_regs\n", __func__);
		} else if (display->panel->oplus_ser.is_multi_reg) {
			len = sizeof(display->panel->oplus_ser.serial_number_multi_regs) - 1;
			for (j = 0; j < len; j++) {
				ret = dsi_display_read_panel_reg(display, display->panel->oplus_ser.serial_number_multi_regs[j],
					ret_val, 1);
				read[j] = ret_val[0];
				if (ret < 0) {
					ret = scnprintf(buf, PAGE_SIZE,
						"Get panel serial number failed, reason:%d", ret);
					msleep(20);
					break;
				}
			}
		} else {
			ret = dsi_display_read_panel_reg(display, display->panel->oplus_ser.serial_number_reg,
				read, display->panel->oplus_ser.serial_number_conut);
		}

		if ((!strcmp(display->panel->name, "tianma nt37705 dsc cmd mode panel"))
			|| (!strcmp(display->panel->name, "senna22623 ab575 tm nt37705 dsc cmd mode panel"))) {
			printk(KERN_INFO"%s set_page and read_reg\n", __func__);
			mutex_lock(&display->display_lock);
			mutex_lock(&display->panel->panel_lock);

			/* switch page*/
			if (display->panel->oplus_ser.is_switch_page) {
				ret = dsi_panel_tx_cmd_set(display->panel, DSI_CMD_PANEL_INFO_SWITCH_PAGE);
				if (ret) {
					printk(KERN_ERR"%s Failed to set DSI_CMD_PANEL_INFO_SWITCH_PAGE !!\n", __func__);
					mutex_unlock(&display->panel->panel_lock);
					mutex_unlock(&display->display_lock);
					return -1;
				}
			}

			ret |= dsi_panel_read_panel_reg_unlock(m_ctrl, display->panel, display->panel->oplus_ser.serial_number_reg,
				read, display->panel->oplus_ser.serial_number_conut);
			mutex_unlock(&display->panel->panel_lock);
			mutex_unlock(&display->display_lock);
		}

		/*  0xA1               11th        12th    13th    14th    15th
		 *  HEX                0x32        0x0C    0x0B    0x29    0x37
		 *  Bit           [D7:D4][D3:D0] [D5:D0] [D5:D0] [D5:D0] [D5:D0]
		 *  exp              3      2       C       B       29      37
		 *  Yyyy,mm,dd      2014   2m      12d     11h     41min   55sec
		*/
		panel_serial_info.reg_index = display->panel->oplus_ser.serial_number_index;

		if (!strcmp(display->panel->name, "boe rm692e5 dsc cmd mode panel")) {
			read[panel_serial_info.reg_index] += 3;
			panel_serial_info.year		= (read[panel_serial_info.reg_index] & 0xF0) >> 0x4;
			panel_serial_info.year += 1;
		} else if ((!strcmp(display->panel->name, "tianma nt37705 dsc cmd mode panel"))
		|| (!strcmp(display->panel->name, "senna22623 ab575 tm nt37705 dsc cmd mode panel"))) {
			panel_serial_info.year		= (read[panel_serial_info.reg_index] & 0xF0) >> 0x4;
			panel_serial_info.year += 10;
		} else {
			panel_serial_info.year		= (read[panel_serial_info.reg_index] & 0xF0) >> 0x4;
		}

		panel_serial_info.month		= read[panel_serial_info.reg_index]	& 0x0F;
		panel_serial_info.day		= read[panel_serial_info.reg_index + 1]	& 0x1F;
		panel_serial_info.hour		= read[panel_serial_info.reg_index + 2]	& 0x1F;
		panel_serial_info.minute	= read[panel_serial_info.reg_index + 3]	& 0x3F;
		panel_serial_info.second	= read[panel_serial_info.reg_index + 4]	& 0x3F;
		panel_serial_info.reserved[0] = read[panel_serial_info.reg_index + 5];
		panel_serial_info.reserved[1] = read[panel_serial_info.reg_index + 6];

		serial_number = (panel_serial_info.year		<< 56)\
				+ (panel_serial_info.month		<< 48)\
				+ (panel_serial_info.day		<< 40)\
				+ (panel_serial_info.hour		<< 32)\
				+ (panel_serial_info.minute	<< 24)\
				+ (panel_serial_info.second	<< 16)\
				+ (panel_serial_info.reserved[0] << 8)\
				+ (panel_serial_info.reserved[1]);

		if (!panel_serial_info.year) {
			/*
			 * the panel we use always large than 2011, so
			 * force retry when year is 2011
			 */
			msleep(20);
			continue;
		}
		if (display->panel->oplus_ser.is_switch_page) {
			/* switch default page */
			mutex_lock(&display->display_lock);
			mutex_lock(&display->panel->panel_lock);
			ret = dsi_panel_tx_cmd_set(display->panel, DSI_CMD_DEFAULT_SWITCH_PAGE);
			if (ret) {
				printk(KERN_ERR"%s Failed to set DSI_CMD_DEFAULT_SWITCH_PAGE !!\n", __func__);
				mutex_unlock(&display->panel->panel_lock);
				mutex_unlock(&display->display_lock);
				return -1;
			}
			mutex_unlock(&display->panel->panel_lock);
			mutex_unlock(&display->display_lock);
		}

		ret = scnprintf(panel_rnum->serial_number, sizeof(panel_rnum->serial_number),
				"Get panel serial number: %llx", serial_number);
		/*Save serial_number value.*/
		if (1 == panel_id) {
			serial_number1 = serial_number;
		} else {
			serial_number0 = serial_number;
		}
		break;
	}

	return ret;
}

int oplus_display_set_qcom_loglevel(void *data)
{
	struct kernel_loglevel *k_loginfo = data;
	if (k_loginfo == NULL) {
		DSI_ERR("k_loginfo is null pointer\n");
		return -EINVAL;
	}

	if (k_loginfo->enable) {
		oplus_dsi_log_type |= OPLUS_DEBUG_LOG_CMD;
		oplus_dsi_log_type |= OPLUS_DEBUG_LOG_BACKLIGHT;
		oplus_dsi_log_type |= OPLUS_DEBUG_LOG_COMMON;
	} else {
		oplus_dsi_log_type &= ~OPLUS_DEBUG_LOG_CMD;
		oplus_dsi_log_type &= ~OPLUS_DEBUG_LOG_BACKLIGHT;
		oplus_dsi_log_type &= ~OPLUS_DEBUG_LOG_COMMON;
	}

	DSI_INFO("Set qcom kernel log, enable:0x%X, level:0x%X, current:0x%X\n",
			k_loginfo->enable,
			k_loginfo->log_level,
			oplus_dsi_log_type);
	return 0;
}


int oplus_big_endian_copy(void *dest, void *src, int count)
{
	int index = 0, knum = 0, rc = 0;
	uint32_t *u_dest = (uint32_t*) dest;
	char *u_src = (char*) src;

	if (dest == NULL || src == NULL) {
		printk("%s null pointer\n", __func__);
		return -EINVAL;
	}

	if (dest == src) {
		return rc;
	}

	while (count > 0) {
		u_dest[index] = ((u_src[knum] << 24) | (u_src[knum+1] << 16) | (u_src[knum+2] << 8) | u_src[knum+3]);
		index += 1;
		knum += 4;
		count = count - 1;
	}

	return rc;
}

int oplus_display_get_softiris_color_status(void *data)
{
	struct softiris_color *iris_color_status = data;
	bool color_vivid_status = false;
	bool color_srgb_status = false;
	bool color_softiris_status = false;
	bool color_dual_panel_status = false;
	bool color_dual_brightness_status = false;
	bool color_oplus_calibrate_status = false;
	struct dsi_parser_utils *utils = NULL;
	struct dsi_panel *panel = NULL;

	struct dsi_display *display = get_main_display();
	if (!display) {
		pr_err("failed for: %s %d\n", __func__, __LINE__);
		return -EINVAL;
	}

	panel = display->panel;
	if (!panel) {
		pr_err("failed for: %s %d\n", __func__, __LINE__);
		return -EINVAL;
	}

	utils = &panel->utils;
	if (!utils) {
		pr_err("failed for: %s %d\n", __func__, __LINE__);
		return -EINVAL;
	}

	color_vivid_status = utils->read_bool(utils->data, "oplus,color_vivid_status");
	DSI_INFO("oplus,color_vivid_status: %s", color_vivid_status ? "true" : "false");

	color_srgb_status = utils->read_bool(utils->data, "oplus,color_srgb_status");
	DSI_INFO("oplus,color_srgb_status: %s", color_srgb_status ? "true" : "false");

	color_softiris_status = utils->read_bool(utils->data, "oplus,color_softiris_status");
	DSI_INFO("oplus,color_softiris_status: %s", color_softiris_status ? "true" : "false");

	color_dual_panel_status = utils->read_bool(utils->data, "oplus,color_dual_panel_status");
	DSI_INFO("oplus,color_dual_panel_status: %s", color_dual_panel_status ? "true" : "false");

	color_dual_brightness_status = utils->read_bool(utils->data, "oplus,color_dual_brightness_status");
	DSI_INFO("oplus,color_dual_brightness_status: %s", color_dual_brightness_status ? "true" : "false");

	color_oplus_calibrate_status = utils->read_bool(utils->data, "oplus,color_oplus_calibrate_status");
	DSI_INFO("oplus,color_oplus_calibrate_status: %s", color_oplus_calibrate_status ? "true" : "false");

	iris_color_status->color_vivid_status = (uint32_t)color_vivid_status;
	iris_color_status->color_srgb_status = (uint32_t)color_srgb_status;
	iris_color_status->color_softiris_status = (uint32_t)color_softiris_status;
	iris_color_status->color_dual_panel_status = (uint32_t)color_dual_panel_status;
	iris_color_status->color_dual_brightness_status = (uint32_t)color_dual_brightness_status;
	iris_color_status->color_oplus_calibrate_status = (uint32_t)color_oplus_calibrate_status;

	return 0;
}

int oplus_display_panel_get_panel_type(void *data)
{
	int ret = 0;
	uint32_t *temp_save = data;
	uint32_t panel_id = (*temp_save >> 12);
	uint32_t panel_type = 0;

	struct dsi_panel *panel = NULL;
	struct dsi_parser_utils *utils = NULL;
	struct dsi_display *display = get_main_display();
	if (1 == panel_id) {
		display = get_sec_display();
	}

	if (!display) {
		LCD_ERR("display is null\n");
		return -EINVAL;
	}
	panel = display->panel;
	if (!panel) {
		LCD_ERR("panel is null\n");
		return -EINVAL;
	}

	utils = &panel->utils;
	if (!utils) {
		LCD_ERR("utils is null\n");
		return -EINVAL;
	}

	ret = utils->read_u32(utils->data, "oplus,mdss-dsi-panel-type", &panel_type);
	LCD_INFO("oplus,mdss-dsi-panel-type: %d\n", panel_type);

	*temp_save = panel_type;

	return ret;
}

int oplus_display_panel_get_id2(void)
{
	struct dsi_display *display = get_main_display();
	int ret = 0;
	unsigned char read[30];
	if(!display || !display->panel) {
		printk(KERN_INFO "oplus_display_get_panel_id and main display is null");
		return 0;
	}

	/* if(get_oplus_display_power_status() == OPLUS_DISPLAY_POWER_ON) { */
	if (display->panel->power_mode == SDE_MODE_DPMS_ON) {
		if(display == NULL) {
			printk(KERN_INFO "oplus_display_get_panel_id and main display is null");
			return 0;
		}

		if ((!strcmp(display->panel->oplus_priv.vendor_name, "S6E3HC3")) ||
			(!strcmp(display->panel->oplus_priv.vendor_name, "S6E3HC4")) ||
			(!strcmp(display->panel->oplus_priv.vendor_name, "AMB670YF01"))) {
			ret = dsi_display_read_panel_reg(display, 0xDB, read, 1);
			if (ret < 0) {
				pr_err("failed to read DB ret=%d\n", ret);
				return -EINVAL;
			}
			ret = (int)read[0];
		}
	} else {
		printk(KERN_ERR	 "%s oplus_display_get_panel_id, but now display panel status is not on\n", __func__);
		return 0;
	}

	return ret;
}

int oplus_display_panel_hbm_lightspot_check(void)
{
	int rc = 0;
	char value[] = { 0xE0 };
	char value1[] = { 0x0F, 0xFF };
	struct dsi_display *display = get_main_display();
	struct mipi_dsi_device *mipi_device;

	if (!display || !display->panel) {
		pr_err("failed for: %s %d\n", __func__, __LINE__);
		return -EINVAL;
	}

	/* if (get_oplus_display_power_status() != OPLUS_DISPLAY_POWER_ON) { */
	if (display->panel->power_mode != SDE_MODE_DPMS_ON) {
		printk(KERN_ERR"%s display panel in off status\n", __func__);
		return -EFAULT;
	}

	mipi_device = &display->panel->mipi_device;

	mutex_lock(&display->display_lock);
	mutex_lock(&display->panel->panel_lock);

	if (!dsi_panel_initialized(display->panel)) {
		pr_err("%s, dsi_panel_initialized failed\n", __func__);
		rc = -EINVAL;
		goto unlock;
	}

	rc = dsi_display_cmd_engine_enable(display);

	if (rc) {
		pr_err("%s, cmd engine enable failed\n", __func__);
		rc = -EINVAL;
		goto unlock;
	}

	/* enable the clk vote for CMD mode panels */
	if (display->config.panel_mode == DSI_OP_CMD_MODE) {
		dsi_display_clk_ctrl(display->dsi_clk_handle,
				DSI_CORE_CLK, DSI_CLK_ON);
	}

	rc = mipi_dsi_dcs_write(mipi_device, 0x53, value, sizeof(value));
	usleep_range(1000, 1100);
	rc = mipi_dsi_dcs_write(mipi_device, 0x51, value1, sizeof(value1));
	usleep_range(1000, 1100);
	pr_err("[%s] hbm_lightspot_check successfully\n",  display->panel->oplus_priv.vendor_name);

	if (display->config.panel_mode == DSI_OP_CMD_MODE) {
		rc = dsi_display_clk_ctrl(display->dsi_clk_handle,
				DSI_CORE_CLK, DSI_CLK_OFF);
	}

	dsi_display_cmd_engine_disable(display);

unlock:

	mutex_unlock(&display->panel->panel_lock);
	mutex_unlock(&display->display_lock);
	return 0;
}

int oplus_display_get_dp_support(void *buf)
{
	struct dsi_display *display = NULL;
	struct dsi_panel *d_panel = NULL;
	uint32_t *dp_support = buf;

	if (!dp_support) {
		pr_err("oplus_display_get_dp_support error dp_support is null\n");
		return -EINVAL;
	}

	display = get_main_display();
	if (!display) {
		pr_err("oplus_display_get_dp_support error get main display is null\n");
		return -EINVAL;
	}

	d_panel = display->panel;
	if (!d_panel) {
		pr_err("oplus_display_get_dp_support error get main panel is null\n");
		return -EINVAL;
	}

	*dp_support = d_panel->oplus_priv.dp_support;

	return 0;
}

int oplus_display_panel_set_audio_ready(void *data) {
	uint32_t *audio_ready = data;

	oplus_display_audio_ready = (*audio_ready);
	printk("%s oplus_display_audio_ready = %d\n", __func__, oplus_display_audio_ready);

	return 0;
}

int oplus_display_panel_dump_info(void *data) {
	int ret = 0;
	struct dsi_display * temp_display;
	struct display_timing_info *timing_info = data;

	temp_display = get_main_display();

	if (temp_display == NULL) {
		printk(KERN_INFO "oplus_display_dump_info and main display is null");
		ret = -1;
		return ret;
	}

	if(temp_display->modes == NULL) {
		printk(KERN_INFO "oplus_display_dump_info and display modes is null");
		ret = -1;
		return ret;
	}

	timing_info->h_active = temp_display->modes->timing.h_active;
	timing_info->v_active = temp_display->modes->timing.v_active;
	timing_info->refresh_rate = temp_display->modes->timing.refresh_rate;
	timing_info->clk_rate_hz_l32 = (uint32_t)(temp_display->modes->timing.clk_rate_hz & 0x00000000FFFFFFFF);
	timing_info->clk_rate_hz_h32 = (uint32_t)(temp_display->modes->timing.clk_rate_hz >> 32);

	return 0;
}

int oplus_display_panel_get_dsc(void *data) {
	int ret = 0;
	uint32_t *reg_read = data;
	unsigned char read[30];
	struct dsi_display *display = get_main_display();

	if (!display || !display->panel) {
		pr_err("failed for: %s %d\n", __func__, __LINE__);
		return -EINVAL;
	}

	/* if (get_oplus_display_power_status() == OPLUS_DISPLAY_POWER_ON) { */
	if (display->panel->power_mode == SDE_MODE_DPMS_ON) {
		ret = dsi_display_read_panel_reg(get_main_display(), 0x03, read, 1);
		if (ret < 0) {
			printk(KERN_ERR  "%s read panel dsc reg error = %d!\n", __func__, ret);
			ret = -1;
		} else {
			(*reg_read) = read[0];
			ret = 0;
		}
	} else {
		printk(KERN_ERR	 "%s but now display panel status is not on\n", __func__);
		ret = -1;
	}

	return ret;
}

int oplus_display_panel_get_closebl_flag(void *data)
{
	uint32_t *closebl_flag = data;

	(*closebl_flag) = lcd_closebl_flag;
	printk(KERN_INFO "oplus_display_get_closebl_flag = %d\n", lcd_closebl_flag);

	return 0;
}

int oplus_display_panel_set_closebl_flag(void *data)
{
	uint32_t *closebl = data;

	pr_err("lcd_closebl_flag = %d\n", (*closebl));
	if (1 != (*closebl))
		lcd_closebl_flag = 0;
	pr_err("oplus_display_set_closebl_flag = %d\n", lcd_closebl_flag);

	return 0;
}

int oplus_display_panel_get_reg(void *data)
{
	struct dsi_display *display = get_main_display();
	struct panel_reg_get *panel_reg = data;
	uint32_t u32_bytes = sizeof(uint32_t)/sizeof(char);

	if (!display) {
		return -EINVAL;
	}

	mutex_lock(&display->display_lock);

	u32_bytes = oplus_rx_len%u32_bytes ? (oplus_rx_len/u32_bytes + 1) : oplus_rx_len/u32_bytes;
	oplus_big_endian_copy(panel_reg->reg_rw, oplus_rx_reg, u32_bytes);
	panel_reg->lens = oplus_rx_len;

	mutex_unlock(&display->display_lock);

	return 0;
}

int oplus_display_panel_set_reg(void *data)
{
	char reg[PANEL_TX_MAX_BUF] = {0x0};
	char payload[PANEL_TX_MAX_BUF] = {0x0};
	u32 index = 0, value = 0;
	int ret = 0;
	int len = 0;
	struct dsi_display *display = get_main_display();
	struct panel_reg_rw *reg_rw = data;

	if (!display || !display->panel) {
		pr_err("debug for: %s %d\n", __func__, __LINE__);
		return -EFAULT;
	}

	if (reg_rw->lens > PANEL_REG_MAX_LENS) {
		pr_err("error: wrong input reg len\n");
		return -EINVAL;
	}

	if (reg_rw->rw_flags == REG_READ) {
		value = reg_rw->cmd;
		len = reg_rw->lens;
		dsi_display_read_panel_reg(get_main_display(), value, reg, len);

		for (index=0; index < len; index++) {
			printk("reg[%d] = %x ", index, reg[index]);
		}
		mutex_lock(&display->display_lock);
		memcpy(oplus_rx_reg, reg, PANEL_TX_MAX_BUF);
		oplus_rx_len = len;
		mutex_unlock(&display->display_lock);
		return 0;
	}

	if (reg_rw->rw_flags == REG_WRITE) {
		memcpy(payload, reg_rw->value, reg_rw->lens);
		reg[0] = reg_rw->cmd;
		len = reg_rw->lens;
		for (index=0; index < len; index++) {
			reg[index + 1] = payload[index];
		}

		/* if(get_oplus_display_power_status() == OPLUS_DISPLAY_POWER_ON) { */
		if (display->panel->power_mode != SDE_MODE_DPMS_OFF) {
				/* enable the clk vote for CMD mode panels */
			mutex_lock(&display->display_lock);
			mutex_lock(&display->panel->panel_lock);

			if (display->panel->panel_initialized) {
				if (display->config.panel_mode == DSI_OP_CMD_MODE) {
					dsi_display_clk_ctrl(display->dsi_clk_handle,
							DSI_ALL_CLKS, DSI_CLK_ON);
				}
				ret = mipi_dsi_dcs_write(&display->panel->mipi_device, reg[0],
						payload, len);
				if (display->config.panel_mode == DSI_OP_CMD_MODE) {
					dsi_display_clk_ctrl(display->dsi_clk_handle,
							DSI_ALL_CLKS, DSI_CLK_OFF);
				}
			}

			mutex_unlock(&display->panel->panel_lock);
			mutex_unlock(&display->display_lock);

			if (ret < 0) {
				return ret;
			}
		}
		return 0;
	}
	printk("%s error: please check the args!\n", __func__);
	return -1;
}

int oplus_display_panel_notify_blank(void *data)
{
	struct msm_drm_notifier notifier_data;
	int blank;
	uint32_t *temp_save_user = data;
	int temp_save = (*temp_save_user);

	printk(KERN_INFO "%s oplus_display_notify_panel_blank = %d\n", __func__, temp_save);

	if(temp_save == 1) {
		blank = MSM_DRM_BLANK_UNBLANK;
		notifier_data.data = &blank;
		notifier_data.id = 0;
		msm_drm_notifier_call_chain(MSM_DRM_EARLY_EVENT_BLANK,
				&notifier_data);
		msm_drm_notifier_call_chain(MSM_DRM_EVENT_BLANK,
				&notifier_data);
		oplus_event_data_notifier_trigger(DRM_PANEL_EVENT_UNBLANK, 0, true);
	} else if (temp_save == 0) {
		blank = MSM_DRM_BLANK_POWERDOWN;
		notifier_data.data = &blank;
		notifier_data.id = 0;
		msm_drm_notifier_call_chain(MSM_DRM_EARLY_EVENT_BLANK,
				&notifier_data);
		oplus_event_data_notifier_trigger(DRM_PANEL_EVENT_BLANK, 0, true);
	}
	return 0;
}

int oplus_display_panel_get_spr(void *data)
{
	uint32_t *spr_mode_user = data;

	printk(KERN_INFO "oplus_display_get_spr = %d\n", spr_mode);
	*spr_mode_user = spr_mode;

	return 0;
}

int oplus_display_panel_set_spr(void *data)
{
	uint32_t *temp_save_user = data;
	int temp_save = (*temp_save_user);
	struct dsi_display *display = get_main_display();

	if (!display || !display->panel) {
		pr_err("failed for: %s %d\n", __func__, __LINE__);
		return -EINVAL;
	}

	printk(KERN_INFO "%s oplus_display_set_spr = %d\n", __func__, temp_save);

	__oplus_display_set_spr(temp_save);
	/* if(get_oplus_display_power_status() == OPLUS_DISPLAY_POWER_ON) { */
	if (display->panel->power_mode == SDE_MODE_DPMS_ON) {
		if(get_main_display() == NULL) {
			printk(KERN_INFO "oplus_display_set_spr and main display is null");
			return 0;
		}

		dsi_display_spr_mode(get_main_display(), spr_mode);
	} else {
		printk(KERN_ERR	 "%s oplus_display_set_spr = %d, but now display panel status is not on\n", __func__, temp_save);
	}
	return 0;
}

int oplus_display_panel_get_roundcorner(void *data)
{
	uint32_t *round_corner = data;
	struct dsi_display *display = get_main_display();
	bool roundcorner = true;

	if (display && display->name &&
			!strcmp(display->name, "qcom,mdss_dsi_oplus19101boe_nt37800_1080_2400_cmd"))
		roundcorner = false;

	*round_corner = roundcorner;

	return 0;
}

int oplus_display_panel_get_dynamic_osc_clock(void *data)
{
	int rc = 0;
	struct dsi_display *display = get_main_display();
	uint32_t *osc_rate = data;

	if (!display || !display->panel) {
		DSI_ERR("Invalid display or panel\n");
		rc = -EINVAL;
		return rc;
	}

	if (!display->panel->oplus_priv.ffc_enabled) {
		DSI_WARN("[%s] FFC is disabled, failed to get osc rate\n",
				__func__);
		rc = -EFAULT;
		return rc;
	}

	mutex_lock(&display->display_lock);

	*osc_rate = display->panel->oplus_priv.osc_rate_cur;
	DSI_INFO("Read osc rate=%d\n", display->panel->oplus_priv.osc_rate_cur);

	mutex_unlock(&display->display_lock);

	return rc;
}

int oplus_display_panel_set_dynamic_osc_clock(void *data)
{
	int rc = 0;
	struct dsi_display *display = get_main_display();
	uint32_t *osc_rate = data;

	if (!display || !display->panel) {
		DSI_ERR("Invalid display or panel\n");
		rc = -EINVAL;
		return rc;
	}

	if (!display->panel->oplus_priv.ffc_enabled) {
		DSI_WARN("[%s] FFC is disabled, failed to set osc rate\n",
				__func__);
		rc = -EFAULT;
		return rc;
	}

	if(display->panel->power_mode != SDE_MODE_DPMS_ON) {
		DSI_WARN("[%s] display panel is not on\n", __func__);
		rc = -EFAULT;
		return rc;
	}

	DSI_INFO("Set osc rate=%d\n", *osc_rate);

	mutex_lock(&display->display_lock);

	rc = oplus_display_update_osc_ffc(display, *osc_rate);
	if (!rc) {
		mutex_lock(&display->panel->panel_lock);
		rc = oplus_panel_set_ffc_mode_unlock(display->panel);
		mutex_unlock(&display->panel->panel_lock);
	}

	mutex_unlock(&display->display_lock);

	return rc;
}

int oplus_display_get_cabc_status(void *buf)
{
	uint32_t *cabc_status = buf;
	struct dsi_display *display = NULL;
	struct dsi_panel *panel = NULL;

	display = get_main_display();
	if (!display) {
		DSI_ERR("No display device\n");
		return -ENODEV;
	}

	panel = display->panel;
	if (!panel) {
		DSI_ERR("No panel device\n");
		return -ENODEV;
	}

	if(panel->oplus_priv.cabc_enabled) {
		*cabc_status = oplus_cabc_status;
	} else {
		*cabc_status = OPLUS_DISPLAY_CABC_OFF;
	}
	return 0;
}

int oplus_display_set_cabc_status(void *buf)
{
	int rc = 0;
	uint32_t *cabc_status = buf;
	struct dsi_display *display = NULL;
	struct dsi_panel *panel = NULL;

	display = get_main_display();
	if (!display) {
		DSI_ERR("No display device\n");
		return -ENODEV;
	}

	panel = display->panel;
	if (!panel) {
		DSI_ERR("No panel device\n");
		return -ENODEV;
	}

	if (!panel->oplus_priv.cabc_enabled) {
		DSI_WARN("This project don't support cabc\n");
		return -EFAULT;
	}

	if (*cabc_status >= OPLUS_DISPLAY_CABC_UNKNOW) {
		DSI_ERR("Unknow cabc status = [%d]\n", *cabc_status);
		return -EINVAL;
	}

	if(get_oplus_display_power_status() == OPLUS_DISPLAY_POWER_ON) {
			if (*cabc_status == OPLUS_DISPLAY_CABC_OFF) {
				rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_CABC_OFF);
				if (rc) {
					DSI_ERR("[%s] failed to send DSI_CMD_CABC_OFF cmds, rc=%d\n",
							panel->name, rc);
				}
			} else if (*cabc_status == OPLUS_DISPLAY_CABC_UI) {
				rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_CABC_UI);
				if (rc) {
					DSI_ERR("[%s] failed to send DSI_CMD_CABC_UI cmds, rc=%d\n",
							panel->name, rc);
				}
			} else if (*cabc_status == OPLUS_DISPLAY_CABC_IMAGE) {
				rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_CABC_IMAGE);
				if (rc) {
					DSI_ERR("[%s] failed to send DSI_CMD_CABC_IMAGE cmds, rc=%d\n",
							panel->name, rc);
				}
			}  else if (*cabc_status == OPLUS_DISPLAY_CABC_VIDEO) {
				rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_CABC_VIDEO);
				if (rc) {
					DSI_ERR("[%s] failed to send DSI_CMD_CABC_VIDEO cmds, rc=%d\n",
							panel->name, rc);
				}
			}
		oplus_cabc_status = *cabc_status;
		pr_err("debug for %s, buf = [%u], oplus_cabc_status = %d\n",
				__func__, *cabc_status, oplus_cabc_status);
	} else {
		pr_err("debug for %s, buf = [%u], but display panel status is not on!\n",
				__func__, *cabc_status);
	}
	return rc;
}

int oplus_display_get_dre_status(void *buf)
{
	uint32_t *dre_status = buf;
	struct dsi_display *display = NULL;
	struct dsi_panel *panel = NULL;

	display = get_main_display();
	if (!display) {
		DSI_ERR("No display device\n");
		return -ENODEV;
	}

	panel = display->panel;
	if (!panel) {
		DSI_ERR("No panel device\n");
		return -ENODEV;
	}

	if(panel->oplus_priv.dre_enabled) {
		*dre_status = oplus_dre_status;
	} else {
		*dre_status = OPLUS_DISPLAY_DRE_OFF;
	}
	return 0;
}

int oplus_display_set_dre_status(void *buf)
{
	int rc = 0;
	uint32_t *dre_status = buf;
	struct dsi_display *display = NULL;
	struct dsi_panel *panel = NULL;

	display = get_main_display();
	if (!display) {
		DSI_ERR("No display device\n");
		return -ENODEV;
	}

	panel = display->panel;
	if (!panel) {
		DSI_ERR("No panel device\n");
		return -ENODEV;
	}

	if(!panel->oplus_priv.dre_enabled) {
		DSI_ERR("This project don't support dre\n");
		return -EFAULT;
	}

	if (*dre_status >= OPLUS_DISPLAY_DRE_UNKNOW) {
		DSI_ERR("Unknow DRE status = [%d]\n", *dre_status);
		return -EINVAL;
	}

	if(get_oplus_display_power_status() == OPLUS_DISPLAY_POWER_ON) {
		if (*dre_status == OPLUS_DISPLAY_DRE_ON) {
			/* if(mtk)  */
			/*	disp_aal_set_dre_en(0);   MTK AAL api */
		} else {
			/* if(mtk) */
			/*	disp_aal_set_dre_en(1);  MTK AAL api */
		}
		oplus_dre_status = *dre_status;
		pr_err("debug for %s, buf = [%u], oplus_dre_status = %d\n",
				__func__, *dre_status, oplus_dre_status);
	} else {
		pr_err("debug for %s, buf = [%u], but display panel status is not on!\n",
				__func__, *dre_status);
	}
	return rc;
}

int oplus_display_get_dither_status(void *buf)
{
	uint32_t *dither_enable = buf;
	*dither_enable = oplus_dither_enable;

	return 0;
}

int oplus_display_set_dither_status(void *buf)
{
	uint32_t *dither_enable = buf;
	oplus_dither_enable = *dither_enable;
	pr_err("debug for %s, buf = [%u], oplus_dither_enable = %d\n",
			__func__, *dither_enable, oplus_dither_enable);

	return 0;
}

int oplus_panel_set_ffc_mode_unlock(struct dsi_panel *panel)
{
	int rc = 0;
	u32 cmd_index = 0;

	if (panel->oplus_priv.ffc_mode_index >= FFC_MODE_MAX_COUNT) {
		DSI_ERR("Invalid ffc_mode_index=%d\n",
				panel->oplus_priv.ffc_mode_index);
		rc = -EINVAL;
		return rc;
	}

	cmd_index = DSI_CMD_FFC_MODE0 + panel->oplus_priv.ffc_mode_index;
	rc = dsi_panel_tx_cmd_set(panel, cmd_index);
	if (rc) {
		DSI_ERR("Failed to send DSI_CMD_FFC_MODE%d, rc=%d\n",
				panel->oplus_priv.ffc_mode_index,
				rc);
	}

	return rc;
}

int oplus_panel_set_ffc_kickoff_lock(struct dsi_panel *panel)
{
	int rc = 0;

	mutex_lock(&panel->oplus_ffc_lock);
	panel->oplus_priv.ffc_delay_frames--;
	if (panel->oplus_priv.ffc_delay_frames) {
		mutex_unlock(&panel->oplus_ffc_lock);
		return rc;
	}

	mutex_lock(&panel->panel_lock);
	rc = oplus_panel_set_ffc_mode_unlock(panel);
	mutex_unlock(&panel->panel_lock);

	mutex_unlock(&panel->oplus_ffc_lock);

	return rc;
}

int oplus_panel_check_ffc_config(struct dsi_panel *panel,
		struct oplus_clk_osc *clk_osc_pending)
{
	int rc = 0;
	int index;
	struct oplus_clk_osc *seq = panel->oplus_priv.clk_osc_seq;
	u32 count = panel->oplus_priv.ffc_mode_count;
	u32 last_index = panel->oplus_priv.ffc_mode_index;

	if (!seq || !count) {
		DSI_ERR("Invalid clk_osc_seq or ffc_mode_count\n");
		rc = -EINVAL;
		return rc;
	}

	for (index = 0; index < count; index++) {
		if (seq->clk_rate == clk_osc_pending->clk_rate &&
				seq->osc_rate == clk_osc_pending->osc_rate) {
			break;
		}
		seq++;
	}

	if (index < count) {
		DSI_INFO("Update ffc config: index:[%d -> %d], clk=%d, osc=%d\n",
				last_index,
				index,
				clk_osc_pending->clk_rate,
				clk_osc_pending->osc_rate);

		panel->oplus_priv.ffc_mode_index = index;
		panel->oplus_priv.clk_rate_cur = clk_osc_pending->clk_rate;
		panel->oplus_priv.osc_rate_cur = clk_osc_pending->osc_rate;
	} else {
		rc = -EINVAL;
	}

	return rc;
}

int oplus_display_update_clk_ffc(struct dsi_display *display,
		struct dsi_display_mode *cur_mode, struct dsi_display_mode *adj_mode)
{
	int rc = 0;
	struct dsi_panel *panel = display->panel;
	struct oplus_clk_osc clk_osc_pending;

	DSI_MM_INFO("DisplayDriverID@@426$$Switching ffc mode, clk:[%d -> %d]",
			display->cached_clk_rate,
			display->dyn_bit_clk);

	if (display->cached_clk_rate == display->dyn_bit_clk) {
		DSI_MM_WARN("DisplayDriverID@@427$$Ignore duplicated clk ffc setting, clk=%d",
				display->dyn_bit_clk);
		return rc;
	}

	mutex_lock(&panel->oplus_ffc_lock);

	clk_osc_pending.clk_rate = display->dyn_bit_clk;
	clk_osc_pending.osc_rate = panel->oplus_priv.osc_rate_cur;

	rc = oplus_panel_check_ffc_config(panel, &clk_osc_pending);
	if (!rc) {
		panel->oplus_priv.ffc_delay_frames = FFC_DELAY_MAX_FRAMES;
	} else {
		DSI_MM_ERR("DisplayDriverID@@427$$Failed to find ffc mode index, clk=%d, osc=%d",
				clk_osc_pending.clk_rate,
				clk_osc_pending.osc_rate);
	}

	mutex_unlock(&panel->oplus_ffc_lock);

	return rc;
}

int oplus_display_update_osc_ffc(struct dsi_display *display,
		u32 osc_rate)
{
	int rc = 0;
	struct dsi_panel *panel = display->panel;
	struct oplus_clk_osc clk_osc_pending;

	DSI_MM_INFO("DisplayDriverID@@428$$Switching ffc mode, osc:[%d -> %d]",
			panel->oplus_priv.osc_rate_cur,
			osc_rate);

	if (osc_rate == panel->oplus_priv.osc_rate_cur) {
		DSI_MM_WARN("DisplayDriverID@@429$$Ignore duplicated osc ffc setting, osc=%d",
				panel->oplus_priv.osc_rate_cur);
		return rc;
	}

	mutex_lock(&panel->oplus_ffc_lock);

	clk_osc_pending.clk_rate = panel->oplus_priv.clk_rate_cur;
	clk_osc_pending.osc_rate = osc_rate;
	rc = oplus_panel_check_ffc_config(panel, &clk_osc_pending);
	if (rc) {
		DSI_MM_ERR("DisplayDriverID@@429$$Failed to find ffc mode index, clk=%d, osc=%d",
				clk_osc_pending.clk_rate,
				clk_osc_pending.osc_rate);
	}

	mutex_unlock(&panel->oplus_ffc_lock);

	return rc;
}

int oplus_panel_parse_ffc_config(struct dsi_panel *panel)
{
	int rc = 0;
	int i;
	u32 length = 0;
	u32 count = 0;
	u32 size = 0;
	u32 *arr_32 = NULL;
	const u32 *arr;
	struct dsi_parser_utils *utils = &panel->utils;
	struct oplus_clk_osc *seq;

	if (panel->host_config.ext_bridge_mode)
		return 0;

	panel->oplus_priv.ffc_enabled = utils->read_bool(utils->data,
			"oplus,ffc-enabled");
	if (!panel->oplus_priv.ffc_enabled) {
		rc = -EFAULT;
		goto error;
	}

	arr = utils->get_property(utils->data,
			"oplus,clk-osc-sequence", &length);
	if (!arr) {
		DSI_ERR("[%s] oplus,clk-osc-sequence not found\n",
				panel->oplus_priv.vendor_name);
		rc = -EINVAL;
		goto error;
	}
	if (length & 0x1) {
		DSI_ERR("[%s] syntax error for oplus,clk-osc-sequence\n",
				panel->oplus_priv.vendor_name);
		rc = -EINVAL;
		goto error;
	}

	length = length / sizeof(u32);
	DSI_INFO("[%s] oplus,clk-osc-sequence length=%d\n",
			panel->oplus_priv.vendor_name, length);

	size = length * sizeof(u32);
	arr_32 = kzalloc(size, GFP_KERNEL);
	if (!arr_32) {
		rc = -ENOMEM;
		goto error;
	}

	rc = utils->read_u32_array(utils->data, "oplus,clk-osc-sequence",
					arr_32, length);
	if (rc) {
		DSI_ERR("[%s] cannot read oplus,clk-osc-sequence\n",
				panel->oplus_priv.vendor_name);
		goto error_free_arr_32;
	}

	count = length / 2;
	if (count > FFC_MODE_MAX_COUNT) {
		DSI_ERR("[%s] invalid ffc mode count:%d, greater than maximum:%d\n",
				panel->oplus_priv.vendor_name, count, FFC_MODE_MAX_COUNT);
		rc = -EINVAL;
		goto error_free_arr_32;
	}

	size = count * sizeof(*seq);
	seq = kzalloc(size, GFP_KERNEL);
	if (!seq) {
		rc = -ENOMEM;
		goto error_free_arr_32;
	}

	panel->oplus_priv.clk_osc_seq = seq;
	panel->oplus_priv.ffc_delay_frames = 0;
	panel->oplus_priv.ffc_mode_count = count;
	panel->oplus_priv.ffc_mode_index = 0;
	panel->oplus_priv.clk_rate_cur = arr_32[0];
	panel->oplus_priv.osc_rate_cur = arr_32[1];

	for (i = 0; i < length; i += 2) {
		DSI_INFO("[%s] clk osc seq: index=%d <%d %d>\n",
				panel->oplus_priv.vendor_name, i / 2, arr_32[i], arr_32[i+1]);
		seq->clk_rate = arr_32[i];
		seq->osc_rate = arr_32[i + 1];
		seq++;
	}

error_free_arr_32:
	kfree(arr_32);
error:
	if (rc) {
		panel->oplus_priv.ffc_enabled = false;
	}

	DSI_INFO("[%s] oplus,ffc-enabled: %s",
			panel->oplus_priv.vendor_name,
			panel->oplus_priv.ffc_enabled ? "true" : "false");

	return rc;
}

int dsi_panel_parse_oplus_config(struct dsi_panel *panel)
{
	struct dsi_parser_utils *utils = &panel->utils;
	int ret = 0;
	u32 delay = 0;

	/* Add for parse ffc config */
	oplus_panel_parse_ffc_config(panel);

	panel->oplus_priv.vendor_name = utils->get_property(utils->data,
			"oplus,mdss-dsi-vendor-name", NULL);

	if (!panel->oplus_priv.vendor_name) {
		pr_err("Failed to found panel name, using dumming name\n");
		panel->oplus_priv.vendor_name = DSI_PANEL_OPLUS_DUMMY_VENDOR_NAME;
	}

	panel->oplus_priv.manufacture_name = utils->get_property(utils->data,
			"oplus,mdss-dsi-manufacture", NULL);

	if (!panel->oplus_priv.manufacture_name) {
		pr_err("Failed to found panel name, using dumming name\n");
		panel->oplus_priv.manufacture_name = DSI_PANEL_OPLUS_DUMMY_MANUFACTURE_NAME;
	}

	panel->oplus_priv.is_pxlw_iris5 = utils->read_bool(utils->data,
					 "oplus,is_pxlw_iris5");
	DSI_INFO("is_pxlw_iris5: %s",
		 panel->oplus_priv.is_pxlw_iris5 ? "true" : "false");

	panel->oplus_priv.is_osc_support = utils->read_bool(utils->data, "oplus,osc-support");
	pr_info("[%s]osc mode support: %s", __func__, panel->oplus_priv.is_osc_support ? "Yes" : "Not");

	if (panel->oplus_priv.is_osc_support) {
		ret = utils->read_u32(utils->data, "oplus,mdss-dsi-osc-clk-mode0-rate",
					&panel->oplus_priv.osc_clk_mode0_rate);
		if (ret) {
			pr_err("[%s]failed get panel parameter: oplus,mdss-dsi-osc-clk-mode0-rate\n", __func__);
			panel->oplus_priv.osc_clk_mode0_rate = 0;
		}
		dynamic_osc_clock = panel->oplus_priv.osc_clk_mode0_rate;

		ret = utils->read_u32(utils->data, "oplus,mdss-dsi-osc-clk-mode1-rate",
					&panel->oplus_priv.osc_clk_mode1_rate);
		if (ret) {
			pr_err("[%s]failed get panel parameter: oplus,mdss-dsi-osc-clk-mode1-rate\n", __func__);
			panel->oplus_priv.osc_clk_mode1_rate = 0;
		}
	}

	/* Add for apollo */
	panel->oplus_priv.is_apollo_support = utils->read_bool(utils->data, "oplus,apollo_backlight_enable");
	apollo_backlight_enable = panel->oplus_priv.is_apollo_support;
	DSI_INFO("apollo_backlight_enable: %s", panel->oplus_priv.is_apollo_support ? "true" : "false");

	if (panel->oplus_priv.is_apollo_support) {
		ret = utils->read_u32(utils->data, "oplus,apollo-sync-brightness-level",
				&panel->oplus_priv.sync_brightness_level);

		if (ret) {
			pr_info("[%s] failed to get panel parameter: oplus,apollo-sync-brightness-level\n", __func__);
			/* Default sync brightness level is set to 200 */
			panel->oplus_priv.sync_brightness_level = 200;
		}
		panel->oplus_priv.dc_apollo_sync_enable = utils->read_bool(utils->data, "oplus,dc_apollo_sync_enable");
		if (panel->oplus_priv.dc_apollo_sync_enable) {
			ret = utils->read_u32(utils->data, "oplus,dc-apollo-backlight-sync-level",
					&panel->oplus_priv.dc_apollo_sync_brightness_level);
			if (ret) {
				pr_info("[%s] failed to get panel parameter: oplus,dc-apollo-backlight-sync-level\n", __func__);
				panel->oplus_priv.dc_apollo_sync_brightness_level = 397;
			}
			ret = utils->read_u32(utils->data, "oplus,dc-apollo-backlight-sync-level-pcc-max",
					&panel->oplus_priv.dc_apollo_sync_brightness_level_pcc);
			if (ret) {
				pr_info("[%s] failed to get panel parameter: oplus,dc-apollo-backlight-sync-level-pcc-max\n", __func__);
				panel->oplus_priv.dc_apollo_sync_brightness_level_pcc = 30000;
			}
			ret = utils->read_u32(utils->data, "oplus,dc-apollo-backlight-sync-level-pcc-min",
					&panel->oplus_priv.dc_apollo_sync_brightness_level_pcc_min);
			if (ret) {
				pr_info("[%s] failed to get panel parameter: oplus,dc-apollo-backlight-sync-level-pcc-min\n", __func__);
				panel->oplus_priv.dc_apollo_sync_brightness_level_pcc_min = 29608;
			}
			pr_info("dc apollo sync enable(%d,%d,%d)\n", panel->oplus_priv.dc_apollo_sync_brightness_level,
					panel->oplus_priv.dc_apollo_sync_brightness_level_pcc, panel->oplus_priv.dc_apollo_sync_brightness_level_pcc_min);
		}
	}

	ret = utils->read_u32(utils->data, "oplus,idle-delayms", &delay);
	if (ret == 0) {
		panel->idle_delayms = delay;
	} else {
		panel->idle_delayms = 0;
	}

	if (oplus_adfr_is_support()) {
		if (oplus_adfr_get_vsync_mode() == OPLUS_EXTERNAL_TE_TP_VSYNC) {
			/* power on with vsync_switch_gpio high bacause default timing is fhd OA 60hz */
			panel->vsync_switch_gpio_level = 1;
			/* default resolution is FHD when use mux switch */
			panel->cur_h_active = 1080;
		}
	}

	return 0;
}
EXPORT_SYMBOL(dsi_panel_parse_oplus_config);

int oplus_display_tx_cmd_set_lock(struct dsi_display *display,
		enum dsi_cmd_set_type type)
{
	int rc = 0;

	mutex_lock(&display->display_lock);
	mutex_lock(&display->panel->panel_lock);
	rc = dsi_panel_tx_cmd_set(display->panel, type);
	mutex_unlock(&display->panel->panel_lock);
	mutex_unlock(&display->display_lock);

	return rc;
}

int oplus_display_get_iris_loopback_status(void *buf)
{
	uint32_t *status = buf;

	*status = iris_loop_back_validate();
	return 0;
}


int oplus_display_panel_set_dc_real_brightness(void *data)
{
	struct dsi_display *display = get_main_display();
	uint32_t *temp_save = data;
	int rc = 0;

	if (!display || !display->panel) {
		pr_err("%s: display or display->panel is null\n", __func__);
		return -EINVAL;
	}

	display->panel->bl_config.bl_dc_real = *temp_save;

	return rc;
}

int oplus_set_dbv_frame(struct dsi_panel *panel, bool enable)
{
	int rc = 0;
	struct dsi_cmd_desc *cmds;
	struct mipi_dsi_msg msg;
	char *tx_buf = NULL;
	struct dsi_panel_cmd_set *cmd_sets;
	u8 avdd_base = 72;
	u8 avdd_out = 0;
	u8 avdd_shift = 0;
	u8 elvss_target = 0;
	u32 bl_lvl = 0;

	if (!panel || !panel->cur_mode || !panel->cur_mode->priv_info) {
		DSI_ERR("Oplus Features config No panel device\n");
		return -ENODEV;
	}

	SDE_ATRACE_BEGIN("oplus_set_dbv_frame");
	bl_lvl = panel->bl_config.bl_level;
	if (enable == true) {
		avdd_base = 78;
	}
	if(bl_lvl < 0x731)
		elvss_target = 20;
	else if (bl_lvl < 0x81E)
		elvss_target = 21;
	else if (bl_lvl < 0x90B)
		elvss_target = 22;
	else if (bl_lvl == 0x90B)
		elvss_target = 23;
	else if (bl_lvl < 0xA9B)
		elvss_target = 24;
	else if (bl_lvl < 0xB63)
		elvss_target = 25;
	else if (bl_lvl < 0xC2B)
		elvss_target = 26;
	else if (bl_lvl < 0xCF3)
		elvss_target = 27;
	else if (bl_lvl < 0xDBB)
		elvss_target = 28;
	else if (bl_lvl == 0xDBB)
		elvss_target = 29;
	else if (bl_lvl < 0xE56)
		elvss_target = 33;
	else if (bl_lvl < 0xE75)
		elvss_target = 35;
	else if (bl_lvl < 0xEB5)
		elvss_target = 38;
	else if (bl_lvl < 0xF15)
		elvss_target = 42;
	else if (bl_lvl <= 0xFFF)
		elvss_target = 49;

	avdd_shift = elvss_target-20;
	avdd_out = avdd_base + avdd_shift;
	cmd_sets = &(panel->cur_mode->priv_info->cmd_sets[DSI_CMD_SWITCH_AVDD]);
	if (cmd_sets) {
		cmds = &(cmd_sets->cmds[cmd_sets->count - 1]);
		msg = cmds->msg;
		tx_buf = (char*)msg.tx_buf;
		tx_buf[msg.tx_len-1] = avdd_out;
	} else {
		printk(KERN_ERR "%s:DSI_CMD_SWITCH_AVDD is not defined\n", __func__);
		return -EINVAL;
	}

	rc |= dsi_panel_tx_cmd_set(panel, DSI_CMD_SWITCH_AVDD);
	SDE_ATRACE_END("oplus_set_dbv_frame");

	return rc;
}

int oplus_set_dbv_frame_next(struct dsi_panel *panel, bool enable)
{
	int rc = 0;
	u32 bl_lvl = 0;
	struct dsi_cmd_desc *cmds;
	struct mipi_dsi_msg msg;
	char *tx_buf = NULL;
	struct dsi_panel_cmd_set *cmd_sets;
	SDE_ATRACE_BEGIN("oplus_set_dbv_frame_next");

	if (!panel || !panel->cur_mode || !panel->cur_mode->priv_info) {
		DSI_ERR("Oplus Features config No panel device\n");
		return -ENODEV;
	}

	bl_lvl = panel->bl_config.bl_level;
	if (enable == true)
		cmd_sets = &(panel->cur_mode->priv_info->cmd_sets[DSI_CMD_HIGH_FRE_120]);
	else
		cmd_sets = &(panel->cur_mode->priv_info->cmd_sets[DSI_CMD_LOW_FRE_120]);
	if(cmd_sets) {
		cmds = &(cmd_sets->cmds[cmd_sets->count - 1]);
		msg = cmds->msg;
		tx_buf = (char*)msg.tx_buf;

		tx_buf[msg.tx_len-1] = (bl_lvl & 0xFF);
		tx_buf[msg.tx_len-2] = (bl_lvl >> 8);
	} else {
		printk(KERN_ERR "%s:DSI_CMD_SWITCH_ELVSS is not defined\n", __func__);
		return -EINVAL;
	}

	if (enable == true) {
		if (bl_lvl <= 0x643 && bl_lvl > 0)
			panel->oplus_priv.oplus_pwm_switch_state = PWM_SWITCH_HIGH_STATE;
		else if (bl_lvl > 0x643)
			panel->oplus_priv.oplus_pwm_switch_state = PWM_SWITCH_LOW_STATE;
		else {
			DSI_ERR("illegal backlight %d\n", bl_lvl);
		}
		cmd_sets = &(panel->cur_mode->priv_info->cmd_sets[DSI_CMD_HIGH_FRE_120]);
		if(cmd_sets) {
			cmds = &(cmd_sets->cmds[cmd_sets->count - 5]);
			msg = cmds->msg;
			tx_buf = (char*)msg.tx_buf;
			if (bl_lvl <= 0x643 && bl_lvl > 0)
				tx_buf[msg.tx_len-1] = 0x4B;
			else if (bl_lvl > 0x643)
				tx_buf[msg.tx_len-1] = 0x42;
			else {
				tx_buf[msg.tx_len-1] = 0x42;
				DSI_ERR("backlight is %d set DSI_CMD_HIGH_FRE_120 plus B2 to 42\n", bl_lvl);
			}

			cmds = &(cmd_sets->cmds[cmd_sets->count - 2]);
			msg = cmds->msg;
			tx_buf = (char*)msg.tx_buf;
			if (bl_lvl <= 0x643 && bl_lvl > 0)
				tx_buf[msg.tx_len-1] = 0xD2;
			else if (bl_lvl > 0x643)
				tx_buf[msg.tx_len-1] = 0xB2;
			else {
				tx_buf[msg.tx_len-1] = 0xB2;
				DSI_ERR("backlight is %d set DSI_CMD_HIGH_FRE_120 plus E5 to B2\n", bl_lvl);
			}
		}
	}

	if (enable == true) {
		rc |= dsi_panel_tx_cmd_set(panel, DSI_CMD_HIGH_FRE_120);
	} else {
		rc |= dsi_panel_tx_cmd_set(panel, DSI_CMD_LOW_FRE_120);
	}

	SDE_ATRACE_END("oplus_set_dbv_frame_next");

	return rc;
}

int oplus_set_pulse_switch(struct dsi_panel *panel, bool enable)
{
	int rc = 0;
	u32 bl_lvl = 0;
	struct dsi_cmd_desc *cmds;
	struct mipi_dsi_msg msg;
	char *tx_buf = NULL;
	struct dsi_panel_cmd_set *cmd_sets;
	SDE_ATRACE_BEGIN("oplus_set_pulse_switch");

	if (!panel || !panel->cur_mode || !panel->cur_mode->priv_info) {
		DSI_ERR("Oplus Features config No panel device\n");
		return -ENODEV;
	}

	bl_lvl = panel->bl_config.bl_level;
	if (enable == true)
		cmd_sets = &(panel->cur_mode->priv_info->cmd_sets[DSI_CMD_HIGH_FRE_120]);
	else
		cmd_sets = &(panel->cur_mode->priv_info->cmd_sets[DSI_CMD_LOW_FRE_120]);
	if(cmd_sets) {
		cmds = &(cmd_sets->cmds[cmd_sets->count - 1]);
		msg = cmds->msg;
		tx_buf = (char*)msg.tx_buf;

		tx_buf[msg.tx_len-1] = (bl_lvl & 0xFF);
		tx_buf[msg.tx_len-2] = (bl_lvl >> 8);
	} else {
		printk(KERN_ERR "%s:DSI_CMD_SET_LPWM_PULSE is not defined\n", __func__);
		return -EINVAL;
	}

	if (enable == true) {
		if (bl_lvl <= 0x643 && bl_lvl > 0)
			panel->oplus_priv.oplus_pwm_switch_state = PWM_SWITCH_HIGH_STATE;
		else if (bl_lvl > 0x643)
			panel->oplus_priv.oplus_pwm_switch_state = PWM_SWITCH_LOW_STATE;
		else {
			DSI_ERR("illegal backlight %d\n", bl_lvl);
		}
		cmd_sets = &(panel->cur_mode->priv_info->cmd_sets[DSI_CMD_HIGH_FRE_120]);
		if(cmd_sets) {
			cmds = &(cmd_sets->cmds[cmd_sets->count - 5]);
			msg = cmds->msg;
			tx_buf = (char*)msg.tx_buf;

			if (bl_lvl <= 0x643 && bl_lvl > 0)
				tx_buf[msg.tx_len-1] = 0x4B;
			else if (bl_lvl > 0x643)
				tx_buf[msg.tx_len-1] = 0x42;
			else {
				tx_buf[msg.tx_len-1] = 0x42;
				DSI_ERR("backlight is %d set DSI_CMD_SET_HPWM_PULSE plus B2 to 42\n", bl_lvl);
			}
		}
	}

	if (enable == true) {
		rc |= dsi_panel_tx_cmd_set(panel, DSI_CMD_HIGH_FRE_120);
	} else {
		rc |= dsi_panel_tx_cmd_set(panel, DSI_CMD_LOW_FRE_120);
	}

	SDE_ATRACE_END("oplus_set_pulse_switch");

	return rc;
}

extern int sde_encoder_resource_control(struct drm_encoder *drm_enc,
		u32 sw_event);
int oplus_sde_early_wakeup(void)
{
	struct dsi_display *d_display = get_main_display();
	struct drm_encoder *drm_enc;
	if (!d_display) {
		DSI_ERR("invalid display params\n");
		return -EINVAL;
	}
	drm_enc = d_display->bridge->base.encoder;
	if (!drm_enc) {
		DSI_ERR("invalid encoder params\n");
		return -EINVAL;
	}
	sde_encoder_resource_control(drm_enc,
			7 /*SDE_ENC_RC_EVENT_EARLY_WAKEUP*/);
	return 0;
}

void oplus_need_to_sync_te(struct dsi_panel *panel)
{
	s64 us_per_frame;
	u32 vsync_width;
	ktime_t last_te_timestamp;
	int delay;

	us_per_frame = panel->cur_mode->priv_info->vsync_period;
	vsync_width = panel->cur_mode->priv_info->vsync_width;
	last_te_timestamp = panel->te_timestamp;

	SDE_ATRACE_BEGIN("oplus_need_to_sync_te");
	delay = vsync_width - (ktime_to_us(ktime_sub(ktime_get(), last_te_timestamp)) % us_per_frame);
	if (delay > 0) {
		SDE_EVT32(us_per_frame, last_te_timestamp, delay);
		usleep_range(delay, delay + 100);
	}
	SDE_ATRACE_END("oplus_need_to_sync_te");

	return;
}

void oplus_save_te_timestamp(struct sde_connector *c_conn, ktime_t timestamp)
{
	struct dsi_display *display = c_conn->display;
	if (!display || !display->panel)
		return;
	display->panel->te_timestamp = timestamp;
}

int oplus_display_pwm_pulse_switch(void *dsi_panel, unsigned int bl_level)
{
	int rc = 0;
	unsigned int count;
	unsigned int refresh_rate = 120;
	static unsigned int last_bl_level = 2047;
	struct dsi_panel *panel = dsi_panel;
	struct dsi_display_mode *mode = NULL;
	struct dsi_display *display = NULL;
	struct drm_encoder *drm_enc = NULL;
	struct sde_encoder_virt *sde_enc = NULL;
	struct dsi_cmd_desc *cmds = NULL;
	unsigned char *tx_buf = NULL;
	unsigned int bl_threshold = 0;

	pr_debug("[DISP][DEBUG][%s:%d]start\n", __func__, __LINE__);

	if (!panel || !panel->cur_mode) {
		pr_err("[DISP][ERR][%s:%d]Invalid panel params\n", __func__, __LINE__);
		return -EINVAL;
	}

	bl_threshold = panel->bl_config.pwm_turbo_gamma_bl_threshold;

	if ((bl_level == 0) || (bl_level == 1)) {
		return 0;
	}

	if (str_equal(panel->oplus_priv.vendor_name, "NT37705")
			|| str_equal(panel->oplus_priv.vendor_name, "BOE_NT37705")
			|| oplus_is_support_pwm_switch(panel)) {
		/* will go on */
	} else {
		/* early return */
		pr_debug("[DISP][DEBUG][%s:%d]it is not NT37705 BOE_NT37705 vendorname"
				"or not support pwm pulse switch\n", __func__, __LINE__);
		return 0;
	}

	mode = panel->cur_mode;
	refresh_rate = mode->timing.refresh_rate;

	display = to_dsi_display(panel->host);
	if (!display) {
		pr_err("[DISP][ERR][%s:%d]Invalid display params\n", __func__, __LINE__);
		return -EINVAL;
	}

	drm_enc = display->bridge->base.encoder;
	if (!drm_enc) {
		pr_err("[DISP][ERR][%s:%d]Invalid drm_enc params\n", __func__, __LINE__);
		return -EINVAL;
	}

	sde_enc = to_sde_encoder_virt(drm_enc);
	if (!sde_enc) {
		pr_err("[DISP][ERR][%s:%d]Invalid sde_enc params\n", __func__, __LINE__);
		return -EINVAL;
	}

	SDE_ATRACE_BEGIN("oplus_display_pwm_pulse_switch");

	if ((oplus_panel_pwm_turbo_is_enabled(panel) && (refresh_rate != 90))
			|| oplus_is_support_pwm_switch(panel)) {
		if (((bl_level <= bl_threshold) && (last_bl_level > bl_threshold))
				|| (panel->oplus_priv.pwm_power_on && (bl_level <= bl_threshold)
				&& (bl_level > 0))) {
			panel->oplus_priv.oplus_pwm_switch_state = PWM_SWITCH_HIGH_STATE;
			cmds = mode->priv_info->cmd_sets[DSI_CMD_SET_HPWM_PULSE].cmds;
			count = mode->priv_info->cmd_sets[DSI_CMD_SET_HPWM_PULSE].count;
			if (count) {
				tx_buf = (unsigned char *)cmds[count-1].msg.tx_buf;
				if (tx_buf[0] == 0x51) {
					tx_buf[1] = (bl_level >> 8);
					tx_buf[2] = (bl_level & 0xFF);
				}
			}
			pr_info("[%s] set hpwm_pulse and temp compensation\n", __func__);
			if (!panel->oplus_priv.pwm_power_on) {
				oplus_sde_early_wakeup();
				oplus_wait_for_vsync(panel);
				oplus_need_to_sync_te(panel);
			}

			rc |= dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_HPWM_PULSE);
			if (!panel->oplus_priv.pwm_power_on) {
				if (str_equal(panel->oplus_priv.vendor_name, "BOE_NT37705")
						|| oplus_is_support_pwm_switch(panel)) {
					oplus_temp_compensation_wait_for_vsync_set = true;
				} else {
					oplus_wait_for_vsync(panel);
				}
			}

			panel->oplus_priv.pwm_power_on = false;
		} else if (((bl_level > bl_threshold) && (last_bl_level <= bl_threshold))
				|| (panel->oplus_priv.pwm_power_on && bl_level > bl_threshold)) {
			panel->oplus_priv.oplus_pwm_switch_state = PWM_SWITCH_LOW_STATE;
			cmds = mode->priv_info->cmd_sets[DSI_CMD_SET_LPWM_PULSE].cmds;
			count = mode->priv_info->cmd_sets[DSI_CMD_SET_LPWM_PULSE].count;
			if (count) {
				tx_buf = (unsigned char *)cmds[count-1].msg.tx_buf;
				if (tx_buf[0] == 0x51) {
					tx_buf[1] = (bl_level >> 8);
					tx_buf[2] = (bl_level & 0xFF);
				}
			}

			pr_info("[%s] set lhpwm_pulse and temp compensation\n", __func__);
			if (!panel->oplus_priv.pwm_power_on) {
				oplus_sde_early_wakeup();
				oplus_wait_for_vsync(panel);
				oplus_need_to_sync_te(panel);
			}

			rc |= dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_LPWM_PULSE);
			if (!panel->oplus_priv.pwm_power_on) {
				if (str_equal(panel->oplus_priv.vendor_name, "BOE_NT37705")
						||oplus_is_support_pwm_switch(panel)) {
					oplus_temp_compensation_wait_for_vsync_set = true;
				} else {
					oplus_wait_for_vsync(panel);
				}
			}

			panel->oplus_priv.pwm_power_on = false;
		}
	}

	last_bl_level = bl_level;

	SDE_ATRACE_END("oplus_display_pwm_pulse_switch");

	pr_debug("[DISP][DEBUG][%s:%d]end\n", __func__, __LINE__);

	return rc;
}

int oplus_wait_for_vsync(struct dsi_panel *panel)
{
	int rc = 0;
	struct dsi_display *d_display = get_main_display();
	struct drm_encoder *drm_enc = NULL;

	if (!d_display || !d_display->bridge) {
		DSI_ERR("invalid display params\n");
		return -ENODEV;
	}
	if (!panel || !panel->cur_mode) {
		DSI_ERR("Oplus Features config No panel device\n");
		return -ENODEV;
	}
	drm_enc = d_display->bridge->base.encoder;

	if (!drm_enc) {
		DSI_ERR("invalid encoder params\n");
		return -ENODEV;
	}

	sde_encoder_wait_for_event(drm_enc, MSM_ENC_VBLANK);

	return rc;
}
EXPORT_SYMBOL(oplus_wait_for_vsync);

inline bool oplus_panel_pwm_turbo_is_enabled(struct dsi_panel *panel)
{
	if (!panel) {
		DSI_ERR("Invalid panel\n");
		return false;
	}
	return (bool)(panel->oplus_priv.pwm_turbo_support &&
			panel->oplus_priv.pwm_turbo_enabled);
}

inline bool oplus_panel_pwm_turbo_switch_state(struct dsi_panel *panel)
{
	if (!panel) {
		DSI_ERR("Invalid panel\n");
		return false;
	}

	return (bool)(panel->oplus_priv.pwm_turbo_support &&
			panel->oplus_priv.oplus_pwm_switch_state);
}

inline bool oplus_is_support_pwm_switch(struct dsi_panel *panel)
{
	return (bool)(panel->oplus_priv.pwm_switch_support);
}

/*
int oplus_panel_send_pwm_turbo_dcs_unlock(struct dsi_panel *panel, bool enabled)
60Hz开启高频PWM：	60HZ低频--120HZ低频--avdd/gamma/elvss/pulse--120HZ高频--60HZ高频
	qcom,mdss-dsi-timing-switch-120-command			(60Hz切120Hz低频)
	qcom,mdss-dsi-switch-avdd-command
	qcom,mdss-dsi-switch-high-fre-120-command
	qcom,mdss-dsi-timing-switch-120-high-fre-command	(60Hz切120Hz高频)
	qcom,mdss-dsi-timing-switch-high-fre-command

120Hz开启高频PWM:	120HZ低频--avdd/gamma/elvss/pulse--120HZ高频
	qcom,mdss-dsi-switch-avdd-command
	qcom,mdss-dsi-switch-high-fre-120-command
	qcom,mdss-dsi-timing-switch-high-fre-command

60Hz关闭高频PWM：	60HZ高频--120HZ高频--avdd/gamma/elvss/pulse--120HZ低频--60HZ低频
	qcom,mdss-dsi-timing-switch-120-high-fre-command	(60Hz切120Hz高频)
	qcom,mdss-dsi-switch-avdd-command
	qcom,mdss-dsi-switch-low-fre-120-command
	qcom,mdss-dsi-timing-switch-120-command			(60Hz切120Hz低频)
	qcom,mdss-dsi-timing-switch-command

120Hz关闭高频PWM：	120HZ高频--avdd/gamma/elvss/pulse--120HZ低频
	qcom,mdss-dsi-switch-avdd-command
	qcom,mdss-dsi-switch-low-fre-120-command
	qcom,mdss-dsi-timing-switch-command
*/
int oplus_panel_send_pwm_turbo_dcs_unlock(struct dsi_panel *panel, bool enabled)
{
	int rc = 0;

	if (panel->power_mode != SDE_MODE_DPMS_ON) {
		DSI_WARN("[%s] display panel is not on\n", __func__);
		rc = -EFAULT;
		return rc;
	}
	if (panel->cur_mode->timing.refresh_rate != 90)
		rc |= dsi_panel_tx_cmd_set(panel, DSI_CMD_RESET_SCANLINE);
	if (enabled) {
		if (panel->cur_mode->timing.refresh_rate == 60)
			rc |= dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_TIMING_SWITCH_120);
		if (panel->cur_mode->timing.refresh_rate != 90) {
			if (!panel->oplus_priv.pwm_turbo_ignore_set_dbv_frame) {
				rc |= oplus_set_dbv_frame(panel, enabled);
				rc |= oplus_set_dbv_frame_next(panel, enabled);
			} else {
				rc |= oplus_set_pulse_switch(panel, enabled);
			}
		}
		if (panel->cur_mode->timing.refresh_rate == 60)
			rc |= dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_TIMING_SWITCH_120_HIGH_FRE);
		if (panel->cur_mode->timing.refresh_rate != 90)
			rc |= dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_TIMING_SWITCH_HIGH_FRE);
	} else {
		if (panel->cur_mode->timing.refresh_rate == 60)
			rc |= dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_TIMING_SWITCH_120_HIGH_FRE);
		if (panel->cur_mode->timing.refresh_rate != 90) {
			if (!panel->oplus_priv.pwm_turbo_ignore_set_dbv_frame) {
				rc |= oplus_set_dbv_frame(panel, enabled);
				rc |= oplus_set_dbv_frame_next(panel, enabled);
			} else {
				rc |= oplus_set_pulse_switch(panel, enabled);
			}
		}
		if (panel->cur_mode->timing.refresh_rate == 60)
			rc |= dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_TIMING_SWITCH_120);
		if (panel->cur_mode->timing.refresh_rate != 90)
			rc |= dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_TIMING_SWITCH);
	}
	if (panel->cur_mode->timing.refresh_rate != 90)
		rc |= dsi_panel_tx_cmd_set(panel, DSI_CMD_RECOVERY_SCANLINE);

	panel->oplus_priv.pwm_turbo_status = enabled;

	if (rc)
		DSI_ERR("[%s] failed to send DSI_CMD_PWM_TURBO_%s cmds, rc=%d\n",
				panel->oplus_priv.vendor_name, enabled ? "ON" : "OFF", rc);

	return rc;
}

int oplus_panel_update_pwm_turbo_lock(struct dsi_panel *panel, bool enabled)
{
	int rc = 0;

	panel->oplus_priv.pwm_turbo_enabled = enabled;
	SDE_ATRACE_BEGIN("oplus_panel_update_pwm_turbo_lock");
	oplus_panel_event_data_notifier_trigger(panel,
			DRM_PANEL_EVENT_PWM_TURBO, enabled, true);

	mutex_lock(&panel->panel_lock);
	rc = oplus_panel_send_pwm_turbo_dcs_unlock(panel, enabled);
	mutex_unlock(&panel->panel_lock);
	SDE_ATRACE_END("oplus_panel_update_pwm_turbo_lock");

	return rc;
}

int oplus_display_panel_set_pwm_turbo(void *data)
{
	int rc = 0;
	struct dsi_display *display = NULL;
	struct dsi_panel *panel = NULL;
	uint32_t *pwm_enable = data;

	if (!data) {
		pr_err("%s: set pwm status data is null\n", __func__);
		return -EINVAL;
	}

	display = get_main_display();

	if (!display || !display->panel) {
		pr_err("%s:get main display failed", __func__);
		return -EINVAL;
	}

	panel = display->panel;

	if (!panel->oplus_priv.pwm_turbo_support) {
		DSI_WARN("[%s] Falied to set pwm turbo status, because it is nonsupport\n",
				__func__);
		rc = -EFAULT;
		return rc;
	}

	if (*pwm_enable == panel->oplus_priv.pwm_turbo_enabled) {
		DSI_WARN("Skip setting duplicate pwm turbo status: %d\n", *pwm_enable);
		rc = -EFAULT;
		return rc;
	}

	mutex_lock(&display->display_lock);
	if (panel->power_mode != SDE_MODE_DPMS_OFF)
		panel->oplus_priv.pwm_turbo_enabled = *pwm_enable;
	mutex_unlock(&display->display_lock);
	DSI_INFO("set pwm_turbo_enabled: %d\n", panel->oplus_priv.pwm_turbo_enabled);

	return rc;
}

int oplus_display_panel_get_pwm_turbo(void *buf)
{
	int rc = 0;
	struct dsi_display *display = NULL;
	struct dsi_panel *panel = NULL;
	uint32_t *pwm_enable = buf;

	display = get_main_display();

	if (!buf) {
		pr_err("%s: set pwm status buf is null\n", __func__);
		return -EINVAL;
	}

	if (!display || !display->panel) {
		pr_err("%s:get main display failed", __func__);
		return -EINVAL;
	}

	panel = display->panel;

	if (!panel->oplus_priv.pwm_turbo_support) {
		DSI_WARN("[%s] Falied to get pwm turbo status, because it is nonsupport\n",
				__func__);
		rc = -EFAULT;
		return rc;
	}

	mutex_lock(&display->display_lock);
	mutex_lock(&panel->panel_lock);

	*pwm_enable = panel->oplus_priv.pwm_turbo_enabled;

	mutex_unlock(&panel->panel_lock);
	mutex_unlock(&display->display_lock);

	DSI_INFO("Get pwm turbo status: %d\n", *pwm_enable);

	return rc;
}

int oplus_display_pwm_turbo_kickoff(void)
{
	struct dsi_display *display = NULL;
	struct dsi_panel *panel = NULL;
	int rc = 0;

	display = get_main_display();
	if (!display) {
		DSI_ERR("failed for: %s %d\n", __func__, __LINE__);
		return -EINVAL;
	}

	panel = display->panel;
	if (!panel) {
		DSI_ERR("failed for: %s %d\n", __func__, __LINE__);
		return -EINVAL;
	}

	if (!panel->oplus_priv.pwm_turbo_support) {
		DSI_DEBUG("[%s] Falied to set pwm turbo status, because it is nonsupport\n", __func__);
		return 0;
	}

	if (panel->power_mode != SDE_MODE_DPMS_OFF) {
		if(panel->oplus_priv.pwm_turbo_enabled != panel->oplus_priv.pwm_turbo_status) {
			DSI_INFO("pwm_turbo_enabled:%d ", panel->oplus_priv.pwm_turbo_enabled);
			rc = oplus_panel_update_pwm_turbo_lock(panel, panel->oplus_priv.pwm_turbo_enabled);
		}
	}
	return rc;
}

int oplus_panel_tx_cmd_update(struct dsi_panel *panel, enum dsi_cmd_set_type *type)
{
	int last_type = *type;
	if (!panel || !panel->cur_mode)
		return -EINVAL;

	if (oplus_panel_pwm_turbo_is_enabled(panel) && panel->cur_mode->timing.refresh_rate != 90) {
		switch(*type) {
		case DSI_CMD_SET_ON:
		case DSI_CMD_SET_ON_HIGH_FRE:
			*type = DSI_CMD_SET_ON_HIGH_FRE;
			break;
		case DSI_CMD_SET_TIMING_SWITCH:
		case DSI_CMD_SET_TIMING_SWITCH_HIGH_FRE:
			*type = DSI_CMD_SET_TIMING_SWITCH_HIGH_FRE;
			break;
		case DSI_CMD_SET_NOLP:
		case DSI_CMD_SET_NOLP_HPWM:
			*type = DSI_CMD_SET_NOLP_HPWM;
			break;
		case DSI_CMD_SET_LP1:
		case DSI_CMD_SET_LP1_HPWM:
			*type = DSI_CMD_SET_LP1_HPWM;
			break;
		default:
			break;
		}

	} else {
		switch(*type) {
		case DSI_CMD_SET_ON:
		case DSI_CMD_SET_ON_HIGH_FRE:
			*type = DSI_CMD_SET_ON;
			break;
		case DSI_CMD_SET_TIMING_SWITCH:
		case DSI_CMD_SET_TIMING_SWITCH_HIGH_FRE:
			*type = DSI_CMD_SET_TIMING_SWITCH;
			break;
		case DSI_CMD_SET_NOLP:
		case DSI_CMD_SET_NOLP_HPWM:
			*type = DSI_CMD_SET_NOLP;
			break;
		case DSI_CMD_SET_LP1:
		case DSI_CMD_SET_LP1_HPWM:
			*type = DSI_CMD_SET_LP1;
			break;
		default:
			break;
		}
	}
	DSI_INFO("pwm_turbo_enabled:%d set type from %d to %d", panel->oplus_priv.pwm_turbo_enabled, last_type, *type);

	return 0;
}

struct LCM_setting_table {
	unsigned int count;
	u8 *para_list;
};

unsigned char Skip_frame_Para[12][17]=
{
	/* 120HZ-DUTY 90HZ-DUTY 120HZ-DUTY 120HZ-VREF2 90HZ-VREF2 144HZ-VREF2 vdata DBV */
	{32, 40, 48, 32, 40, 32, 40, 48, 55, 55, 55, 55, 55, 55, 55, 55, 55}, /*HBM*/
	{32, 40, 48, 32, 40, 32, 40, 48, 27, 27, 36, 29, 29, 38, 27, 27, 36}, /*2315<=DBV<3515*/
	{32, 40, 48, 32, 40, 32, 40, 48, 27, 27, 36, 29, 29, 38, 27, 27, 36}, /*1604<=DBV<2315*/
	{8, 8, 8, 4, 4, 8, 8, 8, 30, 30, 30, 31, 31, 31, 30, 30, 30}, /*1511<=DBV<1604*/
	{8, 8, 8, 4, 4, 8, 8, 8, 30, 30, 30, 31, 31, 31, 30, 30, 30}, /*1419<=DBV<1511*/
	{4, 8, 8, 4, 4, 4, 8, 8, 30, 30, 30, 31, 31, 31, 30, 30, 30}, /*1328<=DBV<1419*/
	{4, 8, 8, 4, 4, 4, 8, 8, 30, 30, 30, 31, 31, 31, 30, 30, 30}, /*1212<=DBV<1328*/
	{4, 4, 4, 4, 4, 4, 4, 4, 29, 29, 29, 30, 30, 30, 29, 29, 29}, /*1096<=DBV<1212*/
	{4, 4, 4, 4, 4, 4, 4, 4, 29, 29, 29, 30, 30, 30, 29, 29, 29}, /*950<=DBV<1096*/
	{0, 4, 4, 0, 0, 0, 4, 4, 28, 28, 28, 30, 30, 30, 28, 28, 28}, /*761<=DBV<950*/
	{0, 0, 0, 0, 0, 0, 0, 0, 28, 28, 28, 28, 28, 28, 28, 28, 28}, /*544<=DBV<761*/
	{0, 0, 0, 0, 0, 0, 0, 0, 27, 27, 27, 28, 28, 28, 27, 27, 27}, /*8<=DBV<544*/
};

int oplus_display_update_dbv(struct dsi_panel *panel)
{
	int i = 0;
	int rc = 0;
	int a_size = 0;
	unsigned int bl_lvl;
	unsigned char para[17];
	struct dsi_display_mode *mode;
	struct dsi_cmd_desc *cmds;
	struct LCM_setting_table temp_dbv_cmd[50];
	uint8_t voltage1, voltage2, voltage3, voltage4;
	unsigned short vpark = 0;
	unsigned char voltage = 0;

	if (IS_ERR_OR_NULL(panel)) {
		pr_info("[DISP][INFO][%s:%d]Invalid params\n", __func__, __LINE__);
		return -EINVAL;
	}

	if (IS_ERR_OR_NULL(&(panel->bl_config))) {
		pr_info("[DISP][INFO][%s:%d]Invalid params\n", __func__, __LINE__);
		return -EINVAL;
	}

	if (IS_ERR_OR_NULL(panel->cur_mode)) {
		pr_info("[DISP][INFO][%s:%d]Invalid params\n", __func__, __LINE__);
		return -EINVAL;
	}

	if (panel->is_secondary) {
		return rc;
	}

	mode = panel->cur_mode;
	bl_lvl = panel->bl_config.bl_level;

	if (IS_ERR_OR_NULL(mode->priv_info)) {
		pr_info("[DISP][INFO][%s:%d]Invalid params\n", __func__, __LINE__);
		return -EINVAL;
	}

	if (IS_ERR_OR_NULL(&(mode->priv_info->cmd_sets[DSI_CMD_SKIPFRAME_DBV]))) {
		pr_info("[DISP][INFO][%s:%d]Invalid params\n", __func__, __LINE__);
		return -EINVAL;
	}

	cmds = mode->priv_info->cmd_sets[DSI_CMD_SKIPFRAME_DBV].cmds;
	a_size = mode->priv_info->cmd_sets[DSI_CMD_SKIPFRAME_DBV].count;

	for(i = 0; i < a_size; i++) {
		temp_dbv_cmd[i].count = cmds[i].msg.tx_len;
		temp_dbv_cmd[i].para_list = (u8 *)cmds[i].msg.tx_buf;
	}

	if(bl_lvl > 3515) {
		for(i = 0; i < 17; i++) {
			para[i] = Skip_frame_Para[0][i]; }
	} else if(bl_lvl >= 2315) {
		for(i = 0; i < 17; i++) {
			para[i] = Skip_frame_Para[1][i]; }
	} else if(bl_lvl >= 1604) {
		for(i = 0; i < 17; i++) {
			para[i] = Skip_frame_Para[2][i]; }
	} else if(bl_lvl >= 1511) {
		for(i = 0; i < 17; i++) {
			para[i] = Skip_frame_Para[3][i]; }
	} else if(bl_lvl >= 1419) {
		for(i = 0; i < 17; i++) {
			para[i] = Skip_frame_Para[4][i]; }
	} else if(bl_lvl >= 1328) {
		for(i = 0; i < 17; i++) {
			para[i] = Skip_frame_Para[5][i]; }
	} else if(bl_lvl >= 1212) {
		for(i = 0; i < 17; i++) {
			para[i] = Skip_frame_Para[6][i]; }
	} else if(bl_lvl >= 1096) {
		for(i = 0; i < 17; i++) {
			para[i] = Skip_frame_Para[7][i]; }
	} else if(bl_lvl >= 950) {
		for(i = 0; i < 17; i++) {
			para[i] = Skip_frame_Para[8][i]; }
	} else if(bl_lvl >= 761) {
		for(i = 0; i < 17; i++) {
			para[i] = Skip_frame_Para[9][i]; }
	} else if(bl_lvl >= 544) {
		for(i = 0; i < 17; i++) {
			para[i] = Skip_frame_Para[10][i]; }
	} else {
		for(i = 0; i < 17; i++) {
			para[i] = Skip_frame_Para[11][i]; }
	}

	for(i=0;i<3;i++){
		temp_dbv_cmd[2].para_list[4+i+1] = para[0];
		temp_dbv_cmd[2].para_list[8+i+1] = para[1];
		temp_dbv_cmd[2].para_list[12+i+1] = para[2];
		temp_dbv_cmd[4].para_list[4+i+1] = para[3];
		temp_dbv_cmd[4].para_list[8+i+1] = para[4];
		temp_dbv_cmd[6].para_list[4+i+1] = para[5];
		temp_dbv_cmd[6].para_list[8+i+1] = para[6];
	}
	for(i=0;i<3;i++){
		temp_dbv_cmd[8].para_list[i+1] = para[8+i];
		temp_dbv_cmd[8].para_list[9+i+1] = para[11+i];
		temp_dbv_cmd[8].para_list[18+i+1] = para[14+i];
	}

	voltage = 69;
	vpark = (69 - voltage) * 1024 / (69 - 10);
	voltage1 = ((vpark & 0xFF00) >> 8) + ((vpark & 0xFF00) >> 6) + ((vpark & 0xFF00) >> 4);
	voltage2 = vpark & 0xFF;
	voltage3 = vpark & 0xFF;
	voltage4 = vpark & 0xFF;
	temp_dbv_cmd[16].para_list[0+1] = voltage1;
	temp_dbv_cmd[16].para_list[1+1] = voltage2;
	temp_dbv_cmd[16].para_list[2+1] = voltage3;
	temp_dbv_cmd[16].para_list[3+1] = voltage4;

	if(bl_lvl > 0x643) {
		temp_dbv_cmd[9].para_list[0+1] = 0xB2;
		temp_dbv_cmd[11].para_list[0+1] = 0xB2;
		temp_dbv_cmd[13].para_list[0+1] = 0xB2;
		temp_dbv_cmd[19].para_list[0+1] = 0x02;
		temp_dbv_cmd[19].para_list[1+1] = 0x03;
		temp_dbv_cmd[19].para_list[2+1] = 0x42;
	} else {
		temp_dbv_cmd[9].para_list[0+1] = 0xD2;
		temp_dbv_cmd[11].para_list[0+1] = 0xE2;
		temp_dbv_cmd[13].para_list[0+1] = 0xD2;
		temp_dbv_cmd[19].para_list[0+1] = 0x0F;
		temp_dbv_cmd[19].para_list[1+1] = 0x17;
		temp_dbv_cmd[19].para_list[2+1] = 0x4E;
	}

	temp_dbv_cmd[20].para_list[0+1] = (bl_lvl >> 8);
	temp_dbv_cmd[20].para_list[1+1] = (bl_lvl & 0xff);

	rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SKIPFRAME_DBV);
	if (rc < 0)
		DSI_ERR("Failed to set DSI_CMD_SKIPFRAME_DBV \n");

	return rc;
}

int oplus_display_panel_set_demua()
{
	u32 bl_lvl = 0;
	int rc = 0;
	struct dsi_display *display = NULL;
	struct dsi_panel *panel = NULL;

	display = get_main_display();
	if (!display) {
		DSI_ERR("failed for: %s %d\n", __func__, __LINE__);
		return -EINVAL;
	}

	panel = display->panel;
	if (!panel) {
		DSI_ERR("failed for: %s %d\n", __func__, __LINE__);
		return -EINVAL;
	}

	/* if not this TM_NT37705_DVT panel, return */
	if (strcmp(panel->oplus_priv.vendor_name, "TM_NT37705_DVT")) {
		return rc;
	}

	if (panel->is_secondary) {
		return rc;
	}

	bl_lvl = panel->bl_config.bl_level;
#ifdef OPLUS_FEATURE_DISPLAY_ONSCREENFINGERPRINT
	if (oplus_ofp_is_supported()) {
		if (oplus_ofp_backlight_filter(panel, bl_lvl)) {
			return rc;
		}
	}
#endif /* OPLUS_FEATURE_DISPLAY_ONSCREENFINGERPRINT */
	if ((!strcmp(panel->name, "senna ab575 tm nt37705 dsc cmd mode panel"))
	|| (!strcmp(panel->name, "senna ab575 04id tm nt37705 dsc cmd mode panel"))) {
		if (iris_is_pt_mode(panel)) {
			return rc;
		}
	}

	mutex_lock(&display->display_lock);
	mutex_lock(&panel->panel_lock);

	if (panel->power_mode != SDE_MODE_DPMS_OFF) {
		if (bl_lvl > 0x644 && panel->oplus_priv.last_demua_status != 1) {
			SDE_ATRACE_BEGIN("oplus_display_panel_set_demua");
			rc |= dsi_display_override_dma_cmd_trig(display, DSI_TRIGGER_SW_SEOF);
			rc |= dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_BL_DEMURAL1);
			rc |= dsi_display_override_dma_cmd_trig(display, DSI_TRIGGER_NONE);
			panel->oplus_priv.last_demua_status = 1;
			SDE_ATRACE_END("oplus_display_panel_set_demua");
		} else if (bl_lvl < 0x644 && bl_lvl >= 0x530 && panel->oplus_priv.last_demua_status != 2) {
			SDE_ATRACE_BEGIN("oplus_display_panel_set_demua");
			rc |= dsi_display_override_dma_cmd_trig(display, DSI_TRIGGER_SW_SEOF);
			rc |= dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_BL_DEMURAL2);
			rc |= dsi_display_override_dma_cmd_trig(display, DSI_TRIGGER_NONE);
			panel->oplus_priv.last_demua_status = 2;
			SDE_ATRACE_END("oplus_display_panel_set_demua");
		} else if (bl_lvl < 0x530 && bl_lvl >= 0x33A && panel->oplus_priv.last_demua_status != 3) {
			SDE_ATRACE_BEGIN("oplus_display_panel_set_demua");
			rc |= dsi_display_override_dma_cmd_trig(display, DSI_TRIGGER_SW_SEOF);
			rc |= dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_BL_DEMURAL3);
			rc |= dsi_display_override_dma_cmd_trig(display, DSI_TRIGGER_NONE);
			panel->oplus_priv.last_demua_status = 3;
			SDE_ATRACE_END("oplus_display_panel_set_demua");
		} else if (bl_lvl < 0x339 && bl_lvl >= 0x25C && panel->oplus_priv.last_demua_status != 4) {
			SDE_ATRACE_BEGIN("oplus_display_panel_set_demua");
			rc |= dsi_display_override_dma_cmd_trig(display, DSI_TRIGGER_SW_SEOF);
			rc |= dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_BL_DEMURAL4);
			rc |= dsi_display_override_dma_cmd_trig(display, DSI_TRIGGER_NONE);
			panel->oplus_priv.last_demua_status = 4;
			SDE_ATRACE_END("oplus_display_panel_set_demua");
		} else if (bl_lvl < 0x25C && bl_lvl >= 0x196 && panel->oplus_priv.last_demua_status != 5) {
			SDE_ATRACE_BEGIN("oplus_display_panel_set_demua");
			rc |= dsi_display_override_dma_cmd_trig(display, DSI_TRIGGER_SW_SEOF);
			rc |= dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_BL_DEMURAL5);
			rc |= dsi_display_override_dma_cmd_trig(display, DSI_TRIGGER_NONE);
			panel->oplus_priv.last_demua_status = 5;
			SDE_ATRACE_END("oplus_display_panel_set_demua");
		} else if (bl_lvl < 0x196 && bl_lvl >= 0x008 && panel->oplus_priv.last_demua_status != 6) {
			SDE_ATRACE_BEGIN("oplus_display_panel_set_demua");
			rc |= dsi_display_override_dma_cmd_trig(display, DSI_TRIGGER_SW_SEOF);
			rc |= dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_BL_DEMURAL6);
			rc |= dsi_display_override_dma_cmd_trig(display, DSI_TRIGGER_NONE);
			panel->oplus_priv.last_demua_status = 6;
			SDE_ATRACE_END("oplus_display_panel_set_demua");
		} else {
		}
	}

	if (rc) {
		DSI_ERR("failed to oplus_display_panel_set_demua, rc = %d\n", rc);
	}

	mutex_unlock(&panel->panel_lock);
	mutex_unlock(&display->display_lock);

	return rc;
}

void oplus_apollo_async_bl_delay(struct dsi_panel *panel)
{
	s64 us_per_frame;
	u32 async_bl_delay;
	ktime_t last_te_timestamp;
	int delay;
	char tag_name[64];
	u32 debounce_time = 3000;
	u32 frame_end;

	us_per_frame = panel->cur_mode->priv_info->vsync_period;
	async_bl_delay = panel->cur_mode->priv_info->async_bl_delay;
	last_te_timestamp = panel->te_timestamp;

	delay = async_bl_delay - (ktime_to_us(ktime_sub(ktime_get(), last_te_timestamp)) % us_per_frame);
	snprintf(tag_name, sizeof(tag_name), "async_bl_delay: delay %d us", delay);

	if (delay > 0) {
		SDE_ATRACE_BEGIN(tag_name);
		SDE_EVT32(us_per_frame, last_te_timestamp, delay);
		usleep_range(delay, delay + 100);
		SDE_ATRACE_END(tag_name);
	}

	frame_end = us_per_frame - (ktime_to_us(ktime_sub(ktime_get(), last_te_timestamp)) % us_per_frame);

	if (frame_end < debounce_time) {
		delay = frame_end + async_bl_delay;
		snprintf(tag_name, sizeof(tag_name), "async_bl_delay: delay %d us to next frame", delay);
		SDE_ATRACE_BEGIN(tag_name);
		usleep_range(delay, delay + 100);
		SDE_ATRACE_END(tag_name);
	}

	return;
}

int oplus_display_send_dcs_lock(struct dsi_display *display,
		enum dsi_cmd_set_type type)
{
	int rc = 0;

	if (!display || !display->panel) {
		LCD_ERR("invalid display panel\n");
		return -ENODEV;
	}

	if (display->panel->power_mode == SDE_MODE_DPMS_OFF) {
		LCD_ERR("display panel is in off status\n");
		return -EINVAL;
	}

	if (type < DSI_CMD_SET_MAX) {
		mutex_lock(&display->display_lock);
		/* enable the clk vote for CMD mode panels */
		if (display->config.panel_mode == DSI_OP_CMD_MODE) {
			rc = dsi_display_clk_ctrl(display->dsi_clk_handle,
				DSI_CORE_CLK, DSI_CLK_ON);
			if (rc) {
				LCD_ERR("failed to enable DSI clocks, rc=%d\n", rc);
				mutex_unlock(&display->display_lock);
				return -EFAULT;
			}
		}

		mutex_lock(&display->panel->panel_lock);
		rc = dsi_panel_tx_cmd_set(display->panel, type);
		mutex_unlock(&display->panel->panel_lock);

		/* disable the clk vote for CMD mode panels */
		if (display->config.panel_mode == DSI_OP_CMD_MODE) {
			rc = dsi_display_clk_ctrl(display->dsi_clk_handle,
				DSI_CORE_CLK, DSI_CLK_OFF);
			if (rc) {
				LCD_ERR("failed to disable DSI clocks, rc=%d\n", rc);
			}
		}
		mutex_unlock(&display->display_lock);
	} else {
		LCD_ERR("dcs[%d] is out of range", type);
		return -EINVAL;
	}

	return rc;
}

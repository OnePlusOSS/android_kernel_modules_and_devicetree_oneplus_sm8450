/***************************************************************
** Copyright (C), 2022, OPLUS Mobile Comm Corp., Ltd
**
** File : oplus_display_private_api.h
** Description : oplus display private api implement
** Version : 1.0
** Date : 2022/08/01
** Author : Display
******************************************************************/
#ifndef _OPLUS_DISPLAY_PRIVATE_API_H_
#define _OPLUS_DISPLAY_PRIVATE_API_H_

#include <linux/err.h>
#include <linux/list.h>
#include <linux/of.h>
#include <linux/err.h>
#include "msm_drv.h"
#include "sde_connector.h"
#include "sde_crtc.h"
#include "sde_hw_dspp.h"
#include "sde_plane.h"
#include "msm_mmu.h"
#include "dsi_display.h"
#include "dsi_panel.h"
#include "dsi_ctrl.h"
#include "dsi_ctrl_hw.h"
#include "dsi_drm.h"
#include "dsi_clk.h"
#include "dsi_pwr.h"
#include "sde_dbg.h"
#include <linux/kobject.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include <linux/module.h>
#include <linux/init.h>
#include <drm/drm_mipi_dsi.h>
#include "oplus_dsi_support.h"
#include "oplus_display_esd.h"

#define DISPLAY_TOOL_CMD_KEYWORD "[display:sh]"

extern int oplus_dsi_log_type;
#define LOG_KEYWORD "LCD_DSI"
#define LCD_ERR(X, ...) pr_err("[%s] %s: "X, LOG_KEYWORD, __func__, ## __VA_ARGS__)
#define LCD_WARNING(X, ...) pr_warning("[%s] %s: "X, LOG_KEYWORD, __func__, ## __VA_ARGS__)
#define LCD_INFO(X, ...) pr_info("[%s] %s: "X, LOG_KEYWORD, __func__, ## __VA_ARGS__)
#define LCD_INFO_ONCE(X, ...) pr_info_once("[%s] %s: "X, LOG_KEYWORD, __func__, ## __VA_ARGS__)
#define LCD_DEBUG_BACKLIGHT(X, ...)	\
		do {	\
			if (oplus_dsi_log_type & OPLUS_DEBUG_LOG_BACKLIGHT)	\
				pr_info("[%s] %s: "X, LOG_KEYWORD, __func__, ## __VA_ARGS__);\
			else	\
				pr_debug("[%s] %s: "X, LOG_KEYWORD, __func__, ## __VA_ARGS__);\
		} while (0)
#define LCD_DEBUG_COMMON(X, ...)	\
		do {	\
			if (oplus_dsi_log_type & OPLUS_DEBUG_LOG_COMMON) \
				pr_info("[%s] %s: "X, LOG_KEYWORD, __func__, ## __VA_ARGS__);\
			else	\
				pr_debug("[%s] %s: "X, LOG_KEYWORD, __func__, ## __VA_ARGS__);\
		} while (0)

/**
 * enum oplus_debug_log --       flags to control debug log; 1->enbale  0->disable
 * @OPLUS_DEBUG_LOG_DISABLED:    disable all debug log
 * @OPLUS_DEBUG_LOG_CMD:         dump register log
 * @OPLUS_DEBUG_LOG_BACKLIGHT:   backlight log
 * @OPLUS_DEBUG_LOG_COMMON:      common log
 * @OPLUS_DEBUG_LOG_OFP:         OFP log
 * @OPLUS_DEBUG_LOG_ADFR:        ADFR log
 * @OPLUS_DEBUG_LOG_TEMP_COMPENSATION:temp compensation log
 */
enum oplus_debug_log {
	OPLUS_DEBUG_LOG_DISABLED = 0,
	OPLUS_DEBUG_LOG_CMD = BIT(0),
	OPLUS_DEBUG_LOG_BACKLIGHT = BIT(1),
	OPLUS_DEBUG_LOG_COMMON = BIT(2),
	OPLUS_DEBUG_LOG_OFP = BIT(3),
	OPLUS_DEBUG_LOG_ADFR = BIT(4),
	OPLUS_DEBUG_LOG_TEMP_COMPENSATION = BIT(6),
};

enum oplus_display_trace_enable {
	OPLUS_DISPLAY_DISABLE_TRACE = 0,
	OPLUS_DISPLAY_OFP_TRACE_ENABLE = BIT(0),
	OPLUS_DISPLAY_ADFR_TRACE_ENABLE = BIT(1),
	OPLUS_DISPLAY_TEMP_COMPENSATION_TRACE_ENABLE = BIT(3),
};

int oplus_set_display_vendor(struct dsi_display *display);

int oplus_dsi_update_spr_mode(void);

void oplus_panel_process_dimming_v2_post(struct dsi_panel *panel,
		bool force_disable);

int oplus_panel_process_dimming_v2(struct dsi_panel *panel, int bl_lvl,
		bool force_disable);

int oplus_panel_process_dimming_v3(struct dsi_panel *panel, int brightness);

int interpolate(int x, int xa, int xb, int ya, int yb);

int dsi_display_oplus_set_power(struct drm_connector *connector, int power_mode,
		void *disp);

int dsi_panel_read_panel_reg_unlock(struct dsi_display_ctrl *ctrl,
		struct dsi_panel *panel, u8 cmd, void *rbuf,  size_t len);

int dsi_display_read_panel_reg(struct dsi_display *display, u8 cmd, void *data,
		size_t len);

#endif /* _OPLUS_DISPLAY_PRIVATE_API_H_ */

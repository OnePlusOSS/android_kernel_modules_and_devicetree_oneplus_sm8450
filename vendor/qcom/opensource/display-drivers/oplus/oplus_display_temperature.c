/***************************************************************
** Copyright (C), 2022, OPLUS Mobile Comm Corp., Ltd
** File : oplus_display_temperature.c
** Description : oplus_display_temperature implement
** Version : 1.0
** Date : 2022/11/20
** Author : Display
***************************************************************/

#include "dsi_display.h"
#include "dsi_panel.h"
#include "dsi_defs.h"
#include "sde_trace.h"
#include "oplus_display_private_api.h"
#include "oplus_onscreenfingerprint.h"
#include "oplus_display_temperature.h"
#ifdef OPLUS_FEATURE_DISPLAY
#include "oplus_display_panel_common.h"
#endif
#include "oplus_adfr.h"
#include "oplus_dsi_support.h"
#include <linux/thermal.h>
#include <drm/drm_device.h>

#if defined(CONFIG_PXLW_IRIS)
#include "dsi_iris_api.h"
#endif

#define CHARGER_25C_VOLT	900
#define REGFLAG_CMD			0xFFFA

struct LCM_setting_table {
	unsigned int count;
	u8 *para_list;
};

extern int dsi_display_read_panel_reg(struct dsi_display *display, u8 cmd,
		void *data, size_t len);
extern int oplus_dsi_log_type;
unsigned int oplus_display_cmd_delay = 0;
struct oplus_display_temp *g_oplus_display_temp = NULL;
static bool calibrated = false;
static bool registered = false;

/* ntc_resistance:100k internal_pull_up:100k voltage:1.84v */
int con_temp_ntc_100k_1840mv[] = {
	-40, -39, -38, -37, -36, -35, -34, -33, -32, -31, -30, -29, -28, -27, -26, -25, -24, -23, -22,
	-21, -20, -19, -18, -17, -16, -15, -14, -13, -12, -11, -10, -9, -8, -7, -6, -5, -4, -3, -2, -1,
	0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25,
	26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49,
	50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73,
	74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97,
	98, 99, 100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116,
	117, 118, 119, 120, 121, 122, 123, 124, 125
};

int con_volt_ntc_100k_1840mv[] = {
	1799, 1796, 1793, 1790, 1786, 1782, 1778, 1774, 1770, 1765, 1760, 1755, 1749, 1743, 1737, 1731,
	1724, 1717, 1709, 1701, 1693, 1684, 1675, 1666, 1656, 1646, 1635, 1624, 1612, 1600, 1588, 1575,
	1561, 1547, 1533, 1518, 1503, 1478, 1471, 1454, 1437, 1420, 1402, 1384, 1365, 1346, 1327, 1307,
	1287, 1267, 1246, 1225, 1204, 1183, 1161, 1139, 1118, 1096, 1074, 1052, 1030, 1008, 986, 964,
	942, 920, 898, 877, 855, 834, 813, 793, 772, 752, 732, 712, 693, 674, 655, 637, 619, 601, 584,
	567, 550, 534, 518, 503, 488, 473, 459, 445, 431, 418, 405, 392, 380, 368, 357, 345, 335, 324,
	314, 304, 294, 285, 276, 267, 259, 251, 243, 235, 227, 220, 213, 206, 200, 194, 187, 182, 176,
	170, 165, 160, 155, 150, 145, 140, 136, 132, 128, 124, 120, 117, 113, 110, 106, 103, 100, 97,
	94, 91, 88, 86, 83, 81, 78, 76, 74, 72, 70, 68, 66, 64, 62, 60, 58, 57, 55, 54, 52, 51, 49, 48,
	47, 45
};

/* ntc_resistance:100k internal_pull_up:100k voltage:1.84v */
unsigned char temp_compensation_paras[11][11][25] = {
	/* dbv > 3515 */
	{
		{16, 20, 24, 16, 20, 16, 20, 24, 58, 58, 58, 58, 58, 58, 58, 58, 58, 0, 0, 0, 0, 0, 0, 0, 0}, /* -20 ~ -10 */
		{16, 20, 24, 16, 20, 16, 20, 24, 58, 58, 58, 58, 58, 58, 58, 58, 58, 0, 0, 0, 0, 0, 0, 0, 0}, /* -10 ~ 0 */
		{20, 24, 28, 20, 24, 20, 24, 28, 57, 57, 57, 57, 57, 57, 57, 57, 57, 0, 0, 0, 0, 0, 0, 0, 0}, /* 0 ~ 10 */
		{24, 28, 32, 24, 28, 24, 28, 32, 56, 56, 56, 56, 56, 56, 56, 56, 56, 0, 0, 0, 0, 0, 0, 0, 0}, /* 10 ~ 20 */
		{28, 32, 36, 28, 32, 28, 32, 36, 55, 55, 55, 55, 55, 55, 55, 55, 55, 0, 0, 0, 0, 0, 0, 0, 0}, /* 20 ~ 25 */
		{36, 40, 48, 36, 40, 36, 40, 48, 55, 55, 55, 55, 55, 55, 55, 55, 55, 0, 0, 0, 0, 0, 0, 0, 0}, /* 25 ~ 30 */
		{40, 44, 52, 40, 44, 40, 44, 52, 54, 54, 54, 54, 54, 54, 54, 54, 54, 0, 0, 0, 0, 0, 0, 0, 0}, /* 30 ~ 35 */
		{44, 48, 56, 44, 48, 44, 48, 56, 54, 54, 54, 54, 54, 54, 54, 54, 54, 0, 0, 0, 0, 0, 0, 0, 0}, /* 35 ~ 40 */
		{48, 52, 60, 48, 52, 48, 52, 60, 53, 53, 53, 53, 53, 53, 53, 53, 53, 0, 0, 0, 0, 0, 0, 0, 0}, /* 40 ~ 45 */
		{52, 56, 64, 52, 56, 52, 56, 64, 53, 53, 53, 53, 53, 53, 53, 53, 53, 0, 0, 0, 0, 0, 0, 0, 0}, /* 45 ~ 50 */
		{56, 60, 68, 56, 60, 56, 60, 68, 52, 52, 52, 52, 52, 52, 52, 52, 52, 0, 0, 0, 0, 0, 0, 0, 0}  /* > 50 */
	},

	/* 1604 <= dbv <= 3515 */
	{
		{16, 20, 24, 16, 20, 16, 20, 24, 29, 29, 38, 31, 31, 40, 30, 30, 39, 0, 0, 0, 0, 0, 0, 0, 0}, /* -20 ~ -10 */
		{16, 20, 24, 16, 20, 16, 20, 24, 29, 29, 38, 31, 31, 40, 29, 29, 38, 0, 0, 0, 0, 0, 0, 0, 0}, /* -10 ~ 0 */
		{20, 24, 28, 20, 24, 20, 24, 28, 28, 28, 37, 30, 30, 39, 28, 28, 37, 0, 0, 0, 0, 0, 0, 0, 0}, /* 0 ~ 10 */
		{24, 28, 32, 24, 28, 24, 28, 32, 28, 28, 37, 30, 30, 39, 27, 27, 36, 0, 0, 0, 0, 0, 0, 0, 0}, /* 10 ~ 20 */
		{28, 32, 36, 24, 28, 28, 32, 36, 27, 27, 36, 29, 29, 38, 27, 27, 36, 0, 0, 0, 0, 0, 0, 0, 0}, /* 20 ~ 25 */
		{36, 40, 48, 36, 40, 36, 40, 48, 27, 27, 36, 28, 28, 37, 27, 27, 36, 0, 0, 0, 0, 0, 0, 0, 0}, /* 25 ~ 30 */
		{40, 44, 52, 40, 44, 40, 44, 52, 26, 26, 35, 27, 27, 36, 26, 26, 35, 0, 0, 0, 0, 0, 0, 0, 0}, /* 30 ~ 35 */
		{44, 48, 56, 44, 48, 44, 48, 56, 26, 26, 35, 27, 27, 36, 26, 26, 35, 0, 0, 0, 0, 0, 0, 0, 0}, /* 35 ~ 40 */
		{48, 52, 60, 48, 52, 48, 52, 60, 25, 25, 34, 26, 26, 35, 25, 25, 34, 0, 0, 0, 0, 0, 0, 0, 0}, /* 40 ~ 45 */
		{52, 56, 64, 52, 56, 52, 56, 64, 25, 25, 34, 26, 26, 35, 25, 25, 34, 0, 0, 0, 0, 0, 0, 0, 0}, /* 45 ~ 50 */
		{56, 60, 68, 56, 60, 56, 60, 68, 24, 24, 33, 25, 25, 34, 24, 24, 33, 0, 0, 0, 0, 0, 0, 0, 0}  /* > 50 */
	},

	/* 1511 <= dbv < 1604 */
	{
		{12, 16, 20, 12, 16,  4,  4,  8, 29, 29, 38, 31, 31, 40, 25, 25, 36, 0, 0, 0, 0, 0, 0, 0, 0}, /* -20 ~ -10 */
		{12, 16, 20, 12, 16,  4,  4,  8, 29, 29, 38, 31, 31, 40, 25, 25, 36, 0, 0, 0, 0, 0, 0, 0, 0}, /* -10 ~ 0 */
		{16, 20, 24, 16, 20,  4,  8, 12, 28, 28, 37, 30, 30, 39, 25, 25, 36, 0, 0, 0, 0, 0, 0, 0, 0}, /* 0 ~ 10 */
		{20, 24, 28, 20, 24,  4,  8, 12, 28, 28, 37, 30, 30, 39, 25, 25, 36, 0, 0, 0, 0, 0, 0, 0, 0}, /* 10 ~ 20 */
		{24, 28, 32, 20, 24,  8,  8, 12, 27, 27, 36, 29, 29, 38, 25, 25, 36, 0, 0, 0, 0, 0, 0, 0, 0}, /* 20 ~ 25 */
		{32, 36, 44, 32, 36,  8, 12, 16, 27, 27, 36, 28, 28, 37, 25, 25, 36, 0, 0, 0, 0, 0, 0, 0, 0}, /* 25 ~ 30 */
		{36, 40, 48, 36, 40,  8, 16, 20, 26, 26, 35, 27, 27, 36, 24, 24, 36, 0, 0, 0, 0, 0, 0, 0, 0}, /* 30 ~ 35 */
		{40, 44, 52, 40, 44,  8, 16, 20, 26, 26, 35, 27, 27, 36, 24, 24, 36, 0, 0, 0, 0, 0, 0, 0, 0}, /* 35 ~ 40 */
		{44, 48, 56, 44, 48,  8, 20, 24, 25, 25, 34, 26, 26, 35, 24, 24, 36, 0, 0, 0, 0, 0, 0, 0, 0}, /* 40 ~ 45 */
		{48, 52, 60, 48, 52, 12, 20, 24, 25, 25, 34, 26, 26, 35, 24, 24, 36, 0, 0, 0, 0, 0, 0, 0, 0}, /* 45 ~ 50 */
		{52, 56, 64, 52, 56, 12, 20, 24, 24, 24, 33, 25, 25, 34, 24, 24, 36, 0, 0, 0, 0, 0, 0, 0, 0}  /* > 50 */
	},

	/* 1419 <= dbv < 1511 */
	{
		{12, 12, 16, 12, 16, 4,  4,  8, 29, 29, 38, 31, 31, 40, 25, 25, 36, 0, 0, 0, 0, 0, 0, 0, 0}, /* -20 ~ -10 */
		{12, 12, 16, 12, 16, 4,  4,  8, 29, 29, 38, 31, 31, 40, 25, 25, 36, 0, 0, 0, 0, 0, 0, 0, 0}, /* -10 ~ 0 */
		{16, 20, 24, 16, 20, 4,  8, 12, 28, 28, 37, 30, 30, 39, 25, 25, 36, 0, 0, 0, 0, 0, 0, 0, 0}, /* 0 ~ 10 */
		{20, 24, 28, 20, 24, 4,  8, 12, 28, 28, 37, 30, 30, 39, 25, 25, 36, 0, 0, 0, 0, 0, 0, 0, 0}, /* 10 ~ 20 */
		{24, 28, 32, 20, 24, 8,  8, 12, 27, 27, 36, 29, 29, 38, 25, 25, 36, 0, 0, 0, 0, 0, 0, 0, 0}, /* 20 ~ 25 */
		{28, 32, 40, 28, 32, 8, 12, 12, 27, 27, 36, 28, 28, 37, 25, 25, 36, 0, 0, 0, 0, 0, 0, 0, 0}, /* 25 ~ 30 */
		{32, 36, 44, 32, 36, 8, 12, 16, 26, 26, 35, 27, 27, 36, 24, 24, 36, 0, 0, 0, 0, 0, 0, 0, 0}, /* 30 ~ 35 */
		{36, 40, 48, 36, 40, 8, 12, 16, 26, 26, 35, 27, 27, 36, 24, 24, 36, 0, 0, 0, 0, 0, 0, 0, 0}, /* 35 ~ 40 */
		{40, 44, 52, 40, 44, 8, 16, 20, 25, 25, 34, 26, 26, 35, 24, 24, 36, 0, 0, 0, 0, 0, 0, 0, 0}, /* 40 ~ 45 */
		{44, 48, 56, 44, 48, 8, 16, 20, 25, 25, 34, 26, 26, 35, 24, 24, 36, 0, 0, 0, 0, 0, 0, 0, 0}, /* 45 ~ 50 */
		{48, 52, 60, 48, 52, 8, 16, 20, 24, 24, 33, 25, 25, 34, 24, 24, 36, 0, 0, 0, 0, 0, 0, 0, 0}  /* > 50 */
	},

	/* 1328 <= dbv < 1419 */
	{
		{12, 12, 16,  8, 12, 4,  4,  8, 29, 29, 38, 31, 31, 40, 25, 25, 36, 0, 0, 0, 0, 0, 0, 0, 0}, /* -20 ~ -10 */
		{12, 12, 16,  8, 12, 4,  4,  8, 29, 29, 38, 31, 31, 40, 25, 25, 36, 0, 0, 0, 0, 0, 0, 0, 0}, /* -10 ~ 0 */
		{16, 20, 24, 12, 16, 4,  4, 12, 28, 28, 37, 30, 30, 39, 25, 25, 36, 0, 0, 0, 0, 0, 0, 0, 0}, /* 0 ~ 10 */
		{16, 20, 24, 16, 20, 4,  8, 12, 28, 28, 37, 30, 30, 39, 25, 25, 36, 0, 0, 0, 0, 0, 0, 0, 0}, /* 10 ~ 20 */
		{20, 24, 28, 16, 20, 4,  8, 12, 27, 27, 36, 29, 29, 38, 25, 25, 36, 0, 0, 0, 0, 0, 0, 0, 0}, /* 20 ~ 25 */
		{24, 28, 36, 24, 28, 4,  8, 12, 27, 27, 36, 28, 28, 37, 25, 25, 36, 0, 0, 0, 0, 0, 0, 0, 0}, /* 25 ~ 30 */
		{28, 32, 40, 28, 32, 8,  8, 12, 26, 26, 35, 27, 27, 36, 24, 24, 36, 0, 0, 0, 0, 0, 0, 0, 0}, /* 30 ~ 35 */
		{32, 36, 44, 32, 36, 8, 12, 16, 26, 26, 35, 27, 27, 36, 24, 24, 36, 0, 0, 0, 0, 0, 0, 0, 0}, /* 35 ~ 40 */
		{36, 40, 48, 36, 40, 8, 12, 16, 25, 25, 34, 26, 26, 35, 24, 24, 36, 0, 0, 0, 0, 0, 0, 0, 0}, /* 40 ~ 45 */
		{40, 44, 52, 40, 44, 8, 16, 20, 25, 25, 34, 26, 26, 35, 24, 24, 36, 0, 0, 0, 0, 0, 0, 0, 0}, /* 45 ~ 50 */
		{44, 48, 56, 44, 48, 8, 16, 20, 24, 24, 33, 25, 25, 34, 24, 24, 36, 0, 0, 0, 0, 0, 0, 0, 0}  /* > 50 */
	},

	/* 1212 <= dbv < 1328 */
	{
		{ 8, 12, 16,  8, 12, 4,  4,  8, 30, 30, 39, 30, 30, 39, 28, 28, 36, 0, 0, 0, 0, 0, 0, 0, 0}, /* -20 ~ -10 */
		{12, 12, 16,  8, 12, 4,  4,  8, 29, 29, 38, 30, 30, 39, 27, 27, 36, 0, 0, 0, 0, 0, 0, 0, 0}, /* -10 ~ 0 */
		{12, 12, 24,  8, 12, 4,  4, 12, 29, 29, 38, 30, 30, 39, 27, 27, 36, 0, 0, 0, 0, 0, 0, 0, 0}, /* 0 ~ 10 */
		{12, 16, 24, 12, 16, 4,  4, 12, 28, 28, 37, 29, 29, 38, 27, 27, 36, 0, 0, 0, 0, 0, 0, 0, 0}, /* 10 ~ 20 */
		{16, 20, 24, 12, 16, 4,  8, 12, 27, 27, 36, 28, 28, 37, 26, 26, 36, 0, 0, 0, 0, 0, 0, 0, 0}, /* 20 ~ 25 */
		{20, 24, 32, 20, 24, 4,  8, 12, 27, 27, 36, 28, 28, 37, 25, 25, 36, 0, 0, 0, 0, 0, 0, 0, 0}, /* 25 ~ 30 */
		{24, 28, 36, 24, 28, 4, 12, 20, 26, 26, 35, 27, 27, 36, 24, 24, 36, 0, 0, 0, 0, 0, 0, 0, 0}, /* 30 ~ 35 */
		{28, 32, 40, 28, 32, 4, 12, 20, 26, 26, 35, 27, 27, 36, 24, 24, 36, 0, 0, 0, 0, 0, 0, 0, 0}, /* 35 ~ 40 */
		{32, 36, 44, 32, 36, 8,  8, 16, 25, 25, 34, 26, 26, 35, 24, 24, 36, 0, 0, 0, 0, 0, 0, 0, 0}, /* 40 ~ 45 */
		{36, 40, 48, 36, 40, 8,  8, 16, 25, 25, 34, 26, 26, 35, 24, 24, 36, 0, 0, 0, 0, 0, 0, 0, 0}, /* 45 ~ 50 */
		{40, 44, 52, 40, 44, 8,  8, 16, 24, 24, 33, 25, 25, 34, 24, 24, 36, 0, 0, 0, 0, 0, 0, 0, 0}  /* > 50 */
	},

	/* 1096 <= dbv < 1212 */
	{
		{ 8, 16, 24, 16, 24, 4, 4, 8, 27, 27, 38, 28, 28, 39, 24, 24, 38, 0, 138, 138, 138, 0, 0, 0, 0}, /* -20 ~ -10 */
		{12, 20, 24, 16, 24, 4, 4, 8, 27, 27, 38, 28, 28, 39, 24, 24, 38, 0,   0,   0,   0, 0, 0, 0, 0}, /* -10 ~ 0 */
		{12, 24, 24, 16, 24, 4, 4, 8, 27, 27, 38, 28, 28, 39, 24, 24, 38, 0,   0,   0,   0, 0, 0, 0, 0}, /* 0 ~ 10 */
		{16, 24, 24, 16, 24, 4, 4, 8, 27, 27, 38, 28, 28, 39, 24, 24, 38, 0,   0,   0,   0, 0, 0, 0, 0}, /* 10 ~ 20 */
		{16, 24, 24, 16, 24, 4, 4, 8, 27, 27, 38, 28, 28, 39, 24, 24, 38, 0,   0,   0,   0, 0, 0, 0, 0}, /* 20 ~ 25 */
		{16, 24, 24, 16, 24, 4, 4, 8, 27, 27, 38, 28, 28, 39, 24, 24, 38, 0,  17,  17,  17, 0, 0, 0, 0}, /* 25 ~ 30 */
		{20, 24, 24, 16, 24, 4, 4, 8, 27, 27, 38, 28, 28, 39, 24, 24, 38, 0,   0,   0,   0, 0, 0, 0, 0}, /* 30 ~ 35 */
		{20, 24, 24, 16, 24, 4, 4, 8, 27, 27, 38, 28, 28, 39, 24, 24, 38, 0,   0,   0,   0, 0, 0, 0, 0}, /* 35 ~ 40 */
		{24, 24, 24, 16, 24, 4, 4, 8, 27, 27, 38, 28, 28, 39, 24, 24, 38, 0,   0,   0,   0, 0, 0, 0, 0}, /* 40 ~ 45 */
		{28, 24, 24, 16, 24, 4, 4, 8, 27, 27, 38, 28, 28, 39, 24, 24, 38, 0,   0,   0,   0, 0, 0, 0, 0}, /* 45 ~ 50 */
		{28, 24, 24, 16, 24, 4, 4, 8, 26, 26, 38, 28, 28, 39, 24, 24, 38, 0,  86,  86,  86, 0, 0, 0, 0}  /* > 50 */
	},

	/* 950 <= dbv < 1096 */
	{
		{ 8, 16, 16, 12, 16, 4, 4, 4, 27, 27, 36, 27, 27, 37, 23, 23, 36, 0, 225, 225, 225, 0, 0, 0, 0}, /* -20 ~ -10 */
		{ 8, 16, 16, 12, 16, 4, 4, 4, 27, 27, 36, 27, 27, 37, 23, 23, 36, 0,   0,   0,   0, 0, 0, 0, 0}, /* -10 ~ 0 */
		{ 8, 16, 16, 12, 16, 4, 4, 4, 27, 27, 36, 27, 27, 37, 23, 23, 36, 0,   0,   0,   0, 0, 0, 0, 0}, /* 0 ~ 10 */
		{12, 16, 16, 12, 16, 4, 4, 4, 27, 27, 36, 27, 27, 37, 23, 23, 36, 0,   0,   0,   0, 0, 0, 0, 0}, /* 10 ~ 20 */
		{12, 16, 16, 12, 16, 4, 4, 4, 27, 27, 36, 27, 27, 37, 23, 23, 36, 0,   0,   0,   0, 0, 0, 0, 0}, /* 20 ~ 25 */
		{12, 16, 16, 12, 16, 4, 4, 4, 27, 27, 36, 27, 27, 37, 23, 23, 36, 0,  17,  17,  17, 0, 0, 0, 0}, /* 25 ~ 30 */
		{16, 16, 16, 12, 16, 4, 4, 4, 27, 27, 36, 27, 27, 37, 23, 23, 36, 0,  17,  17,  17, 0, 0, 0, 0}, /* 30 ~ 35 */
		{16, 16, 16, 12, 16, 4, 4, 4, 27, 27, 36, 27, 27, 37, 23, 23, 36, 0,   0,   0,   0, 0, 0, 0, 0}, /* 35 ~ 40 */
		{20, 16, 16, 12, 16, 4, 4, 4, 27, 27, 36, 27, 27, 37, 23, 23, 36, 0,   0,   0,   0, 0, 0, 0, 0}, /* 40 ~ 45 */
		{20, 16, 16, 12, 16, 4, 4, 4, 27, 27, 36, 27, 27, 37, 23, 23, 36, 0,   0,   0,   0, 0, 0, 0, 0}, /* 45 ~ 50 */
		{20, 16, 16, 12, 16, 4, 4, 4, 25, 25, 36, 27, 27, 37, 23, 23, 36, 0, 190, 190, 190, 0, 0, 0, 0}  /* > 50 */
	},

	/* 761 <= dbv < 950 */
	{
		{ 4, 16, 16, 8, 16, 4, 4, 4, 27, 27, 36, 27, 27, 37, 22, 22, 36, 0, 225, 225, 225, 0, 0, 0, 0}, /* -20 ~ -10 */
		{ 8, 16, 16, 8, 16, 4, 4, 4, 27, 27, 36, 27, 27, 37, 22, 22, 36, 0,   0,   0,   0, 0, 0, 0, 0}, /* -10 ~ 0 */
		{ 8, 16, 16, 8, 16, 4, 4, 4, 27, 27, 36, 27, 27, 37, 22, 22, 36, 0,   0,   0,   0, 0, 0, 0, 0}, /* 0 ~ 10 */
		{ 8, 16, 16, 8, 16, 4, 4, 4, 27, 27, 36, 27, 27, 37, 22, 22, 36, 0,   0,   0,   0, 0, 0, 0, 0}, /* 10 ~ 20 */
		{ 8, 16, 16, 8, 16, 4, 4, 4, 27, 27, 36, 27, 27, 37, 22, 22, 36, 0,   0,   0,   0, 0, 0, 0, 0}, /* 20 ~ 25 */
		{ 8, 16, 16, 8, 16, 4, 4, 4, 27, 27, 36, 27, 27, 37, 22, 22, 36, 0,  86,  86,  86, 0, 0, 0, 0}, /* 25 ~ 30 */
		{12, 16, 16, 8, 16, 4, 4, 4, 27, 27, 36, 27, 27, 37, 22, 22, 36, 0,  34,  34,  34, 0, 0, 0, 0}, /* 30 ~ 35 */
		{12, 16, 16, 8, 16, 4, 4, 4, 27, 27, 36, 27, 27, 37, 22, 22, 36, 0,   0,   0,   0, 0, 0, 0, 0}, /* 35 ~ 40 */
		{16, 16, 16, 8, 16, 4, 4, 4, 27, 27, 36, 27, 27, 37, 22, 22, 36, 0,   0,   0,   0, 0, 0, 0, 0}, /* 40 ~ 45 */
		{16, 16, 16, 8, 16, 4, 4, 4, 27, 27, 36, 27, 27, 37, 22, 22, 36, 0,   0,   0,   0, 0, 0, 0, 0}, /* 45 ~ 50 */
		{16, 16, 16, 8, 16, 4, 4, 4, 25, 25, 36, 27, 27, 37, 22, 22, 36, 0, 190, 190, 190, 0, 0, 0, 0}  /* > 50 */
	},

	/* 544 <= dbv < 761 */
	{
		{4, 8, 12, 8, 12, 0, 0, 0, 27, 27, 36, 26, 26, 37, 21, 21, 36,  0, 242, 242, 242, 0, 0, 0, 0}, /* -20 ~ -10 */
		{4, 8, 12, 8, 12, 0, 0, 0, 27, 27, 36, 26, 26, 37, 21, 21, 36,  0, 190, 190, 190, 0, 0, 0, 0}, /* -10 ~ 0 */
		{4, 8, 12, 8, 12, 0, 0, 0, 27, 27, 35, 26, 26, 37, 21, 21, 36,  0, 104, 104, 104, 0, 0, 0, 0}, /* 0 ~ 10 */
		{4, 8, 12, 8, 12, 0, 0, 0, 27, 27, 36, 26, 26, 37, 21, 21, 36,  0,  52,  52,  52, 0, 0, 0, 0}, /* 10 ~ 20 */
		{8, 8, 12, 8, 12, 0, 0, 0, 27, 27, 36, 26, 26, 37, 21, 21, 36,  0,  52,  52,  52, 0, 0, 0, 0}, /* 20 ~ 25 */
		{8, 8, 12, 8, 12, 0, 0, 0, 27, 27, 36, 26, 26, 37, 21, 21, 36,  0, 104, 104, 104, 0, 0, 0, 0}, /* 25 ~ 30 */
		{8, 8, 12, 8, 12, 0, 0, 0, 27, 27, 36, 26, 26, 37, 21, 21, 36,  0,  17,  17,  17, 0, 0, 0, 0}, /* 30 ~ 35 */
		{8, 8, 12, 8, 12, 0, 0, 0, 27, 27, 36, 26, 26, 37, 21, 21, 36,  0,  17,  17,  17, 0, 0, 0, 0}, /* 35 ~ 40 */
		{8, 8, 12, 8, 12, 0, 0, 0, 26, 26, 36, 26, 26, 37, 21, 21, 36,  0,  52,  52,  52, 0, 0, 0, 0}, /* 40 ~ 45 */
		{8, 8, 12, 8, 12, 0, 0, 0, 27, 27, 36, 26, 26, 37, 21, 21, 36,  0,   0,   0,   0, 0, 0, 0, 0}, /* 45 ~ 50 */
		{8, 8, 12, 8, 12, 0, 0, 0, 24, 24, 36, 26, 26, 37, 21, 21, 36,  0, 190, 190, 190, 0, 0, 0, 0}  /* > 50 */
	},

	/* dbv < 544 */
	{
		{0, 8, 12, 8, 12, 0, 0, 0, 26, 26, 35, 27, 27, 37, 20, 20, 36, 21,  56,  56,  56, 0, 0, 0, 0}, /* -20 ~ -10 */
		{4, 8, 12, 8, 12, 0, 0, 0, 26, 26, 35, 27, 27, 37, 20, 20, 36, 21,  21,  21,  21, 0, 0, 0, 0}, /* -10 ~ 0 */
		{4, 8, 12, 8, 12, 0, 0, 0, 26, 26, 35, 27, 27, 37, 20, 20, 36,  0, 208, 208, 208, 0, 0, 0, 0}, /* 0 ~ 10 */
		{4, 8, 12, 8, 12, 0, 0, 0, 26, 26, 35, 27, 27, 37, 20, 20, 36,  0, 242, 242, 242, 0, 0, 0, 0}, /* 10 ~ 20 */
		{4, 8, 12, 8, 12, 0, 0, 0, 26, 26, 35, 27, 27, 37, 20, 20, 36,  0, 173, 173, 173, 0, 0, 0, 0}, /* 20 ~ 25 */
		{4, 8, 12, 8, 12, 0, 0, 0, 26, 26, 35, 27, 27, 37, 20, 20, 36,  0, 173, 173, 173, 0, 0, 0, 0}, /* 25 ~ 30 */
		{4, 8, 12, 8, 12, 0, 0, 0, 25, 25, 34, 27, 27, 37, 20, 20, 36,  0, 190, 190, 190, 0, 0, 0, 0}, /* 30 ~ 35 */
		{4, 8, 12, 8, 12, 0, 0, 0, 24, 24, 33, 27, 27, 37, 20, 20, 36, 21,   4,   4,   4, 0, 0, 0, 0}, /* 35 ~ 40 */
		{4, 8, 12, 8, 12, 0, 0, 0, 24, 24, 33, 27, 27, 37, 20, 20, 36,  0, 225, 225, 225, 0, 0, 0, 0}, /* 40 ~ 45 */
		{4, 8, 12, 8, 12, 0, 0, 0, 24, 24, 33, 27, 27, 37, 20, 20, 36,  0, 225, 225, 225, 0, 0, 0, 0}, /* 45 ~ 50 */
		{4, 8, 12, 8, 12, 0, 0, 0, 24, 24, 33, 27, 27, 37, 20, 20, 36,  0, 225, 225, 225, 0, 0, 0, 0}  /* > 50 */
	}
};

int oplus_display_change_compensation_params_ref_reg(void *dsi_display) {
	int i = 0;
	int j = 0;
	int k = 0;
	int rc = 0;
	int value1;
	int value2;
	int delta1 = 0;
	int delta2 = 0;
	unsigned char buf[10];
	struct dsi_display *display = dsi_display;

	if (calibrated) {
		return 0;
	}

	if (display == NULL) {
		DSI_ERR("[DISP][ERR] Invalid display pointer\n");
		return 0;
	}

	if (display->panel == NULL) {
		DSI_ERR("[DISP][ERR] Invalid panel pointer\n");
		return 0;
	}

	if (strcmp(display->panel->oplus_priv.vendor_name, "NT37705")) {
		pr_debug("[DISP][INFO][%s:%d]isn't NT37705 panel\n", __func__, __LINE__);
		return 0;
	}

	rc = oplus_display_tx_cmd_set_lock(display, DSI_CMD_READ_COMPENSATION_REG1);
	if (rc) {
		DSI_ERR("[DISP][ERR] failed to send DSI_CMD_READ_COMPENSATION_REG1 cmds rc=%d\n", rc);
		return 0;
	}
	rc = dsi_display_read_panel_reg(display, 0xE0, buf, 10);
	if (rc) {
		DSI_ERR("[DISP][ERR] failed to read value1 rc=%d\n", rc);
		return 0;
	}
	value1 = buf[0];
	pr_info("[DISP][INFO][%s:%d] value1 = %d\n", __func__, __LINE__, value1);
	if (value1 < 25 || value1 > 29) {
		pr_info("[DISP][INFO][%s:%d] value1 Invalid\n", __func__, __LINE__);
		return 0;
	}

	value2 = buf[6];
	pr_info("[DISP][INFO][%s:%d] value2 = %d\n", __func__, __LINE__, value2);
	if (value2 < 26 || value2 > 30) {
		pr_info("[DISP][INFO][%s:%d] value2 Invalid\n", __func__, __LINE__);
		return 0;
	}

	delta1 = value1 - 0x1B;
	delta2 = value2 - 0x1C;
	pr_info("[DISP][INFO][%s:%d] delta1 = %d, delta2 = %d\n", __func__, __LINE__, delta1, delta2);

	for (i = OPLUS_DISPLAY_1511_1604_DBV_INDEX; i <= OPLUS_DISPLAY_1212_1328_DBV_INDEX; i++) {
		for (j = OPLUS_DISPLAY_LESS_THAN_MINUS10_TEMP_INDEX; j <= OPLUS_DISPLAY_GREATER_THAN_50_TEMP_INDEX; j++) {
			for (k = 11; k <= 13; k++) {
				temp_compensation_paras[i][j][k] += delta2;
			}
			for (k = 14; k <= 16; k++) {
				temp_compensation_paras[i][j][k] += delta1;
			}
		}
	}

	i = OPLUS_DISPLAY_1604_3515_DBV_INDEX;
	for (j = OPLUS_DISPLAY_LESS_THAN_MINUS10_TEMP_INDEX; j <= OPLUS_DISPLAY_GREATER_THAN_50_TEMP_INDEX; j++) {
		for (k = 8; k <= 10; k++) {
			temp_compensation_paras[i][j][k] += delta1;
		}
	}

	calibrated = true;
	return 0;
}
EXPORT_SYMBOL(oplus_display_change_compensation_params_ref_reg);

int oplus_display_register_ntc_channel(void *dsi_display)
{
	struct dsi_display *display = dsi_display;
	struct device *dev = NULL;
	struct dsi_panel *panel = NULL;
	int i, rc = 0;

	pr_debug("[DISP][DEBUG][%s:%d]start\n", __func__, __LINE__);

	if (registered)
		goto end;

	dev = &display->pdev->dev;
	if (IS_ERR_OR_NULL(dev)) {
		pr_info("[DISP][ERR][%s:%d]Invalid params\n", __func__, __LINE__);
		goto end;
	}

	panel = display->panel;
	if (IS_ERR_OR_NULL(panel)) {
		pr_info("[DISP][INFO][%s:%d]Invalid params\n", __func__, __LINE__);
		goto end;
	}

#if defined(CONFIG_PXLW_IRIS)
	if (iris_is_chip_supported() && (!strcmp(panel->type, "secondary"))) {
		pr_info("[DISP][INFO][%s:%d]no need to init secondary panel for iris chip\n", __func__, __LINE__);
		goto end;
	}
#endif

	g_oplus_display_temp = devm_kzalloc(dev, sizeof(struct oplus_display_temp), GFP_KERNEL);
	if (IS_ERR_OR_NULL(g_oplus_display_temp)) {
		pr_err("[DISP][ERR][%s:%d]failed to kzalloc g_oplus_display_temp\n", __func__, __LINE__);
		goto end;
	}

	for (i = 0; i < 5; i++) {
		g_oplus_display_temp->ntc_temp_chan = iio_channel_get(dev, "disp0_con_therm_adc");
		rc = IS_ERR_OR_NULL(g_oplus_display_temp->ntc_temp_chan);
		if (rc) {
			pr_err("[DISP][ERR][%s:%d]failed to get panel channel\n", __func__, __LINE__);
		} else {
			registered = true;
			break;
		}
	}

	if (rc) {
		devm_kfree(dev, g_oplus_display_temp);
	} else {
		pr_info("[DISP][INFO][%s:%d]register ntc channel successfully\n", __func__, __LINE__);
		g_oplus_display_temp->ntc_temp = 29;
		g_oplus_display_temp->shell_temp = 29;
		g_oplus_display_temp->fake_ntc_temp = false;
		g_oplus_display_temp->fake_shell_temp = false;
		g_oplus_display_temp->cmd_delay = 0;
		g_oplus_display_temp->compensation_enable = true;
		g_oplus_display_temp->oplus_compensation_set_wq = create_singlethread_workqueue("oplus_compensation_set");
		INIT_WORK(&g_oplus_display_temp->oplus_compensation_set_work, oplus_compensation_set_work_handler);
	}

end:
	pr_debug("[DISP][DEBUG][%s:%d]end\n", __func__, __LINE__);

	return 0;
}
EXPORT_SYMBOL(oplus_display_register_ntc_channel);

int oplus_display_volt_to_temp(int volt)
{
	int i, volt_avg;

	pr_debug("[DISP][DEBUG][%s:%d]start\n", __func__, __LINE__);

	for (i = 0; i < ARRAY_SIZE(con_temp_ntc_100k_1840mv) - 1; i++) {
		if ((volt >= con_volt_ntc_100k_1840mv[i + 1])
				&& (volt <= con_volt_ntc_100k_1840mv[i])) {
			volt_avg = (con_volt_ntc_100k_1840mv[i + 1] + con_volt_ntc_100k_1840mv[i]) / 2;
			if(volt <= volt_avg)
				i++;
			break;
		}
	}

	pr_debug("[DISP][DEBUG][%s:%d]end\n", __func__, __LINE__);

	return con_temp_ntc_100k_1840mv[i];
}

int oplus_display_get_ntc_temp(void)
{
	int i;
	int timeout = 0;
	int rc;
	int val_avg;
	int val[3] = {0, 0, 0};

	pr_debug("[DISP][DEBUG][%s:%d]start\n", __func__, __LINE__);

	if (IS_ERR_OR_NULL(g_oplus_display_temp)) {
		pr_info("[DISP][INFO][%s:%d]Invalid params\n", __func__, __LINE__);
		return -EINVAL;
	}

	if (IS_ERR_OR_NULL(g_oplus_display_temp->ntc_temp_chan)) {
		pr_info("[DISP][INFO][%s:%d]Invalid params\n", __func__, __LINE__);
		g_oplus_display_temp->ntc_temp = 29;
		return -EINVAL;
	}

	if (g_oplus_display_temp->fake_ntc_temp)
		return g_oplus_display_temp->ntc_temp;

read:
	for (i = 0; i < 3; i++) {
		rc = iio_read_channel_processed(g_oplus_display_temp->ntc_temp_chan, &val[i]);
		if (oplus_dsi_log_type & OPLUS_DEBUG_LOG_COMPENSATION) {
			pr_info("[DISP][INFO][%s:%d] ntc_temp = %d\n", __func__, __LINE__, val[i]);
		}
		if (rc < 0) {
			pr_err("[DISP][INFO][%s:%d]read ntc_temp_chan volt failed, rc=%d\n", __func__, __LINE__, rc);
		}
	}

	if (timeout >= 5)
		return g_oplus_display_temp->ntc_temp;

	if ((abs(val[0] - val[1]) >= 2000)
			|| (abs(val[0] - val[2]) >= 2000)
			|| (abs(val[1] - val[2]) >= 2000)) {
		pr_info("[DISP][INFO][%s:%d] big difference, timeout = %d\n", __func__, __LINE__, timeout);
		timeout++;
		goto read;
	} else
		val_avg = (val[0] + val[1] + val[2]) / 3;

/*
	pr_debug("[DISP][DEBUG][%s:%d]panel ntc voltage is %d\n", __func__, __LINE__, val);
	if (val <= 0) {
		val = CHARGER_25C_VOLT;
	}
*/
	if (oplus_dsi_log_type & OPLUS_DEBUG_LOG_COMPENSATION) {
		pr_info("[DISP][INFO][%s:%d] intact ntc_temp is %d\n", __func__, __LINE__, val_avg);
	}
	g_oplus_display_temp->ntc_temp = val_avg/ 1000;

	pr_debug("[DISP][DEBUG][%s:%d]panel ntc temp is %d\n", __func__, __LINE__, g_oplus_display_temp->ntc_temp);
	pr_debug("[DISP][DEBUG][%s:%d]end\n", __func__, __LINE__);

	return g_oplus_display_temp->ntc_temp;
}
EXPORT_SYMBOL(oplus_display_get_ntc_temp);

int oplus_display_get_shell_temp(void)
{
	int i = 0;
	int temp = -127000;
	int max_temp = -127000;
	const char *shell_tz[] = {"shell_front", "shell_frame", "shell_back"};
	struct thermal_zone_device *tz = NULL;

	pr_debug("[DISP][DEBUG][%s:%d]start\n", __func__, __LINE__);

	if (IS_ERR_OR_NULL(g_oplus_display_temp)) {
		pr_info("[DISP][INFO][%s:%d]Invalid params\n", __func__, __LINE__);
		return -EINVAL;
	}

	if (g_oplus_display_temp->fake_shell_temp)
		return g_oplus_display_temp->shell_temp;

	for (i = 0; i < ARRAY_SIZE(shell_tz); i++) {
		tz = thermal_zone_get_zone_by_name(shell_tz[i]);
		thermal_zone_get_temp(tz, &temp);
		if (max_temp < temp) {
			max_temp = temp;
		}
	}

	g_oplus_display_temp->shell_temp = max_temp / 1000;

	pr_debug("[DISP][DEBUG][%s:%d]shell temp is %d\n", __func__, __LINE__, g_oplus_display_temp->shell_temp);
	pr_debug("[DISP][DEBUG][%s:%d]end\n", __func__, __LINE__);

	return g_oplus_display_temp->shell_temp;
}
EXPORT_SYMBOL(oplus_display_get_shell_temp);

void vpark_set(unsigned char voltage, struct LCM_setting_table *temp_compensation_cmd)
{
	uint8_t voltage1, voltage2, voltage3, voltage4;
	unsigned short vpark = (69 - voltage) * 1024 / (69 - 10);

	pr_debug("[DISP][DEBUG][%s:%d]start\n", __func__, __LINE__);

	voltage1 = ((vpark & 0xFF00) >> 8) + ((vpark & 0xFF00) >> 6) + ((vpark & 0xFF00) >> 4);
	voltage2 = vpark & 0xFF;
	voltage3 = vpark & 0xFF;
	voltage4 = vpark & 0xFF;
	temp_compensation_cmd[15].para_list[0+1] = voltage1;
	temp_compensation_cmd[15].para_list[1+1] = voltage2;
	temp_compensation_cmd[15].para_list[2+1] = voltage3;
	temp_compensation_cmd[15].para_list[3+1] = voltage4;

	pr_debug("[DISP][DEBUG][%s:%d]end\n", __func__, __LINE__);
}

static unsigned int oplus_display_get_temp_index(int temp)
{
	unsigned int temp_index = 0;

	pr_debug("[DISP][DEBUG][%s:%d]start\n", __func__, __LINE__);

	if (temp < -10) {
		temp_index = OPLUS_DISPLAY_LESS_THAN_MINUS10_TEMP_INDEX;
	} else if (temp < 0) {
		temp_index = OPLUS_DISPLAY_MINUS10_0_TEMP_INDEX;
	} else if (temp < 10) {
		temp_index = OPLUS_DISPLAY_0_10_TEMP_INDEX;
	} else if (temp < 20) {
		temp_index = OPLUS_DISPLAY_10_20_TEMP_INDEX;
	} else if (temp < 25) {
		temp_index = OPLUS_DISPLAY_20_25_TEMP_INDEX;
	} else if (temp < 30) {
		temp_index = OPLUS_DISPLAY_25_30_TEMP_INDEX;
	} else if (temp < 35) {
		temp_index = OPLUS_DISPLAY_30_35_TEMP_INDEX;
	} else if (temp < 40) {
		temp_index = OPLUS_DISPLAY_35_40_TEMP_INDEX;
	} else if (temp < 45) {
		temp_index = OPLUS_DISPLAY_40_45_TEMP_INDEX;
	} else if (temp <= 50) {
		temp_index = OPLUS_DISPLAY_45_50_TEMP_INDEX;
	} else {
		temp_index = OPLUS_DISPLAY_GREATER_THAN_50_TEMP_INDEX;
	}

	pr_debug("[DISP][DEBUG][%s:%d]temp_index:%d\n", __func__, __LINE__, temp_index);
	pr_debug("[DISP][DEBUG][%s:%d]end\n", __func__, __LINE__);

	return temp_index;
}

int oplus_display_temp_compensation_set(void *display_panel, bool force_set)
{
	int i = 0;
	int j = 0;
	int rc = 0;
	int ntc_temp = 0;
	int a_size = 0;
	int dbv_index = 0;
	int temp_index = 0;
	unsigned int bl_lvl;
	static int old_dbv_index = -1;
	static int old_temp_index = -1;
	struct LCM_setting_table temp_compensation_cmd[50];
	struct dsi_display_mode *mode;
	struct dsi_cmd_desc *cmds;
	struct dsi_panel *panel = display_panel;
#ifdef OPLUS_FEATURE_DISPLAY
	bool pwm_turbo = oplus_pwm_turbo_is_enabled(panel);
#endif

	pr_debug("[DISP][DEBUG][%s:%d]start\n", __func__, __LINE__);
	SDE_ATRACE_BEGIN("oplus_display_temp_compensation_set");

	if (IS_ERR_OR_NULL(panel)) {
		pr_info("[DISP][INFO][%s:%d]Invalid params\n", __func__, __LINE__);
		return -EINVAL;
	}

	if (strcmp(panel->oplus_priv.vendor_name, "NT37705")) {
		pr_debug("[DISP][INFO][%s:%d]isn't NT37705 panel\n", __func__, __LINE__);
		return 0;
	}

	if (panel->is_secondary) {
		pr_info("[DISP][INFO][%s:%d]ignore for secondary panel\n", __func__, __LINE__);
		return 0;
	}

	if (!panel->panel_initialized) {
		pr_info("[DISP][INFO][%s:%d]ignore for panel uninitialized status\n", __func__, __LINE__);
		return 0;
	}

	if (IS_ERR_OR_NULL(&(panel->bl_config))) {
		pr_info("[DISP][INFO][%s:%d]Invalid params\n", __func__, __LINE__);
		return -EINVAL;
	}

	bl_lvl = panel->bl_config.bl_level;

#ifdef OPLUS_FEATURE_DISPLAY_ONSCREENFINGERPRINT
	if (oplus_ofp_is_supported()) {
		if (oplus_ofp_get_hbm_state()) {
			bl_lvl = 4095;
		}
	}
#endif /* OPLUS_FEATURE_DISPLAY_ONSCREENFINGERPRINT */
	SDE_ATRACE_INT("bl_lvl_comepensation", bl_lvl);

	if (IS_ERR_OR_NULL(panel->cur_mode)) {
		pr_info("[DISP][INFO][%s:%d]Invalid params\n", __func__, __LINE__);
		return -EINVAL;
	}

	mode = panel->cur_mode;

	if (IS_ERR_OR_NULL(mode->priv_info)) {
		pr_info("[DISP][INFO][%s:%d]Invalid params\n", __func__, __LINE__);
		return -EINVAL;
	}

	if (IS_ERR_OR_NULL(&(mode->priv_info->cmd_sets[DSI_CMD_TEMPERATURE_COMPENSATION]))) {
		pr_info("[DISP][INFO][%s:%d]Invalid params\n", __func__, __LINE__);
		return -EINVAL;
	}

	cmds = mode->priv_info->cmd_sets[DSI_CMD_TEMPERATURE_COMPENSATION].cmds;
	a_size = mode->priv_info->cmd_sets[DSI_CMD_TEMPERATURE_COMPENSATION].count;

	pr_debug("[DISP][INFO] a_size = %d", a_size);
	for(i = 0; i < a_size; i++) {
		temp_compensation_cmd[i].count = cmds[i].msg.tx_len;
		temp_compensation_cmd[i].para_list = (u8 *)cmds[i].msg.tx_buf;
/*
		pr_info(KERN_CONT "[DISP][INFO] count = %d, line %d: ", temp_compensation_cmd[i].count, i);
		for (j = 0; j < temp_compensation_cmd[i].count; j++) {
			pr_info(KERN_CONT "0x%02X ", temp_compensation_cmd[i].para_list[j]);
		}
*/
	}

	if (bl_lvl > 3515) {
		dbv_index = OPLUS_DISPLAY_GREATER_THAN_3515_DBV_INDEX;
	} else if (bl_lvl >= 1604) {
		dbv_index = OPLUS_DISPLAY_1604_3515_DBV_INDEX;
	} else if (bl_lvl >= 1511) {
		dbv_index = OPLUS_DISPLAY_1511_1604_DBV_INDEX;
	} else if (bl_lvl >= 1419) {
		dbv_index = OPLUS_DISPLAY_1419_1511_DBV_INDEX;
	} else if (bl_lvl >= 1328) {
		dbv_index = OPLUS_DISPLAY_1328_1419_DBV_INDEX;
	} else if (bl_lvl >= 1212) {
		dbv_index = OPLUS_DISPLAY_1212_1328_DBV_INDEX;
	} else if (bl_lvl >= 1096) {
		dbv_index = OPLUS_DISPLAY_1096_1212_DBV_INDEX;
	} else if (bl_lvl >= 950) {
		dbv_index = OPLUS_DISPLAY_950_1096_DBV_INDEX;
	} else if (bl_lvl >= 761) {
		dbv_index = OPLUS_DISPLAY_761_950_DBV_INDEX;
	} else if (bl_lvl >= 544) {
		dbv_index = OPLUS_DISPLAY_544_761_DBV_INDEX;
	} else {
		dbv_index = OPLUS_DISPLAY_LESS_THAN_544_DBV_INDEX;
	}

	if (IS_ERR_OR_NULL(g_oplus_display_temp)) {
		pr_debug("[DISP][DEBUG][%s:%d]panel ntc is not exist, use default ntc temp value\n", __func__, __LINE__);
		ntc_temp = 29;
	} else {
		ntc_temp = g_oplus_display_temp->ntc_temp;
	}

	if (!g_oplus_display_temp->compensation_enable) {
		pr_info("[DISP][INFO][%s:%d]compensation was disabled\n", __func__, __LINE__);
		return 0;
	}

	temp_index = oplus_display_get_temp_index(ntc_temp);

	if (oplus_dsi_log_type & OPLUS_DEBUG_LOG_COMPENSATION) {
		pr_info("[DISP][INFO][%s:%d]force_set=%d,bl_lvl=%d,ntc_temp=%d,shell_temp=%d,dbv_index=%d,temp_index=%d,old_dbv_index=%d,old_temp_index=%d\n",
				__func__, __LINE__, force_set, bl_lvl, ntc_temp, oplus_display_get_shell_temp(),
				dbv_index, temp_index, old_dbv_index, old_temp_index);
	}

	if ((old_dbv_index != dbv_index)
			|| (old_temp_index != temp_index)
			|| force_set) {
		for (i = 0; i < 4; i++) {
			temp_compensation_cmd[2].para_list[i+1] = temp_compensation_paras[dbv_index][temp_index][0];
			temp_compensation_cmd[4].para_list[i+1] = temp_compensation_paras[dbv_index][temp_index][1];
			temp_compensation_cmd[6].para_list[i+1] = temp_compensation_paras[dbv_index][temp_index][2];
			temp_compensation_cmd[12].para_list[i+1] = temp_compensation_paras[dbv_index][temp_index][5];
			temp_compensation_cmd[14].para_list[i+1] = temp_compensation_paras[dbv_index][temp_index][6];
			temp_compensation_cmd[16].para_list[i+1] = temp_compensation_paras[dbv_index][temp_index][7];

			if (pwm_turbo) {
				temp_compensation_cmd[25].para_list[i+1] = temp_compensation_paras[dbv_index][temp_index][21+i];
			} else {
				temp_compensation_cmd[25].para_list[i+1] = temp_compensation_paras[dbv_index][temp_index][17+i];
			}
		}

		for (i = 0; i < 3; i++) {
			temp_compensation_cmd[8].para_list[i+1] = temp_compensation_paras[dbv_index][temp_index][3];
			temp_compensation_cmd[10].para_list[i+1] = temp_compensation_paras[dbv_index][temp_index][4];
			temp_compensation_cmd[18].para_list[i+1] = temp_compensation_paras[dbv_index][temp_index][8+i];
			temp_compensation_cmd[20].para_list[i+1] = temp_compensation_paras[dbv_index][temp_index][11+i];
			temp_compensation_cmd[22].para_list[i+1] = temp_compensation_paras[dbv_index][temp_index][14+i];
		}

		if (oplus_dsi_log_type & OPLUS_DEBUG_LOG_CMD) {
			for(i = 0; i < a_size; i++) {
				cmds[i].msg.tx_buf = temp_compensation_cmd[i].para_list;
				pr_info(KERN_CONT "[DISP][INFO] count = %d, line %d: ", temp_compensation_cmd[i].count, i);
				for (j = 0; j < temp_compensation_cmd[i].count; j++) {
					pr_info(KERN_CONT "0x%02X ", temp_compensation_cmd[i].para_list[j]);
				}
				pr_info(KERN_CONT "\n");
			}
		}

		if (force_set) {
			pr_info("[DISP][INFO][%s:%d] panel resume, force set compensation params\n", __func__, __LINE__);
		} else if (old_dbv_index != dbv_index) {
			pr_info("[DISP][INFO][%s:%d] backlight change, set compensation params, bl_lvl = %d\n", __func__, __LINE__, bl_lvl);
		} else if (old_temp_index != temp_index) {
			pr_info("[DISP][INFO][%s:%d] temperature change, set compensation params\n", __func__, __LINE__);
		}

		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_TEMPERATURE_COMPENSATION);
		if (rc) {
			DSI_ERR("[DISP][ERR] [%s] failed to send DSI_CMD_TEMPERATURE_COMPENSATION cmds rc=%d\n", panel->name, rc);
		}
	}

	old_dbv_index = dbv_index;
	old_temp_index = temp_index;
	SDE_ATRACE_END("oplus_display_temp_compensation_set");
	pr_debug("[DISP][DEBUG][%s:%d]end\n", __func__, __LINE__);

	return 0;
}
EXPORT_SYMBOL(oplus_display_temp_compensation_set);

int oplus_display_temp_check(void *display)
{
	int rc = 0;
	int ntc_temp = 0;
	int shell_temp = 0;
	int last_ntc_temp = 0;
	struct dsi_display *dsi_display = display;
	struct dsi_panel *panel;

	pr_debug("[DISP][DEBUG][%s:%d]start\n", __func__, __LINE__);

	if (!dsi_display) {
		pr_info("[DISP][INFO][%s:%d]Invalid params\n", __func__, __LINE__);
		return -EINVAL;
	}

	panel = dsi_display->panel;

	if (!panel) {
		pr_info("[DISP][INFO][%s:%d]Invalid params\n", __func__, __LINE__);
		return -EINVAL;
	}

	if (strcmp(panel->oplus_priv.vendor_name, "NT37705")) {
		pr_debug("[DISP][INFO][%s:%d]isn't NT37705 panel\n", __func__, __LINE__);
		return 0;
	}

	if (!panel->panel_initialized) {
		pr_info("[DISP][INFO][%s:%d]Panel not initialized\n", __func__, __LINE__);
		return rc;
	}

	if (panel->is_secondary) {
		pr_info("[DISP][INFO][%s:%d]ignore for secondary panel\n", __func__, __LINE__);
		return rc;
	}

	if (IS_ERR_OR_NULL(g_oplus_display_temp)) {
		pr_info("[DISP][INFO][%s:%d]Invalid params\n", __func__, __LINE__);
		return -EINVAL;
	}

	if (!g_oplus_display_temp->compensation_enable) {
		pr_info("[DISP][INFO][%s:%d]compensation was disabled\n", __func__, __LINE__);
		return 0;
	}

	if (!&(panel->bl_config)) {
		pr_info("[DISP][INFO][%s:%d]Invalid params\n", __func__, __LINE__);
		return -EINVAL;
	}

	last_ntc_temp = g_oplus_display_temp->ntc_temp;
	ntc_temp = oplus_display_get_ntc_temp();
	shell_temp = oplus_display_get_shell_temp();

	if (oplus_display_get_temp_index(last_ntc_temp) != oplus_display_get_temp_index(ntc_temp)) {
		mutex_lock(&panel->panel_lock);
		rc = oplus_display_temp_compensation_set(panel, false);
		if (rc) {
			pr_err("[DISP][ERR][%s:%d]unable to set compensation cmd\n", __func__, __LINE__);
		}
		mutex_unlock(&panel->panel_lock);

		pr_info("[DISP][INFO][%s:%d]last_ntc_temp:%d,current_ntc_temp:%d, update temp compensation cmd\n",
			__func__, __LINE__, last_ntc_temp, ntc_temp);
	}

	if (oplus_adfr_is_support()) {
		oplus_adfr_temperature_detection_handle(dsi_display, ntc_temp, shell_temp);
	}

	pr_debug("[DISP][DEBUG][%s:%d]end\n", __func__, __LINE__);

	return 0;
}
EXPORT_SYMBOL(oplus_display_temp_check);

void oplus_compensation_set_work_handler(struct work_struct *work_item)
{
	int rc = 0;
	struct dsi_display *display = oplus_display_get_current_display();
	struct dsi_panel *panel = NULL;

	if (!display) {
		DSI_ERR("[DISP][ERR][%s:%d] Invalid display params\n", __func__, __LINE__);
		return;
	}

	panel = display->panel;
	if (!panel) {
		DSI_ERR("[DISP][ERR][%s:%d] Invalid panel params\n", __func__, __LINE__);
		return;
	}

	SDE_ATRACE_BEGIN("oplus_compensation_set_work_handler");

	if (oplus_dsi_log_type & OPLUS_DEBUG_LOG_COMPENSATION) {
		pr_info("[DISP][INFO][%s:%d]send temp compensation cmd\n", __func__, __LINE__);
	}
	oplus_sde_early_wakeup();
	oplus_wait_for_vsync(panel);
	mutex_lock(&panel->panel_lock);
	rc = oplus_display_temp_compensation_set(panel, false);
	mutex_unlock(&panel->panel_lock);
	if (rc) {
		pr_err("[DISP][ERR][%s:%d]failed to send temp compensation cmd\n", __func__, __LINE__);
	}

	SDE_ATRACE_END("oplus_compensation_set_work_handler");

	return;
}

void oplus_display_queue_compensation_set_work(void)
{
	if (IS_ERR_OR_NULL(g_oplus_display_temp)) {
		pr_debug("[DISP][DEBUG][%s:%d]panel ntc is not exist, use default ntc temp value\n", __func__, __LINE__);
		return;
	}
	queue_work(g_oplus_display_temp->oplus_compensation_set_wq, &g_oplus_display_temp->oplus_compensation_set_work);
}

ssize_t oplus_display_get_cmd_delay_attr(struct kobject *obj,
	struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", oplus_display_cmd_delay);
}

ssize_t oplus_display_set_cmd_delay_attr(struct kobject *obj,
	struct kobj_attribute *attr, const char *buf, size_t count)
{
	int cmd_delay = 0;

	if (IS_ERR_OR_NULL(g_oplus_display_temp)) {
		pr_info("[DISP][INFO][%s:%d]Invalid params, failed to fake ntc temp\n", __func__, __LINE__);
		return -EINVAL;
	}

	sscanf(buf, "%du", &cmd_delay);

	oplus_display_cmd_delay = cmd_delay;

	return count;
}

ssize_t oplus_display_get_compensation_enable_attr(struct kobject *obj,
	struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", g_oplus_display_temp->compensation_enable);
}

ssize_t oplus_display_set_compensation_enable_attr(struct kobject *obj,
	struct kobj_attribute *attr, const char *buf, size_t count)
{
	int compensation_enable = 0;

	if (IS_ERR_OR_NULL(g_oplus_display_temp)) {
		pr_info("[DISP][INFO][%s:%d]Invalid params, failed to fake ntc temp\n", __func__, __LINE__);
		return -EINVAL;
	}

	sscanf(buf, "%d", &compensation_enable);

	g_oplus_display_temp->compensation_enable = compensation_enable;

	return count;
}
ssize_t oplus_display_get_ntc_temp_attr(struct kobject *obj,
	struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", oplus_display_get_ntc_temp());
}

ssize_t oplus_display_set_ntc_temp_attr(struct kobject *obj,
	struct kobj_attribute *attr, const char *buf, size_t count)
{
	int ntc_temp = 0;

	if (IS_ERR_OR_NULL(g_oplus_display_temp)) {
		pr_info("[DISP][INFO][%s:%d]Invalid params, failed to fake ntc temp\n", __func__, __LINE__);
		return -EINVAL;
	}

	sscanf(buf, "%du", &ntc_temp);

	if (ntc_temp == -1) {
		g_oplus_display_temp->fake_ntc_temp = false;
		return count;
	}
	g_oplus_display_temp->ntc_temp = ntc_temp;
	g_oplus_display_temp->fake_ntc_temp = true;

	return count;
}

ssize_t oplus_display_get_shell_temp_attr(struct kobject *obj,
	struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", oplus_display_get_shell_temp());
}

ssize_t oplus_display_set_shell_temp_attr(struct kobject *obj,
	struct kobj_attribute *attr, const char *buf, size_t count)
{
	int shell_temp = 0;

	if (IS_ERR_OR_NULL(g_oplus_display_temp)) {
		pr_info("[DISP][INFO][%s:%d]Invalid params, failed to fake ntc temp\n", __func__, __LINE__);
		return -EINVAL;
	}

	sscanf(buf, "%du", &shell_temp);

	if (shell_temp == -1) {
		g_oplus_display_temp->fake_shell_temp = false;
		return count;
	}
	g_oplus_display_temp->shell_temp = shell_temp;
	g_oplus_display_temp->fake_shell_temp = true;

	return count;
}

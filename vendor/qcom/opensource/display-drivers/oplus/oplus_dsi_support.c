/***************************************************************
** Copyright (C),  2020,  OPLUS Mobile Comm Corp.,  Ltd
**
** File : oplus_dsi_support.c
** Description : display driver private management
** Version : 1.1
** Date : 2020/09/06
**
** ------------------------------- Revision History: -----------
**  <author>        <data>        <version >        <desc>
**   LiPing-M         2020/09/06        1.1           Build this moudle
******************************************************************/
#include "oplus_dsi_support.h"
#include <soc/oplus/system/boot_mode.h>
#include <soc/oplus/system/oplus_project.h>
#include <soc/oplus/device_info.h>
#include <linux/notifier.h>
#include <linux/module.h>
#include "dsi_display.h"

#ifdef OPLUS_FEATURE_DISPLAY_ONSCREENFINGERPRINT
#include "oplus_onscreenfingerprint.h"
#endif /* OPLUS_FEATURE_DISPLAY_ONSCREENFINGERPRINT */

static enum oplus_display_support_list  oplus_display_vendor =
	OPLUS_DISPLAY_UNKNOW;
static enum oplus_display_power_status oplus_display_status =
	OPLUS_DISPLAY_POWER_OFF;
static BLOCKING_NOTIFIER_HEAD(oplus_display_notifier_list);
/* add for dual panel */
static struct dsi_display *current_display = NULL;

int oplus_display_register_client(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&oplus_display_notifier_list,
						nb);
}
EXPORT_SYMBOL(oplus_display_register_client);


int oplus_display_unregister_client(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&oplus_display_notifier_list,
			nb);
}
EXPORT_SYMBOL(oplus_display_unregister_client);

static int oplus_display_notifier_call_chain(unsigned long val, void *v)
{
	return blocking_notifier_call_chain(&oplus_display_notifier_list, val,
					    v);
}

bool is_oplus_correct_display(enum oplus_display_support_list lcd_name)
{
	return (oplus_display_vendor == lcd_name ? true : false);
}

bool is_silence_reboot(void)
{
	pr_err("%s get_boot_mode=%d!", __func__, get_boot_mode());
	if ((MSM_BOOT_MODE__SILENCE == get_boot_mode())
			|| (MSM_BOOT_MODE__SAU == get_boot_mode())) {
		return true;

	} else {
		return false;
	}
}
EXPORT_SYMBOL(is_silence_reboot);

int set_oplus_display_vendor(const char *display_name)
{
	if (display_name == NULL) {
		return -1;
	}

	if (!strcmp(display_name,
			"qcom,mdss_dsi_oplus19065_samsung_1440_3168_dsc_cmd")) {
		oplus_display_vendor = OPLUS_SAMSUNG_ANA6706_DISPLAY_FHD_DSC_CMD_PANEL;
		//register_device_proc("lcd", "ANA6706", "samsung1024");

	} else if (!strcmp(display_name, "qcom,mdss_dsi_samsung_oneplus_dsc_cmd")) {
		oplus_display_vendor = OPLUS_SAMSUNG_ONEPLUS_DISPLAY_FHD_DSC_CMD_PANEL;
		//register_device_proc("lcd", "oneplus", "samsung1024");

	} else {
		oplus_display_vendor = OPLUS_DISPLAY_UNKNOW;
		pr_err("%s panel vendor info set failed!", __func__);
	}

	return 0;
}

void notifier_oplus_display_early_status(enum oplus_display_power_status
					power_status)
{
	int blank;
	OPLUS_DISPLAY_NOTIFIER_EVENT oplus_notifier_data;

	switch (power_status) {
	case OPLUS_DISPLAY_POWER_ON:
		blank = OPLUS_DISPLAY_POWER_ON;
		oplus_notifier_data.data = &blank;
		oplus_notifier_data.status = OPLUS_DISPLAY_POWER_ON;
		oplus_display_notifier_call_chain(OPLUS_DISPLAY_EARLY_EVENT_BLANK,
						 &oplus_notifier_data);
		break;

	case OPLUS_DISPLAY_POWER_DOZE:
		blank = OPLUS_DISPLAY_POWER_DOZE;
		oplus_notifier_data.data = &blank;
		oplus_notifier_data.status = OPLUS_DISPLAY_POWER_DOZE;
		oplus_display_notifier_call_chain(OPLUS_DISPLAY_EARLY_EVENT_BLANK,
						 &oplus_notifier_data);
		break;

	case OPLUS_DISPLAY_POWER_DOZE_SUSPEND:
		blank = OPLUS_DISPLAY_POWER_DOZE_SUSPEND;
		oplus_notifier_data.data = &blank;
		oplus_notifier_data.status = OPLUS_DISPLAY_POWER_DOZE_SUSPEND;
		oplus_display_notifier_call_chain(OPLUS_DISPLAY_EARLY_EVENT_BLANK,
						 &oplus_notifier_data);
		break;

	case OPLUS_DISPLAY_POWER_OFF:
		blank = OPLUS_DISPLAY_POWER_OFF;
		oplus_notifier_data.data = &blank;
		oplus_notifier_data.status = OPLUS_DISPLAY_POWER_OFF;
		oplus_display_notifier_call_chain(OPLUS_DISPLAY_EARLY_EVENT_BLANK,
						 &oplus_notifier_data);
		break;

	default:
		break;
	}
}

void notifier_oplus_display_status(enum oplus_display_power_status power_status)
{
	int blank;
	OPLUS_DISPLAY_NOTIFIER_EVENT oplus_notifier_data;

	switch (power_status) {
	case OPLUS_DISPLAY_POWER_ON:
		blank = OPLUS_DISPLAY_POWER_ON;
		oplus_notifier_data.data = &blank;
		oplus_notifier_data.status = OPLUS_DISPLAY_POWER_ON;
		oplus_display_notifier_call_chain(OPLUS_DISPLAY_EVENT_BLANK,
						 &oplus_notifier_data);
		break;

	case OPLUS_DISPLAY_POWER_DOZE:
		blank = OPLUS_DISPLAY_POWER_DOZE;
		oplus_notifier_data.data = &blank;
		oplus_notifier_data.status = OPLUS_DISPLAY_POWER_DOZE;
		oplus_display_notifier_call_chain(OPLUS_DISPLAY_EVENT_BLANK,
						 &oplus_notifier_data);
		break;

	case OPLUS_DISPLAY_POWER_DOZE_SUSPEND:
		blank = OPLUS_DISPLAY_POWER_DOZE_SUSPEND;
		oplus_notifier_data.data = &blank;
		oplus_notifier_data.status = OPLUS_DISPLAY_POWER_DOZE_SUSPEND;
		oplus_display_notifier_call_chain(OPLUS_DISPLAY_EVENT_BLANK,
						 &oplus_notifier_data);
		break;

	case OPLUS_DISPLAY_POWER_OFF:
		blank = OPLUS_DISPLAY_POWER_OFF;
		oplus_notifier_data.data = &blank;
		oplus_notifier_data.status = OPLUS_DISPLAY_POWER_OFF;
		oplus_display_notifier_call_chain(OPLUS_DISPLAY_EVENT_BLANK,
						 &oplus_notifier_data);
		break;

	default:
		break;
	}
}

void set_oplus_display_power_status(enum oplus_display_power_status power_status)
{
	oplus_display_status = power_status;
}
EXPORT_SYMBOL(set_oplus_display_power_status);

enum oplus_display_power_status get_oplus_display_power_status(void)
{
	return oplus_display_status;
}
EXPORT_SYMBOL(get_oplus_display_power_status);

bool is_oplus_display_support_feature(enum oplus_display_feature feature_name)
{
	bool ret = false;

	switch (feature_name) {
	case OPLUS_DISPLAY_HDR:
		ret = false;
		break;

	case OPLUS_DISPLAY_SEED:
		ret = true;
		break;

	case OPLUS_DISPLAY_HBM:
		ret = true;
		break;

	case OPLUS_DISPLAY_LBR:
		ret = true;
		break;

	case OPLUS_DISPLAY_AOD:
		ret = true;
		break;

	case OPLUS_DISPLAY_ULPS:
		ret = false;
		break;

	case OPLUS_DISPLAY_ESD_CHECK:
		ret = true;
		break;

	case OPLUS_DISPLAY_DYNAMIC_MIPI:
		ret = true;
		break;

	case OPLUS_DISPLAY_PARTIAL_UPDATE:
		ret = false;
		break;

	default:
		break;
	}

	return ret;
}

/* add for dual panel */
void oplus_display_set_current_display(void *dsi_display)
{
	struct dsi_display *display = dsi_display;
	current_display = display;
}

/* update current display when panel is enabled and disabled */
void oplus_display_update_current_display(void)
{
	struct dsi_display *primary_display = get_main_display();
	struct dsi_display *secondary_display = get_sec_display();

	pr_debug("[DEBUG][%s:%d]start\n", __func__, __LINE__);

#if defined(OPLUS_FEATURE_PXLW_IRIS5)
	if (iris_is_chip_supported()) {
		if (primary_display && primary_display->panel) {
			current_display = primary_display;
		} else {
			current_display = NULL;
			pr_err("[ERROR][%s:%d]failed to get current display\n", __func__, __LINE__);
		}
		return;
	}
#endif /* OPLUS_FEATURE_PXLW_IRIS5 */

	if ((!primary_display && !secondary_display) || (!primary_display->panel && !secondary_display->panel)) {
		current_display = NULL;
	} else if ((primary_display && !secondary_display) || (primary_display->panel && !secondary_display->panel)) {
		current_display = primary_display;
	} else if ((!primary_display && secondary_display) || (!primary_display->panel && secondary_display->panel)) {
		current_display = secondary_display;
	} else if (primary_display->panel->panel_initialized && !secondary_display->panel->panel_initialized) {
		current_display = primary_display;
	} else if (!primary_display->panel->panel_initialized && secondary_display->panel->panel_initialized) {
		current_display = secondary_display;
	} else if (primary_display->panel->panel_initialized && secondary_display->panel->panel_initialized) {
		current_display = primary_display;
	}

#ifdef OPLUS_FEATURE_DISPLAY_ONSCREENFINGERPRINT
	if (oplus_ofp_is_supported()) {
		oplus_ofp_update_display_id();
	}
#endif /* OPLUS_FEATURE_DISPLAY_ONSCREENFINGERPRINT */

	pr_debug("[DEBUG][%s:%d]end\n", __func__, __LINE__);

	return;
}

struct dsi_display *oplus_display_get_current_display(void)
{
	return current_display;
}

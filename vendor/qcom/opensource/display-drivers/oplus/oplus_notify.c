/***************************************************************
** Copyright (C),  2020,  oplus Mobile Comm Corp.,  Ltd
**
** File : oplus_aod.c
** Description : oplus aod feature
** Version : 1.0
** Date : 2020/09/24
**
** ------------------------------- Revision History: -----------
**  <author>        <data>        <version >        <desc>
**  liping-m         2020/09/24        1.0           Build this moudle
******************************************************************/
#include <linux/msm_drm_notify.h>
#include <linux/module.h>
#include <linux/soc/qcom/panel_event_notifier.h>
#include "dsi_display.h"
#include "dsi_drm.h"
#include "sde_encoder.h"


//#ifdef OPLUS_BUG_STABILITY
static BLOCKING_NOTIFIER_HEAD(msm_drm_notifier_list);

/**
 * msm_drm_register_client - register a client notifier
 * @nb: notifier block to callback on events
 *
 * This function registers a notifier callback function
 * to msm_drm_notifier_list, which would be called when
 * received unblank/power down event.
 */
int msm_drm_register_client(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&msm_drm_notifier_list,
						nb);
}
EXPORT_SYMBOL(msm_drm_register_client);

/**
 * msm_drm_unregister_client - unregister a client notifier
 * @nb: notifier block to callback on events
 *
 * This function unregisters the callback function from
 * msm_drm_notifier_list.
 */
int msm_drm_unregister_client(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&msm_drm_notifier_list,
						  nb);
}
EXPORT_SYMBOL(msm_drm_unregister_client);

/**
 * msm_drm_notifier_call_chain - notify clients of drm_events
 * @val: event MSM_DRM_EARLY_EVENT_BLANK or MSM_DRM_EVENT_BLANK
 * @v: notifier data, inculde display id and display blank
 *     event(unblank or power down).
 */
int msm_drm_notifier_call_chain(unsigned long val, void *v)
{
	return blocking_notifier_call_chain(&msm_drm_notifier_list, val,
					    v);
}
EXPORT_SYMBOL(msm_drm_notifier_call_chain);

int oplus_panel_event_notification_trigger(struct dsi_display *display, enum panel_event_notification_type notif_type)
{
	struct panel_event_notification notification;
	struct drm_panel *panel = dsi_display_get_drm_panel(display);
	enum panel_event_notifier_tag panel_type;

	if (!panel)
		return -ENOLINK;

	panel_type = PANEL_EVENT_NOTIFICATION_PRIMARY;

	memset(&notification, 0, sizeof(notification));

	notification.notif_type = notif_type;
	notification.panel = panel;
	notification.notif_data.early_trigger = true;
	panel_event_notification_trigger(panel_type, &notification);
	return 0;
}

EXPORT_SYMBOL(oplus_panel_event_notification_trigger);

int oplus_display_event_data_notifier_trigger(struct dsi_display *display,
		enum panel_event_notifier_tag panel_type,
		enum panel_event_notification_type notif_type,
		u32 data)
{
	struct drm_panel *panel = dsi_display_get_drm_panel(display);
	struct panel_event_notification notifier;

	if (!panel) {
		pr_err("[%s] invalid panel\n", __func__);
		return -EINVAL;
	}

	memset(&notifier, 0, sizeof(notifier));

	notifier.panel = panel;
	notifier.notif_type = notif_type;
	notifier.notif_data.early_trigger = true;
	notifier.notif_data.data = data;

	panel_event_notification_trigger(panel_type, &notifier);
	return 0;
}
EXPORT_SYMBOL(oplus_display_event_data_notifier_trigger);

MODULE_LICENSE("GPL v2");
//#endif /* VENDOR_EDIT */

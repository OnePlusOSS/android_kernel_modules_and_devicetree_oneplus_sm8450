/* SPDX-License-Identifier: GPL-2.0-only  */
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */
#include "palm_detecthub.h"
#include "sensor_cmd.h"
#include "virtual_sensor.h"
#include <linux/notifier.h>
#include <linux/pm_wakeup.h>
#include <linux/version.h>

#define PALM_DETECT_TAG					"[palm_detecthub] "
#define PALM_DETECT_FUN(f)				pr_err(PALM_DETECT_TAG"%s\n", __func__)
#define PALM_DETECT_PR_ERR(fmt, args...)	pr_err(PALM_DETECT_TAG"%s %d : "fmt, __func__, __LINE__, ##args)
#define PALM_DETECT_LOG(fmt, args...)		pr_err(PALM_DETECT_TAG fmt, ##args)

static struct virtual_sensor_init_info palm_detecthub_init_info;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 19, 0))
static struct wakeup_source palm_detect_wake_lock;
#else
static struct wakeup_source *palm_detect_wake_lock = NULL;
#endif

static int palm_detect_open_report_data(int open)
{
	return 0;
}

static int palm_detect_enable_nodata(int en)
{
	PALM_DETECT_LOG("palm_detect enable nodata, en = %d\n", en);

	return oplus_enable_to_hub(ID_PALM_DETECT, en);
}

static int palm_detect_set_delay(u64 delay)
{
#if defined CONFIG_MTK_SCP_SENSORHUB_V1
	unsigned int delayms = 0;

	delayms = delay / 1000 / 1000;
	return oplus_set_delay_to_hub(ID_PALM_DETECT, delayms);
#elif defined CONFIG_NANOHUB
	return 0;
#else
	return 0;
#endif
}

static int palm_detect_batch(int flag, int64_t samplingPeriodNs, int64_t maxBatchReportLatencyNs)
{
#if defined CONFIG_MTK_SCP_SENSORHUB_V1
	palm_detect_set_delay(samplingPeriodNs);
#endif

	PALM_DETECT_LOG("palm_detect: samplingPeriodNs:%lld, maxBatchReportLatencyNs: %lld\n",
		samplingPeriodNs, maxBatchReportLatencyNs);

	return oplus_batch_to_hub(ID_PALM_DETECT, flag, samplingPeriodNs, maxBatchReportLatencyNs);
}

static int palm_detect_flush(void)
{
	return oplus_flush_to_hub(ID_PALM_DETECT);
}

static int palm_detect_data_report(struct data_unit_t *input_event)
{
	struct oplus_sensor_event event;

	memset(&event, 0, sizeof(struct oplus_sensor_event));

	event.handle = ID_PALM_DETECT;
	event.flush_action = DATA_ACTION;
	event.time_stamp = (int64_t)input_event->time_stamp;
	event.word[0] = input_event->oplus_data_t.palm_detect_event.value;
	event.word[1] = input_event->oplus_data_t.palm_detect_event.state;
	event.word[2] = input_event->oplus_data_t.palm_detect_event.report_count;
	return virtual_sensor_data_report(&event);
}

static int	palm_detect_flush_report()
{
	return virtual_sensor_flush_report(ID_PALM_DETECT);
}

static int palm_detect_recv_data(struct data_unit_t *event, void *reserved)
{
	int err = 0;

	PALM_DETECT_LOG("palm_detect recv data, flush_action = %d, value = %d, state = %d, report_count = %d, timestamp = %lld\n",
		event->flush_action,
		event->oplus_data_t.palm_detect_event.value,
		event->oplus_data_t.palm_detect_event.state,
		event->oplus_data_t.palm_detect_event.report_count,
		(int64_t)event->time_stamp);

	if (event->flush_action == DATA_ACTION) {
		/*hold 100 ms timeout wakelock*/
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 19, 0))
		__pm_wakeup_event(&palm_detect_wake_lock, msecs_to_jiffies(100));
#else
		__pm_wakeup_event(palm_detect_wake_lock, msecs_to_jiffies(100));
#endif
		palm_detect_data_report(event);
	} else if (event->flush_action == FLUSH_ACTION) {
		err = palm_detect_flush_report();
	}

	return err;
}

static int palm_detecthub_local_init(void)
{
	struct virtual_sensor_control_path ctl = {0};
	int err = 0;

	ctl.open_report_data = palm_detect_open_report_data;
	ctl.enable_nodata = palm_detect_enable_nodata;
	ctl.set_delay = palm_detect_set_delay;
	ctl.batch = palm_detect_batch;
	ctl.flush = palm_detect_flush;
	ctl.report_data = palm_detect_recv_data;

#if defined CONFIG_MTK_SCP_SENSORHUB_V1
	ctl.is_report_input_direct = true;
	ctl.is_support_batch = false;
#ifdef OPLUS_FEATURE_SENSOR_ALGORITHM
	ctl.is_support_wake_lock = true;
#endif
#elif defined CONFIG_NANOHUB
	ctl.is_report_input_direct = true;
	ctl.is_support_batch = false;
#ifdef OPLUS_FEATURE_SENSOR_ALGORITHM
	ctl.is_support_wake_lock = true;
#endif
#else
#endif

	err = virtual_sensor_register_control_path(&ctl, ID_PALM_DETECT);
	if (err) {
		PALM_DETECT_PR_ERR("register palm_detect control path err\n");
		goto exit;
	}
#ifdef _OPLUS_SENSOR_HUB_VI

	err = scp_sensorHub_data_registration(ID_PALM_DETECT, palm_detect_recv_data);
	if (err < 0) {
		PALM_DETECT_PR_ERR("SCP_sensorHub_data_registration failed\n");
		goto exit;
	}
#endif
	#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 19, 0))
	wakeup_source_init(&palm_detect_wake_lock, "palm_detect_wake_lock");
	#else
	palm_detect_wake_lock = wakeup_source_register(NULL, "palm_detect_wake_lock");
	#endif

        PALM_DETECT_LOG("palm_detecthub_local_init done.");
	return 0;
exit:
	return -1;
}

static int palm_detecthub_local_uninit(void)
{
	return 0;
}

static struct virtual_sensor_init_info palm_detecthub_init_info = {
	.name = "palm_detect_hub",
	.init = palm_detecthub_local_init,
	.uninit = palm_detecthub_local_uninit,
};

static int __init palm_detecthub_init(void)
{
	PALM_DETECT_LOG("palm_detecthub_init  done.");
	virtual_sensor_driver_add(&palm_detecthub_init_info, ID_PALM_DETECT);
	return 0;
}

static void __exit palm_detecthub_exit(void)
{
	PALM_DETECT_FUN();
}

module_init(palm_detecthub_init);
module_exit(palm_detecthub_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ACTIVITYHUB driver");

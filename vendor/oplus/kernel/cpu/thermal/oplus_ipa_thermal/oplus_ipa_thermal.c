// SPDX-License-Identifier: GPL-2.0-only
/*
 * oplus_ipa_thermal.c
 *
 * function of oplus_ipa_thermal module
 *
 * Copyright (c) 2022 Oplus. All rights reserved.
 *
 */

#include <linux/cpu_cooling.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/thermal.h>
#include <linux/cpumask.h>
#include <linux/topology.h>
#include <linux/energy_model.h>
#include "../horae_shell_temp/horae_shell_temp.h"
#include <linux/pm_opp.h>
#include <linux/pm_qos.h>
#include <linux/cpufreq.h>
#include <linux/slab.h>
#include <../../../drivers/thermal/thermal_core.h>

#include <trace/hooks/thermal.h>
#include <trace/events/thermal.h>

#define IPA_SENSOR "ipa_sensor0"

/* The frequency table in cooling device for both MTK and Qcom platforms is
 * arranged in descending order by default. To ensure optimal performance,
 * the maximum frequency while boost is engaged is set to the second highest
 * frequency in the table.
 */
#define BOOST_STATE_IDX 1

struct oplus_ipa_sensor {
	u16 sensor_id;
	struct thermal_zone_device *tzd;
};

static struct oplus_ipa_sensor ipa_temp_sensor;

static inline int oplus_get_shell_temp()
{
	return get_current_shell_temp();
}

static int oplus_get_temp_value(void *data, int *temp)
{
	*temp = oplus_get_shell_temp();

	return 0;
}

static struct thermal_zone_of_device_ops oplus_ipa_sensor_ops = {
	.get_temp = oplus_get_temp_value,
};

static int reset_freq_limit(struct thermal_cooling_device *cdev)
{
	int ret;

	if (!cdev->devdata) {
		pr_err("err, not freq cooling device!\n");
		return -ENODEV;
	}

	mutex_lock(&cdev->lock);
	ret = cdev->ops->set_cur_state(cdev, BOOST_STATE_IDX);
	mutex_unlock(&cdev->lock);

	return ret;
}

void oplus_ipa_get_mode_change(void *unused, int event, int tz_id, int *enable_thermal_genl)
{
	struct oplus_ipa_sensor *sensor = &ipa_temp_sensor;
	struct thermal_cooling_device *cdev;
	struct thermal_instance *instance;
	int ipa_tz_id;

	if (!sensor->tzd) {
		pr_err("err, tzd init not finished\n");
		return;
	}

	ipa_tz_id = sensor->tzd->id;

	if(event != THERMAL_GENL_EVENT_TZ_DISABLE || ipa_tz_id != tz_id)
		return;

	list_for_each_entry(instance, &sensor->tzd->thermal_instances, tz_node) {
		cdev = instance->cdev;
		if (reset_freq_limit(cdev) < 0)
			pr_err("failed to update the frequency limit in the vh\n");
	}
}

void register_thermal_ipa_vh(void)
{
	register_trace_android_vh_enable_thermal_genl_check(oplus_ipa_get_mode_change, NULL);
}

static int oplus_ipa_thermal_probe(struct platform_device *pdev)
{
	struct oplus_ipa_sensor *sensor_data = &ipa_temp_sensor;
	int sensor = 0, cpu;
	struct em_perf_domain *domain = NULL;

	for_each_possible_cpu(cpu) {
		domain = em_cpu_get(cpu);
		if (domain == NULL) {
			dev_err(&pdev->dev, "cpu%d's perf domain not"
					"created yet, probe deferred\n", cpu);
			return -EPROBE_DEFER;
		}
	}

	register_thermal_ipa_vh();

	sensor_data->sensor_id = (u16)sensor;
	dev_err(&pdev->dev, "Probed %s sensor. Id=%hu\n",
			IPA_SENSOR, sensor_data->sensor_id);
	sensor_data->tzd = thermal_zone_of_sensor_register(&pdev->dev,
									sensor_data->sensor_id,
									sensor_data,
									&oplus_ipa_sensor_ops);

	if (IS_ERR(sensor_data->tzd)) {
		dev_err(&pdev->dev, "Error registering sensor: %ld\n",
					PTR_ERR(sensor_data->tzd));
		return PTR_ERR(sensor_data->tzd);
	}

	platform_set_drvdata(pdev, sensor_data);

	return 0;
}

static int oplus_ipa_thermal_remove(struct platform_device *pdev)
{
	struct oplus_ipa_sensor *sensor = platform_get_drvdata(pdev);

	thermal_zone_of_sensor_unregister(&pdev->dev, sensor->tzd);
	platform_set_drvdata(pdev, NULL);

	return 0;
}

static struct of_device_id ipa_thermal_of_match[] = {
	{ .compatible = "oplus,ipa-thermal-sensor"},
	{},
};

static struct platform_driver oplus_ipa_thermal_platdrv = {
	.driver = {
		.name = "oplus_ipa_thermal",
		.owner = THIS_MODULE,
		.of_match_table = ipa_thermal_of_match,
	},
	.probe = oplus_ipa_thermal_probe,
	.remove = oplus_ipa_thermal_remove,
};
module_platform_driver(oplus_ipa_thermal_platdrv);

MODULE_LICENSE("GPL v2");

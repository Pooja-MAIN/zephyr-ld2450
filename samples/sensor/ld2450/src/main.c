/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <math.h>
#include <stdio.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/sensor/ld2450.h>
#include <zephyr/kernel.h>

#define RAD_TO_DEG (180.0 / 3.14159265358979323846)

static const struct device *const radar = DEVICE_DT_GET_ONE(hilink_ld2450);

static void print_target(int index)
{
	struct sensor_value x;
	struct sensor_value y;
	struct sensor_value speed;
	double x_m;
	double y_m;

	if ((sensor_channel_get(radar, SENSOR_CHAN_LD2450_TARGET_X(index), &x) != 0) ||
	    (sensor_channel_get(radar, SENSOR_CHAN_LD2450_TARGET_Y(index), &y) != 0) ||
	    (sensor_channel_get(radar, SENSOR_CHAN_LD2450_TARGET_SPEED(index), &speed) != 0)) {
		return;
	}

	x_m = sensor_value_to_double(&x);
	y_m = sensor_value_to_double(&y);

	printf("  target %d: x=%6.3f m  y=%6.3f m  range=%6.3f m  azimuth=%6.1f deg  "
	       "speed=%6.2f m/s\n",
	       index + 1, x_m, y_m, sqrt((x_m * x_m) + (y_m * y_m)), atan2(x_m, y_m) * RAD_TO_DEG,
	       sensor_value_to_double(&speed));
}

int main(void)
{
	struct sensor_value count;
	int rc;

	if (!device_is_ready(radar)) {
		printf("LD2450 device %s is not ready\n", radar->name);
		return 0;
	}

	while (1) {
		rc = sensor_sample_fetch(radar);
		if (rc != 0) {
			printf("sample fetch failed: %d\n", rc);
			k_sleep(K_MSEC(500));
			continue;
		}

		sensor_channel_get(radar, (enum sensor_channel)SENSOR_CHAN_LD2450_TARGET_COUNT, &count);
		printf("targets: %d\n", count.val1);

		for (int i = 0; i < LD2450_TARGET_COUNT_MAX; i++) {
			print_target(i);
		}

		k_sleep(K_MSEC(500));
	}

	return 0;
}

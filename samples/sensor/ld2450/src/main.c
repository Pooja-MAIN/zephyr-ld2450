/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/sensor/ld2450.h>
#include <zephyr/kernel.h>

static const struct device *const radar = DEVICE_DT_GET_ONE(hilink_ld2450);

static int get_mm(enum sensor_channel chan, int *mm)
{
	struct sensor_value val;
	int rc = sensor_channel_get(radar, chan, &val);

	if (rc == 0) {
		*mm = (int)sensor_value_to_milli(&val);
	}
	return rc;
}

static void print_target(int index)
{
	int x;
	int y;
	int speed;

	if ((get_mm(SENSOR_CHAN_LD2450_TARGET_X(index), &x) != 0) ||
	    (get_mm(SENSOR_CHAN_LD2450_TARGET_Y(index), &y) != 0) ||
	    (get_mm(SENSOR_CHAN_LD2450_TARGET_SPEED(index), &speed) != 0)) {
		return;
	}

	printf("  target %d: x=%d mm  y=%d mm  speed=%d mm/s\n", index + 1, x, y, speed);
}

int main(void)
{
	struct sensor_value count;
	int nearest;
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

		if (get_mm(SENSOR_CHAN_DISTANCE, &nearest) == 0) {
			printf("  nearest: %d mm\n", nearest);
		}

		for (int i = 0; i < LD2450_TARGET_COUNT_MAX; i++) {
			print_target(i);
		}

		k_sleep(K_MSEC(500));
	}

	return 0;
}

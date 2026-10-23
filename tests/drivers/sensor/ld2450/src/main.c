/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/sensor/ld2450.h>
#include <zephyr/drivers/serial/uart_emul.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#define FRAME_SIZE 30

static const struct device *const radar = DEVICE_DT_GET(DT_NODELABEL(ld2450));
static const struct device *const uart = DEVICE_DT_GET(DT_NODELABEL(euart0));

/* Target 1: X = -782 mm, Y = 1713 mm, speed = -16 cm/s, resolution = 320 mm. */
static const uint8_t frame_one_target[FRAME_SIZE] = {
	0xAA, 0xFF, 0x03, 0x00,
	0x0E, 0x03, 0xB1, 0x86, 0x10, 0x00, 0x40, 0x01,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x55, 0xCC,
};

/* Adds target 2: X = 500 mm, Y = 1000 mm, speed = +25 cm/s, resolution = 360 mm. */
static const uint8_t frame_two_targets[FRAME_SIZE] = {
	0xAA, 0xFF, 0x03, 0x00,
	0x0E, 0x03, 0xB1, 0x86, 0x10, 0x00, 0x40, 0x01,
	0xF4, 0x81, 0xE8, 0x83, 0x19, 0x80, 0x68, 0x01,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x55, 0xCC,
};

static const uint8_t frame_empty[FRAME_SIZE] = {
	0xAA, 0xFF, 0x03, 0x00,
	[28] = 0x55, [29] = 0xCC,
};

static struct {
	struct k_work_delayable work;
	uint8_t buf[128];
	size_t len;
} feeder;

static void feed_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	uart_emul_put_rx_data(uart, feeder.buf, feeder.len);
}

/* Deliver bytes shortly after sample_fetch() has started waiting. */
static void feed_later(const uint8_t *data, size_t len)
{
	zassert_true(len <= sizeof(feeder.buf));
	memcpy(feeder.buf, data, len);
	feeder.len = len;
	k_work_schedule(&feeder.work, K_MSEC(20));
}

static int64_t get_milli(int chan)
{
	struct sensor_value val;

	zassert_ok(sensor_channel_get(radar, (enum sensor_channel)chan, &val), "channel %d", chan);
	return sensor_value_to_milli(&val);
}

static int32_t get_int(int chan)
{
	struct sensor_value val;

	zassert_ok(sensor_channel_get(radar, (enum sensor_channel)chan, &val), "channel %d", chan);
	return val.val1;
}

static void *ld2450_setup(void)
{
	zassert_true(device_is_ready(uart));
	zassert_true(device_is_ready(radar));
	k_work_init_delayable(&feeder.work, feed_handler);
	return NULL;
}

static void ld2450_before(void *fixture)
{
	ARG_UNUSED(fixture);
	k_work_cancel_delayable(&feeder.work);
	uart_emul_flush_rx_data(uart);
}

ZTEST(ld2450, test_one_target)
{
	struct sensor_value val;

	feed_later(frame_one_target, sizeof(frame_one_target));
	zassert_ok(sensor_sample_fetch(radar));

	zassert_equal(get_int(SENSOR_CHAN_LD2450_TARGET_COUNT), 1);
	zassert_equal(get_int(SENSOR_CHAN_PROX), 1);
	zassert_equal(get_milli(SENSOR_CHAN_LD2450_TARGET_X(0)), -782);
	zassert_equal(get_milli(SENSOR_CHAN_LD2450_TARGET_Y(0)), 1713);
	zassert_equal(get_milli(SENSOR_CHAN_LD2450_TARGET_SPEED(0)), -160);
	zassert_equal(get_milli(SENSOR_CHAN_LD2450_TARGET_RESOLUTION(0)), 320);
	zassert_equal(get_milli(SENSOR_CHAN_DISTANCE), 1883);

	zassert_equal(sensor_channel_get(radar, SENSOR_CHAN_LD2450_TARGET_X(1), &val), -ENODATA);
	zassert_equal(sensor_channel_get(radar, SENSOR_CHAN_LD2450_TARGET_X(2), &val), -ENODATA);
}

ZTEST(ld2450, test_two_targets_nearest_distance)
{
	feed_later(frame_two_targets, sizeof(frame_two_targets));
	zassert_ok(sensor_sample_fetch(radar));

	zassert_equal(get_int(SENSOR_CHAN_LD2450_TARGET_COUNT), 2);
	zassert_equal(get_milli(SENSOR_CHAN_LD2450_TARGET_X(1)), 500);
	zassert_equal(get_milli(SENSOR_CHAN_LD2450_TARGET_Y(1)), 1000);
	zassert_equal(get_milli(SENSOR_CHAN_LD2450_TARGET_SPEED(1)), 250);
	zassert_equal(get_milli(SENSOR_CHAN_LD2450_TARGET_RESOLUTION(1)), 360);
	/* Target 2 at 1.118 m is nearer than target 1 at 1.883 m. */
	zassert_equal(get_milli(SENSOR_CHAN_DISTANCE), 1118);
}

ZTEST(ld2450, test_no_targets)
{
	struct sensor_value val;

	feed_later(frame_empty, sizeof(frame_empty));
	zassert_ok(sensor_sample_fetch(radar));

	zassert_equal(get_int(SENSOR_CHAN_LD2450_TARGET_COUNT), 0);
	zassert_equal(get_int(SENSOR_CHAN_PROX), 0);
	zassert_equal(sensor_channel_get(radar, SENSOR_CHAN_DISTANCE, &val), -ENODATA);
}

ZTEST(ld2450, test_timeout_without_data)
{
	zassert_equal(sensor_sample_fetch(radar), -ETIMEDOUT);
}

ZTEST(ld2450, test_resync_after_noise_and_corrupt_frame)
{
	static const uint8_t noise[] = {0x00, 0x55, 0xCC, 0xAA, 0x12, 0xAA, 0xFF};
	uint8_t stream[sizeof(noise) + (2 * FRAME_SIZE)];

	memcpy(stream, noise, sizeof(noise));
	memcpy(&stream[sizeof(noise)], frame_two_targets, FRAME_SIZE);
	stream[sizeof(noise) + FRAME_SIZE - 1] = 0xCD; /* corrupt tail */
	memcpy(&stream[sizeof(noise) + FRAME_SIZE], frame_one_target, FRAME_SIZE);

	feed_later(stream, sizeof(stream));
	zassert_ok(sensor_sample_fetch(radar));

	/* Only the valid one-target frame may be reported. */
	zassert_equal(get_int(SENSOR_CHAN_LD2450_TARGET_COUNT), 1);
	zassert_equal(get_milli(SENSOR_CHAN_LD2450_TARGET_X(0)), -782);
}

ZTEST(ld2450, test_returns_fresh_frame)
{
	/* A frame received before the fetch call must not be returned. */
	uart_emul_put_rx_data(uart, frame_two_targets, sizeof(frame_two_targets));
	k_msleep(50);

	feed_later(frame_one_target, sizeof(frame_one_target));
	zassert_ok(sensor_sample_fetch(radar));

	zassert_equal(get_int(SENSOR_CHAN_LD2450_TARGET_COUNT), 1);
}

ZTEST(ld2450, test_unsupported_requests)
{
	struct sensor_value val;

	zassert_equal(sensor_sample_fetch_chan(radar, SENSOR_CHAN_PROX), -ENOTSUP);
	zassert_equal(sensor_channel_get(radar, SENSOR_CHAN_ACCEL_X, &val), -ENOTSUP);
}

ZTEST_SUITE(ld2450, NULL, ld2450_setup, ld2450_before, NULL, NULL);

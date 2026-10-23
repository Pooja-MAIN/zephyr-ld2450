/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT hilink_ld2450

#include <errno.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/sensor/ld2450.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/spinlock.h>

#include "ld2450_protocol.h"

LOG_MODULE_REGISTER(ld2450, CONFIG_SENSOR_LOG_LEVEL);

BUILD_ASSERT(LD2450_TARGET_COUNT_MAX == LD2450_MAX_TARGETS,
	     "Public and protocol target counts must match");

struct ld2450_config {
	const struct device *uart_dev;
	uart_irq_callback_user_data_t cb;
};

struct ld2450_data {
	/* Decoded data from the most recent sample_fetch() */
	struct ld2450_target targets[LD2450_MAX_TARGETS];
	uint8_t target_count;

	/* Serializes sample_fetch() and channel_get() callers */
	struct k_mutex lock;

	/* Receive state, owned by the UART ISR */
	uint8_t rx_buf[LD2450_FRAME_SIZE];
	uint8_t rx_pos;

	/* Latest complete frame, shared between the ISR and sample_fetch() */
	struct k_spinlock frame_lock;
	uint8_t frame[LD2450_FRAME_SIZE];
	struct k_sem frame_sem;
};

static void ld2450_rx_byte(struct ld2450_data *data, uint8_t byte)
{
	if (data->rx_pos < LD2450_HEADER_SIZE) {
		if (byte == ld2450_header_byte(data->rx_pos)) {
			data->rx_buf[data->rx_pos++] = byte;
		} else if (byte == LD2450_HEADER_0) {
			data->rx_buf[0] = byte;
			data->rx_pos = 1U;
		} else {
			data->rx_pos = 0U;
		}
		return;
	}

	data->rx_buf[data->rx_pos++] = byte;

	if (data->rx_pos < LD2450_FRAME_SIZE) {
		return;
	}

	data->rx_pos = 0U;

	if ((data->rx_buf[LD2450_FRAME_SIZE - 2U] != LD2450_TAIL_0) ||
	    (data->rx_buf[LD2450_FRAME_SIZE - 1U] != LD2450_TAIL_1)) {
		/* Lost bytes mid-frame; resynchronize on the next header. */
		return;
	}

	K_SPINLOCK(&data->frame_lock) {
		memcpy(data->frame, data->rx_buf, sizeof(data->frame));
	}

	k_sem_give(&data->frame_sem);
}

static void ld2450_uart_isr(const struct device *uart_dev, void *user_data)
{
	struct ld2450_data *data = user_data;
	uint8_t byte;

	uart_irq_update(uart_dev);

	if (uart_irq_rx_ready(uart_dev) <= 0) {
		return;
	}

	while (uart_fifo_read(uart_dev, &byte, 1) == 1) {
		ld2450_rx_byte(data, byte);
	}
}

static int ld2450_sample_fetch(const struct device *dev, enum sensor_channel chan)
{
	struct ld2450_data *data = dev->data;
	uint8_t frame[LD2450_FRAME_SIZE];
	int count;
	int rc;

	if (chan != SENSOR_CHAN_ALL) {
		return -ENOTSUP;
	}

	k_mutex_lock(&data->lock, K_FOREVER);

	/* Wait for a frame that completes after this call, so data is fresh. */
	k_sem_reset(&data->frame_sem);
	rc = k_sem_take(&data->frame_sem, K_MSEC(CONFIG_LD2450_UART_TIMEOUT_MS));
	if (rc != 0) {
		LOG_DBG("No complete frame within %d ms", CONFIG_LD2450_UART_TIMEOUT_MS);
		rc = -ETIMEDOUT;
		goto unlock;
	}

	K_SPINLOCK(&data->frame_lock) {
		memcpy(frame, data->frame, sizeof(frame));
	}

	count = ld2450_parse_frame(frame, data->targets);
	if (count < 0) {
		rc = -EBADMSG;
		goto unlock;
	}

	data->target_count = (uint8_t)count;

unlock:
	k_mutex_unlock(&data->lock);
	return rc;
}

static int ld2450_nearest_distance_mm(const struct ld2450_data *data, uint32_t *distance_mm)
{
	bool found = false;
	uint32_t nearest = UINT32_MAX;

	for (uint8_t i = 0U; i < LD2450_MAX_TARGETS; i++) {
		if (!ld2450_target_present(&data->targets[i])) {
			continue;
		}

		nearest = MIN(nearest, ld2450_target_distance_mm(&data->targets[i]));
		found = true;
	}

	if (!found) {
		return -ENODATA;
	}

	*distance_mm = nearest;
	return 0;
}

static int ld2450_target_channel_get(const struct ld2450_data *data, uint32_t chan,
				     struct sensor_value *val)
{
	uint32_t offset = chan - (uint32_t)SENSOR_CHAN_LD2450_TARGET1_X;
	uint32_t index = offset / LD2450_CHANNELS_PER_TARGET;
	uint32_t field = offset % LD2450_CHANNELS_PER_TARGET;
	const struct ld2450_target *target = &data->targets[index];

	if (!ld2450_target_present(target)) {
		return -ENODATA;
	}

	switch (field + (uint32_t)SENSOR_CHAN_LD2450_TARGET1_X) {
	case SENSOR_CHAN_LD2450_TARGET1_X:
		return sensor_value_from_milli(val, target->x_mm);
	case SENSOR_CHAN_LD2450_TARGET1_Y:
		return sensor_value_from_milli(val, target->y_mm);
	case SENSOR_CHAN_LD2450_TARGET1_SPEED:
		/* cm/s to mm/s, reported as m/s */
		return sensor_value_from_milli(val, (int64_t)target->speed_cm_s * 10);
	case SENSOR_CHAN_LD2450_TARGET1_RESOLUTION:
		return sensor_value_from_milli(val, target->resolution_mm);
	default:
		return -ENOTSUP;
	}
}

static int ld2450_channel_get(const struct device *dev, enum sensor_channel chan,
			      struct sensor_value *val)
{
	struct ld2450_data *data = dev->data;
	uint32_t distance_mm;
	int rc;

	k_mutex_lock(&data->lock, K_FOREVER);

	switch ((uint32_t)chan) {
	case SENSOR_CHAN_PROX:
		val->val1 = (data->target_count > 0U) ? 1 : 0;
		val->val2 = 0;
		rc = 0;
		break;
	case SENSOR_CHAN_DISTANCE:
		rc = ld2450_nearest_distance_mm(data, &distance_mm);
		if (rc == 0) {
			rc = sensor_value_from_milli(val, distance_mm);
		}
		break;
	case SENSOR_CHAN_LD2450_TARGET_COUNT:
		val->val1 = data->target_count;
		val->val2 = 0;
		rc = 0;
		break;
	default:
		if (((uint32_t)chan >= (uint32_t)SENSOR_CHAN_LD2450_TARGET1_X) &&
		    ((uint32_t)chan <= (uint32_t)SENSOR_CHAN_LD2450_TARGET3_RESOLUTION)) {
			rc = ld2450_target_channel_get(data, (uint32_t)chan, val);
		} else {
			rc = -ENOTSUP;
		}
		break;
	}

	k_mutex_unlock(&data->lock);
	return rc;
}

static DEVICE_API(sensor, ld2450_driver_api) = {
	.sample_fetch = ld2450_sample_fetch,
	.channel_get = ld2450_channel_get,
};

static int ld2450_init(const struct device *dev)
{
	const struct ld2450_config *config = dev->config;
	struct ld2450_data *data = dev->data;
	int rc;

	if (!device_is_ready(config->uart_dev)) {
		LOG_ERR_DEVICE_NOT_READY(config->uart_dev);
		return -ENODEV;
	}

	k_mutex_init(&data->lock);
	k_sem_init(&data->frame_sem, 0, 1);
	data->rx_pos = 0U;
	data->target_count = 0U;

	uart_irq_rx_disable(config->uart_dev);
	uart_irq_tx_disable(config->uart_dev);

	rc = uart_irq_callback_user_data_set(config->uart_dev, config->cb, data);
	if (rc != 0) {
		LOG_ERR("UART IRQ setup failed: %d", rc);
		return rc;
	}

	uart_irq_rx_enable(config->uart_dev);

	return 0;
}

#define LD2450_DEFINE(inst)                                                                        \
	static struct ld2450_data ld2450_data_##inst;                                              \
                                                                                                   \
	static const struct ld2450_config ld2450_config_##inst = {                                 \
		.uart_dev = DEVICE_DT_GET(DT_INST_BUS(inst)),                                      \
		.cb = ld2450_uart_isr,                                                             \
	};                                                                                         \
                                                                                                   \
	SENSOR_DEVICE_DT_INST_DEFINE(inst, ld2450_init, NULL, &ld2450_data_##inst,                 \
				     &ld2450_config_##inst, POST_KERNEL,                           \
				     CONFIG_SENSOR_INIT_PRIORITY, &ld2450_driver_api);

DT_INST_FOREACH_STATUS_OKAY(LD2450_DEFINE)

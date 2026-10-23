/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Header file for extended sensor API of Hi-Link LD2450 sensor
 * @ingroup ld2450_interface
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_SENSOR_LD2450_H_
#define ZEPHYR_INCLUDE_DRIVERS_SENSOR_LD2450_H_

/**
 * @brief Hi-Link LD2450 24 GHz multi-target tracking radar
 * @defgroup ld2450_interface Hi-Link LD2450
 * @ingroup sensor_interface_ext
 * @{
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <zephyr/drivers/sensor.h>

/** Maximum number of targets reported by the LD2450. */
#define LD2450_TARGET_COUNT_MAX 3

/**
 * @brief Custom sensor channels for LD2450.
 *
 * Coordinates follow the sensor's frame: Y is the distance in front of the
 * sensor and X is the lateral offset. Per-target channels return -ENODATA
 * when that target slot is empty.
 */
enum sensor_channel_ld2450 {
	/** Number of detected targets (0-3), in @c sensor_value.val1. */
	SENSOR_CHAN_LD2450_TARGET_COUNT = SENSOR_CHAN_PRIV_START,

	/** Target 1 lateral position in meters. */
	SENSOR_CHAN_LD2450_TARGET1_X,
	/** Target 1 forward position in meters. */
	SENSOR_CHAN_LD2450_TARGET1_Y,
	/** Target 1 radial speed in m/s; positive when moving away. */
	SENSOR_CHAN_LD2450_TARGET1_SPEED,
	/** Target 1 distance resolution in meters. */
	SENSOR_CHAN_LD2450_TARGET1_RESOLUTION,

	/** Target 2 lateral position in meters. */
	SENSOR_CHAN_LD2450_TARGET2_X,
	/** Target 2 forward position in meters. */
	SENSOR_CHAN_LD2450_TARGET2_Y,
	/** Target 2 radial speed in m/s; positive when moving away. */
	SENSOR_CHAN_LD2450_TARGET2_SPEED,
	/** Target 2 distance resolution in meters. */
	SENSOR_CHAN_LD2450_TARGET2_RESOLUTION,

	/** Target 3 lateral position in meters. */
	SENSOR_CHAN_LD2450_TARGET3_X,
	/** Target 3 forward position in meters. */
	SENSOR_CHAN_LD2450_TARGET3_Y,
	/** Target 3 radial speed in m/s; positive when moving away. */
	SENSOR_CHAN_LD2450_TARGET3_SPEED,
	/** Target 3 distance resolution in meters. */
	SENSOR_CHAN_LD2450_TARGET3_RESOLUTION,
};

/** Number of channels per target, used to index per-target channels. */
#define LD2450_CHANNELS_PER_TARGET                                                         \
	(SENSOR_CHAN_LD2450_TARGET2_X - SENSOR_CHAN_LD2450_TARGET1_X)

/**
 * @brief Get the X channel of a target.
 *
 * @param n Zero-based target index (0 to @ref LD2450_TARGET_COUNT_MAX - 1).
 */
#define SENSOR_CHAN_LD2450_TARGET_X(n)                                                     \
	((enum sensor_channel)(SENSOR_CHAN_LD2450_TARGET1_X + (n) * LD2450_CHANNELS_PER_TARGET))

/** @brief Get the Y channel of a target. */
#define SENSOR_CHAN_LD2450_TARGET_Y(n)                                                     \
	((enum sensor_channel)(SENSOR_CHAN_LD2450_TARGET1_Y + (n) * LD2450_CHANNELS_PER_TARGET))

/** @brief Get the speed channel of a target. */
#define SENSOR_CHAN_LD2450_TARGET_SPEED(n)                                                 \
	((enum sensor_channel)(SENSOR_CHAN_LD2450_TARGET1_SPEED +                          \
			       (n) * LD2450_CHANNELS_PER_TARGET))

/** @brief Get the distance resolution channel of a target. */
#define SENSOR_CHAN_LD2450_TARGET_RESOLUTION(n)                                            \
	((enum sensor_channel)(SENSOR_CHAN_LD2450_TARGET1_RESOLUTION +                     \
			       (n) * LD2450_CHANNELS_PER_TARGET))

#ifdef __cplusplus
}
#endif

/**
 * @}
 */

#endif /* ZEPHYR_INCLUDE_DRIVERS_SENSOR_LD2450_H_ */

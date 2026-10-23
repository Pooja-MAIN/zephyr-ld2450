/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Frame decoding for the Hi-Link LD2450 radar.
 *
 * This header has no Zephyr dependencies so that the decoding logic can be
 * unit tested on its own.
 *
 * Periodic report frame (30 bytes, little endian):
 *
 *   AA FF 03 00 | target 1 | target 2 | target 3 | 55 CC
 *
 * Each target is 8 bytes: X (mm), Y (mm), speed (cm/s), distance
 * resolution (mm). X, Y and speed use sign-magnitude encoding where the
 * most significant bit set means positive. An empty slot is all zeros.
 */

#ifndef ZEPHYR_DRIVERS_SENSOR_HILINK_LD2450_PROTOCOL_H_
#define ZEPHYR_DRIVERS_SENSOR_HILINK_LD2450_PROTOCOL_H_

#include <stdbool.h>
#include <stdint.h>

#define LD2450_FRAME_SIZE  30U
#define LD2450_HEADER_SIZE 4U
#define LD2450_MAX_TARGETS 3U
#define LD2450_TARGET_SIZE 8U

#define LD2450_HEADER_0 0xAAU
#define LD2450_HEADER_1 0xFFU
#define LD2450_HEADER_2 0x03U
#define LD2450_HEADER_3 0x00U
#define LD2450_TAIL_0   0x55U
#define LD2450_TAIL_1   0xCCU

struct ld2450_target {
	int16_t x_mm;
	int16_t y_mm;
	/* Positive when moving away from the sensor, negative when approaching. */
	int16_t speed_cm_s;
	uint16_t resolution_mm;
};

static inline uint8_t ld2450_header_byte(uint8_t index)
{
	static const uint8_t header[LD2450_HEADER_SIZE] = {
		LD2450_HEADER_0, LD2450_HEADER_1, LD2450_HEADER_2, LD2450_HEADER_3,
	};

	return header[index];
}

static inline int16_t ld2450_decode_signed(uint8_t low, uint8_t high)
{
	int16_t magnitude = (int16_t)(((uint16_t)(high & 0x7FU) << 8) | low);

	return ((high & 0x80U) != 0U) ? magnitude : (int16_t)-magnitude;
}

static inline bool ld2450_target_present(const struct ld2450_target *target)
{
	return (target->x_mm != 0) || (target->y_mm != 0);
}

/* Integer square root, rounded down. */
static inline uint32_t ld2450_isqrt(uint32_t value)
{
	uint32_t result = 0U;
	uint32_t bit = 1UL << 30;

	while (bit > value) {
		bit >>= 2;
	}

	while (bit != 0U) {
		if (value >= result + bit) {
			value -= result + bit;
			result = (result >> 1) + bit;
		} else {
			result >>= 1;
		}
		bit >>= 2;
	}

	return result;
}

static inline uint32_t ld2450_target_distance_mm(const struct ld2450_target *target)
{
	int32_t x = target->x_mm;
	int32_t y = target->y_mm;

	return ld2450_isqrt((uint32_t)(x * x) + (uint32_t)(y * y));
}

/*
 * Decode a complete 30-byte report frame.
 *
 * Returns the number of detected targets, or -1 if the frame header or tail
 * is invalid. Empty slots are stored as all-zero targets.
 */
static inline int ld2450_parse_frame(const uint8_t *frame,
				     struct ld2450_target targets[LD2450_MAX_TARGETS])
{
	int count = 0;

	for (uint8_t i = 0U; i < LD2450_HEADER_SIZE; i++) {
		if (frame[i] != ld2450_header_byte(i)) {
			return -1;
		}
	}

	if ((frame[LD2450_FRAME_SIZE - 2U] != LD2450_TAIL_0) ||
	    (frame[LD2450_FRAME_SIZE - 1U] != LD2450_TAIL_1)) {
		return -1;
	}

	for (uint8_t i = 0U; i < LD2450_MAX_TARGETS; i++) {
		const uint8_t *p = &frame[LD2450_HEADER_SIZE + (i * LD2450_TARGET_SIZE)];
		struct ld2450_target *t = &targets[i];

		t->x_mm = ld2450_decode_signed(p[0], p[1]);
		t->y_mm = ld2450_decode_signed(p[2], p[3]);
		t->speed_cm_s = ld2450_decode_signed(p[4], p[5]);
		t->resolution_mm = (uint16_t)(((uint16_t)p[7] << 8) | p[6]);

		if (ld2450_target_present(t)) {
			count++;
		}
	}

	return count;
}

#endif /* ZEPHYR_DRIVERS_SENSOR_HILINK_LD2450_PROTOCOL_H_ */

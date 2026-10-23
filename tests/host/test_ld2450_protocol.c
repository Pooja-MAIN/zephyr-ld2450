/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Host unit tests for the LD2450 frame decoder. Build and run with:
 *   gcc -std=c99 -Wall -Wextra -Werror -I../../drivers/sensor/hilink/ld2450 \
 *       test_ld2450_protocol.c -o test_ld2450_protocol && ./test_ld2450_protocol
 */

#include <stdio.h>
#include <string.h>

#include "ld2450_protocol.h"

static int failures;

#define CHECK_EQ(actual, expected)                                                         \
	do {                                                                               \
		long a_ = (long)(actual);                                                  \
		long e_ = (long)(expected);                                                \
		if (a_ != e_) {                                                            \
			printf("FAIL %s:%d: %s = %ld, expected %ld\n", __FILE__, __LINE__, \
			       #actual, a_, e_);                                           \
			failures++;                                                        \
		}                                                                          \
	} while (0)

/* Example report from the Hi-Link LD2450 protocol documentation. */
static const uint8_t one_target_frame[LD2450_FRAME_SIZE] = {
	0xAA, 0xFF, 0x03, 0x00,
	0x0E, 0x03, 0xB1, 0x86, 0x10, 0x00, 0x40, 0x01,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x55, 0xCC,
};

static void test_decode_signed(void)
{
	CHECK_EQ(ld2450_decode_signed(0x0E, 0x03), -782);
	CHECK_EQ(ld2450_decode_signed(0xB1, 0x86), 1713);
	CHECK_EQ(ld2450_decode_signed(0x00, 0x00), 0);
	CHECK_EQ(ld2450_decode_signed(0x00, 0x80), 0);
	CHECK_EQ(ld2450_decode_signed(0xFF, 0xFF), 32767);
	CHECK_EQ(ld2450_decode_signed(0xFF, 0x7F), -32767);
}

static void test_isqrt(void)
{
	CHECK_EQ(ld2450_isqrt(0U), 0);
	CHECK_EQ(ld2450_isqrt(1U), 1);
	CHECK_EQ(ld2450_isqrt(15U), 3);
	CHECK_EQ(ld2450_isqrt(16U), 4);
	CHECK_EQ(ld2450_isqrt(3545893U), 1883);
	CHECK_EQ(ld2450_isqrt(2147352578U), 46339);
	CHECK_EQ(ld2450_isqrt(0xFFFFFFFFU), 65535);
}

static void test_one_target(void)
{
	struct ld2450_target t[LD2450_MAX_TARGETS];

	CHECK_EQ(ld2450_parse_frame(one_target_frame, t), 1);
	CHECK_EQ(t[0].x_mm, -782);
	CHECK_EQ(t[0].y_mm, 1713);
	CHECK_EQ(t[0].speed_cm_s, -16);
	CHECK_EQ(t[0].resolution_mm, 320);
	CHECK_EQ(ld2450_target_distance_mm(&t[0]), 1883);
	CHECK_EQ(ld2450_target_present(&t[1]), 0);
	CHECK_EQ(ld2450_target_present(&t[2]), 0);
}

static void test_three_targets(void)
{
	struct ld2450_target t[LD2450_MAX_TARGETS];
	uint8_t frame[LD2450_FRAME_SIZE];

	memcpy(frame, one_target_frame, sizeof(frame));
	/* Target 2: X = +500 mm, Y = +1000 mm, speed = +25 cm/s (moving away). */
	frame[12] = 0xF4; frame[13] = 0x81;
	frame[14] = 0xE8; frame[15] = 0x83;
	frame[16] = 0x19; frame[17] = 0x80;
	frame[18] = 0x68; frame[19] = 0x01;
	/* Target 3: X = 0 mm, Y = +3000 mm, stationary. */
	frame[22] = 0xB8; frame[23] = 0x8B;

	CHECK_EQ(ld2450_parse_frame(frame, t), 3);
	CHECK_EQ(t[1].x_mm, 500);
	CHECK_EQ(t[1].y_mm, 1000);
	CHECK_EQ(t[1].speed_cm_s, 25);
	CHECK_EQ(t[1].resolution_mm, 360);
	CHECK_EQ(t[2].x_mm, 0);
	CHECK_EQ(t[2].y_mm, 3000);
	CHECK_EQ(t[2].speed_cm_s, 0);
	CHECK_EQ(ld2450_target_distance_mm(&t[2]), 3000);
}

static void test_empty_frame(void)
{
	struct ld2450_target t[LD2450_MAX_TARGETS];
	uint8_t frame[LD2450_FRAME_SIZE] = {0};

	frame[0] = 0xAA; frame[1] = 0xFF; frame[2] = 0x03; frame[3] = 0x00;
	frame[28] = 0x55; frame[29] = 0xCC;

	CHECK_EQ(ld2450_parse_frame(frame, t), 0);
}

static void test_bad_frames(void)
{
	struct ld2450_target t[LD2450_MAX_TARGETS];
	uint8_t frame[LD2450_FRAME_SIZE];

	memcpy(frame, one_target_frame, sizeof(frame));
	frame[2] = 0x02;
	CHECK_EQ(ld2450_parse_frame(frame, t), -1);

	memcpy(frame, one_target_frame, sizeof(frame));
	frame[29] = 0xCD;
	CHECK_EQ(ld2450_parse_frame(frame, t), -1);
}

int main(void)
{
	test_decode_signed();
	test_isqrt();
	test_one_target();
	test_three_targets();
	test_empty_frame();
	test_bad_frames();

	if (failures != 0) {
		printf("%d check(s) failed\n", failures);
		return 1;
	}

	printf("All LD2450 protocol tests passed\n");
	return 0;
}

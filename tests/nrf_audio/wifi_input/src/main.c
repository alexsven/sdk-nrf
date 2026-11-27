/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/ztest.h>

#define CONFIG_BT_BAP_BROADCAST_SOURCE 1
#include "audio_wifi.c"

K_MSGQ_DEFINE(test_queue, sizeof(struct net_buf *), 4, sizeof(void *));

static void reset_test(void *fixture)
{
	ARG_UNUSED(fixture);
	struct net_buf *frame;

	while (k_msgq_get(&test_queue, &frame, K_NO_WAIT) == 0) {
		net_buf_unref(frame);
	}
	output_queue = &test_queue;
	reset_buffer();
	playing = true;
	session_valid = false;
}

static void send_packet(uint32_t seq, int16_t left, int16_t right)
{
	uint8_t packet[PACKET_BYTES] = {'N', 'A', 'W', '1'};

	sys_put_be32(123, packet + 4);
	sys_put_be32(seq, packet + 8);
	for (size_t i = 0; i < PACKET_SAMPLES; i++) {
		sys_put_le16((uint16_t)left, packet + 12 + i * 4);
		sys_put_le16((uint16_t)right, packet + 14 + i * 4);
	}
	packet_receive(packet, sizeof(packet));
}

ZTEST(wifi_input, test_frame_and_metadata)
{
	for (uint32_t i = 0; i < 6; i++) {
		send_packet(i, -12000, 15000);
	}
	frame_submit();
	struct net_buf *frame;

	zassert_ok(k_msgq_get(&test_queue, &frame, K_NO_WAIT));
	struct audio_metadata *meta = net_buf_user_data(frame);

	zassert_equal(frame->len, 1920);
	zassert_equal(meta->data_len_us, 10000);
	zassert_equal(meta->bytes_per_location, 960);
	zassert_equal(meta->locations,
		      BT_AUDIO_LOCATION_FRONT_LEFT | BT_AUDIO_LOCATION_FRONT_RIGHT);
	zassert_true(meta->interleaved);
	for (size_t i = 0; i < FRAME_SAMPLES; i++) {
		zassert_equal((int16_t)sys_get_le16(frame->data + i * 4), -12000);
		zassert_equal((int16_t)sys_get_le16(frame->data + i * 4 + 2), 15000);
	}
	net_buf_unref(frame);
}

ZTEST(wifi_input, test_gap_fills_silence_and_late_packet_is_dropped)
{
	send_packet(0, 100, -100);
	send_packet(1, 100, -100);
	send_packet(3, 100, -100);
	send_packet(2, 200, -200);
	send_packet(4, 100, -100);
	send_packet(5, 100, -100);
	zassert_equal(sample_count, 6 * PACKET_SAMPLES);
	zassert_equal(ring[2 * PACKET_SAMPLES][0], 0);
	zassert_equal(ring[3 * PACKET_SAMPLES][0], 100);
}

ZTEST(wifi_input, test_invalid_packet_and_underrun)
{
	uint8_t invalid[PACKET_BYTES] = {0};

	packet_receive(invalid, sizeof(invalid));
	zassert_equal(sample_count, 0);
	frame_submit();
	struct net_buf *frame;

	zassert_ok(k_msgq_get(&test_queue, &frame, K_NO_WAIT));
	for (size_t i = 0; i < frame->len; i++) {
		zassert_equal(frame->data[i], 0);
	}
	net_buf_unref(frame);
}

ZTEST(wifi_input, test_clock_correction_and_sequence_wrap)
{
	send_packet(UINT32_MAX, -1000, 1000);
	send_packet(0, -1000, 1000);
	zassert_equal(sample_count, 2 * PACKET_SAMPLES);

	for (uint32_t i = 1; i < 8; i++) {
		send_packet(i, -1000, 1000);
	}
	size_t before = sample_count;

	frame_submit();
	struct net_buf *frame;

	zassert_ok(k_msgq_get(&test_queue, &frame, K_NO_WAIT));
	zassert_equal(sample_count, before - (FRAME_SAMPLES + 1));
	zassert_equal((int16_t)sys_get_le16(frame->data), -1000);
	net_buf_unref(frame);

	sample_count = PREFILL_SAMPLES - CORRECTION_MARGIN - 1;
	before = sample_count;
	frame_submit();
	zassert_ok(k_msgq_get(&test_queue, &frame, K_NO_WAIT));
	zassert_equal(sample_count, before - (FRAME_SAMPLES - 1));
	zassert_equal((int16_t)sys_get_le16(frame->data + 4), -1000);
	net_buf_unref(frame);
}

ZTEST(wifi_input, test_stop_discards_buffer_and_new_session_can_start)
{
	send_packet(0, 1, 2);
	frame_submit();
	zassert_equal(k_msgq_num_used_get(&test_queue), 1);
	audio_wifi_stop();
	zassert_equal(sample_count, 0);
	zassert_equal(k_msgq_num_used_get(&test_queue), 0);
	send_packet(1, 1, 2);
	zassert_equal(sample_count, 0);
	zassert_ok(audio_wifi_start(&test_queue));
	send_packet(0, 3, 4);
	zassert_equal(sample_count, PACKET_SAMPLES);
	zassert_equal(ring[0][0], 3);
}

ZTEST_SUITE(wifi_input, NULL, NULL, reset_test, NULL, NULL);

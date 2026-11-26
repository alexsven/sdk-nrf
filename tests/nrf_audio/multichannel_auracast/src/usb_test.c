/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/ztest.h>
#include "programmes.h"

/* Exercise the real receive callbacks without a physical USB controller. */
#include "audio_usb.c"

K_MSGQ_DEFINE(pcm_queue, sizeof(struct net_buf *), 4, sizeof(void *));

ZTEST(usb_input, test_four_channel_pcm_frame_assembly)
{
	audio_q_out = &pcm_queue;
	playing_state = true;
	ops.terminal_update_cb(NULL, TERMINAL_ID_HEADPHONES_OUT, true, false, NULL);
	ops.sof_cb(NULL, NULL);

	for (size_t millisecond = 0; millisecond < 10; millisecond++) {
		uint16_t *packet = ops.get_recv_buf(NULL, TERMINAL_ID_HEADPHONES_OUT, 384, NULL);

		zassert_not_null(packet);
		for (size_t sample = 0; sample < 48; sample++) {
			for (size_t channel = 0; channel < 4; channel++) {
				packet[sample * 4 + channel] =
					(millisecond * 48 + sample) * 4 + channel;
			}
		}
		ops.data_recv_cb(NULL, TERMINAL_ID_HEADPHONES_OUT, packet, 384, NULL);
		zassert_equal(k_msgq_num_used_get(&pcm_queue), millisecond == 9 ? 1 : 0);
	}

	struct net_buf *frame;

	zassert_ok(k_msgq_get(&pcm_queue, &frame, K_NO_WAIT));
	const struct audio_metadata *meta = net_buf_user_data(frame);

	zassert_equal(frame->len, 3840);
	zassert_equal(meta->locations, MULTICHANNEL_AUDIO_LOCATIONS);
	zassert_equal(meta->data_coding, PCM);
	zassert_equal(meta->data_len_us, 10000);
	zassert_equal(meta->bytes_per_location, 960);
	zassert_equal(meta->sample_rate_hz, 48000);
	zassert_equal(meta->carried_bits_per_sample, 16);
	zassert_true(meta->interleaved);
	for (size_t sample = 0; sample < 480 * 4; sample++) {
		zassert_equal(sys_get_le16(frame->data + sample * 2), sample);
	}
	net_buf_unref(frame);
}

ZTEST(usb_input, test_rejects_oversized_and_stereo_packets)
{
	audio_q_out = &pcm_queue;
	playing_state = true;
	ops.terminal_update_cb(NULL, TERMINAL_ID_HEADPHONES_OUT, true, false, NULL);
	zassert_is_null(ops.get_recv_buf(NULL, TERMINAL_ID_HEADPHONES_OUT, 392, NULL));
	void *packet = ops.get_recv_buf(NULL, TERMINAL_ID_HEADPHONES_OUT, 384, NULL);

	zassert_not_null(packet);
	ops.data_recv_cb(NULL, TERMINAL_ID_HEADPHONES_OUT, packet, 192, NULL);
	zassert_equal(k_msgq_num_used_get(&pcm_queue), 0);
	zassert_equal(k_mem_slab_num_used_get(&usb_in_slab), 0);
}

ZTEST_SUITE(usb_input, NULL, NULL, NULL, NULL, NULL);

/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/ztest.h>
#include <zephyr/fff.h>
#include <zephyr/sys/byteorder.h>
#include "programmes.h"
#include "sw_codec_select.h"
#include "sw_codec_lc3.h"

DEFINE_FFF_GLOBALS;
FAKE_VALUE_FUNC(int, sw_codec_lc3_single_rate_init, uint16_t, uint16_t, uint8_t *, uint32_t *,
		uint16_t);
FAKE_VALUE_FUNC(int, sw_codec_lc3_enc_init, uint16_t, uint8_t, uint16_t, uint32_t, uint8_t,
		uint16_t *);
FAKE_VALUE_FUNC(int, sw_codec_lc3_dec_init, uint16_t, uint8_t, uint16_t, uint8_t);
FAKE_VALUE_FUNC(int, sw_codec_lc3_enc_uninit_all);
FAKE_VALUE_FUNC(int, sw_codec_lc3_dec_uninit_all);
FAKE_VALUE_FUNC(int, sw_codec_lc3_uninit);

NET_BUF_POOL_FIXED_DEFINE(pcm_pool, 1, 3840, sizeof(struct audio_metadata), NULL);
NET_BUF_POOL_FIXED_DEFINE(lc3_pool, 1, 480, sizeof(struct audio_metadata), NULL);
static uint8_t encoded_channels;

int sw_codec_lc3_enc_run(const void *const pcm, uint32_t pcm_size, uint32_t bitrate,
			 uint8_t channel, uint16_t output_capacity, uint8_t *const output,
			 uint16_t *const written)
{
	zassert_equal(channel, encoded_channels++);
	zassert_equal(pcm_size, 960);
	zassert_equal(bitrate, 96000);
	zassert_equal(output_capacity, 480 - channel * 120);
	for (size_t sample = 0; sample < 480; sample++) {
		zassert_equal(sys_get_le16((const uint8_t *)pcm + sample * 2),
			      sample * 4 + channel);
	}
	memset(output, channel + 1, 120);
	*written = 120;
	return 0;
}

ZTEST(codec, test_four_independent_encoder_sessions)
{
	struct sw_codec_config config = {
		.sw_codec = SW_CODEC_LC3,
		.encoder =
			{
				.enabled = true,
				.bitrate = 96000,
				.num_ch = 4,
				.audio_loc = MULTICHANNEL_AUDIO_LOCATIONS,
				.sample_rate_hz = 48000,
			},
	};
	struct net_buf *pcm = net_buf_alloc(&pcm_pool, K_NO_WAIT);
	struct net_buf *encoded = net_buf_alloc(&lc3_pool, K_NO_WAIT);

	zassert_not_null(pcm);
	zassert_not_null(encoded);
	struct audio_metadata *pcm_meta = net_buf_user_data(pcm);
	struct audio_metadata *lc3_meta = net_buf_user_data(encoded);

	*pcm_meta = (struct audio_metadata){
		.data_coding = PCM,
		.data_len_us = 10000,
		.sample_rate_hz = 48000,
		.carried_bits_per_sample = 16,
		.bytes_per_location = 960,
		.interleaved = true,
		.locations = MULTICHANNEL_AUDIO_LOCATIONS,
	};
	*lc3_meta = *pcm_meta;
	lc3_meta->data_coding = LC3;
	lc3_meta->interleaved = false;
	lc3_meta->bytes_per_location = 120;
	lc3_meta->bitrate_bps = 96000;
	for (size_t sample = 0; sample < 480 * 4; sample++) {
		net_buf_add_le16(pcm, sample);
	}

	zassert_ok(sw_codec_init(config));
	zassert_equal(sw_codec_lc3_enc_init_fake.arg4_val, 4);
	zassert_equal(sw_codec_lc3_single_rate_init_fake.arg0_val, 48000);
	zassert_ok(sw_codec_encode(pcm, encoded));
	zassert_equal(encoded_channels, 4);
	zassert_equal(encoded->len, 480);
	for (size_t byte = 0; byte < encoded->len; byte++) {
		zassert_equal(encoded->data[byte], byte / 120 + 1);
	}
	zassert_ok(sw_codec_uninit(config));
	net_buf_unref(pcm);
	net_buf_unref(encoded);
}

ZTEST_SUITE(codec, NULL, NULL, NULL, NULL, NULL);

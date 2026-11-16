/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/ztest.h>
#include <audio_defines.h>
#include "broadcast_source.h"
#include "programmes.h"

NET_BUF_POOL_FIXED_DEFINE(encoded_pool, 1, 4 * 120, sizeof(struct audio_metadata), NULL);
static uint8_t sent_data[2][240];
static struct audio_metadata sent_meta[2];
static uint8_t calls;
static int send_error;

int broadcast_source_send(const struct net_buf *const audio_frame, uint8_t big_index,
			  uint8_t subgroup_index)
{
	zassert_equal(big_index, 0);
	zassert_equal(subgroup_index, calls);
	zassert_equal(audio_frame->len, 240);
	memcpy(sent_data[subgroup_index], audio_frame->data, audio_frame->len);
	sent_meta[subgroup_index] = *(struct audio_metadata *)net_buf_user_data(audio_frame);
	calls++;
	return subgroup_index == 0 ? send_error : 0;
}

static void before(void *fixture)
{
	ARG_UNUSED(fixture);
	calls = 0;
	send_error = 0;
}

static struct net_buf *encoded_frame(void)
{
	struct net_buf *frame = net_buf_alloc(&encoded_pool, K_NO_WAIT);

	zassert_not_null(frame);
	struct audio_metadata *meta = net_buf_user_data(frame);

	*meta = (struct audio_metadata){
		.data_coding = LC3,
		.data_len_us = 10000,
		.sample_rate_hz = 48000,
		.bitrate_bps = 96000,
		.bytes_per_location = 120,
		.locations = MULTICHANNEL_AUDIO_LOCATIONS,
		.ref_ts_us = 123456,
		.bad_data = BT_AUDIO_LOCATION_FRONT_LEFT | BT_AUDIO_LOCATION_BACK_RIGHT,
	};
	for (uint8_t channel = 0; channel < 4; channel++) {
		memset(net_buf_add(frame, 120), channel + 1, 120);
	}
	return frame;
}

ZTEST(programmes, test_channel_pairs_are_independent_stereo)
{
	struct net_buf *frame = encoded_frame();

	zassert_ok(programmes_send(frame));
	zassert_equal(calls, 2);
	for (size_t subgroup = 0; subgroup < 2; subgroup++) {
		for (size_t byte = 0; byte < sizeof(sent_data[subgroup]); byte++) {
			zassert_equal(sent_data[subgroup][byte], subgroup * 2 + byte / 120 + 1);
		}
		zassert_equal(sent_meta[subgroup].locations,
			      BT_AUDIO_LOCATION_FRONT_LEFT | BT_AUDIO_LOCATION_FRONT_RIGHT);
		zassert_equal(sent_meta[subgroup].bytes_per_location, 120);
		zassert_equal(sent_meta[subgroup].ref_ts_us, 123456);
		zassert_equal(sent_meta[subgroup].bad_data,
			      subgroup == 0 ? BT_AUDIO_LOCATION_FRONT_LEFT
					    : BT_AUDIO_LOCATION_FRONT_RIGHT);
	}
	zassert_equal(frame->len, 480);
	zassert_equal(((struct audio_metadata *)net_buf_user_data(frame))->locations,
		      MULTICHANNEL_AUDIO_LOCATIONS);
	net_buf_unref(frame);
}

ZTEST(programmes, test_first_subgroup_failure_does_not_skip_second)
{
	struct net_buf *frame = encoded_frame();

	send_error = -ECANCELED;
	zassert_equal(programmes_send(frame), -ECANCELED);
	zassert_equal(calls, 2);
	net_buf_unref(frame);
}

ZTEST(programmes, test_rejects_malformed_encoded_frames)
{
	struct net_buf *frame = encoded_frame();
	struct audio_metadata *meta = net_buf_user_data(frame);

	zassert_equal(programmes_send(NULL), -EINVAL);
	meta->locations = BT_AUDIO_LOCATION_FRONT_LEFT | BT_AUDIO_LOCATION_FRONT_RIGHT;
	zassert_equal(programmes_send(frame), -EINVAL);
	meta->locations = MULTICHANNEL_AUDIO_LOCATIONS;
	meta->interleaved = true;
	zassert_equal(programmes_send(frame), -EINVAL);
	meta->interleaved = false;
	meta->bytes_per_location = 121;
	zassert_equal(programmes_send(frame), -EINVAL);
	meta->bytes_per_location = 0;
	zassert_equal(programmes_send(frame), -EINVAL);
	meta->bytes_per_location = 120;
	meta->sample_rate_hz = 16000;
	zassert_equal(programmes_send(frame), -EINVAL);
	meta->sample_rate_hz = 48000;
	frame->len--;
	zassert_equal(programmes_send(frame), -EINVAL);
	zassert_equal(calls, 0);
	net_buf_unref(frame);
}

ZTEST_SUITE(programmes, NULL, NULL, before, NULL, NULL);

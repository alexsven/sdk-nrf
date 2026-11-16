/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/ztest.h>
#include <zephyr/fff.h>
#include "zbus_fakes/zbus_fakes.h"

/* Exercise private BASE and asynchronous stream callbacks without a controller. */
#include "broadcast_sink.c"

DEFINE_FFF_GLOBALS;
FAKE_VALUE_FUNC(int, bt_bap_broadcast_sink_stop, struct bt_bap_broadcast_sink *);
FAKE_VALUE_FUNC(int, bt_bap_broadcast_sink_delete, struct bt_bap_broadcast_sink *);
FAKE_VOID_FUNC(device_location_get, enum bt_audio_location *);

static unsigned int sync_calls;
static uint32_t synced_bises;

int bt_bap_broadcast_sink_sync(struct bt_bap_broadcast_sink *sink, uint32_t indexes_bitfield,
			      struct bt_bap_stream *streams[],
			      const uint8_t broadcast_code[BT_ISO_BROADCAST_CODE_SIZE])
{
	sync_calls++;
	synced_bises = indexes_bitfield;
	return 0;
}

#define SUBGROUP(bis_left, bis_right)                                                              \
	2, BT_HCI_CODING_FORMAT_LC3, 0, 0, 0, 0, 10,                                              \
	2, BT_AUDIO_CODEC_CFG_FREQ, BT_AUDIO_CODEC_CFG_FREQ_48KHZ,                                 \
	2, BT_AUDIO_CODEC_CFG_DURATION, BT_AUDIO_CODEC_CFG_DURATION_10,                            \
	3, BT_AUDIO_CODEC_CFG_FRAME_LEN, 120, 0, 0,                                               \
	bis_left, 6, 5, BT_AUDIO_CODEC_CFG_CHAN_ALLOC, 1, 0, 0, 0,                                 \
	bis_right, 6, 5, BT_AUDIO_CODEC_CFG_CHAN_ALLOC, 2, 0, 0, 0

static const uint8_t base_ad[] = {
	BT_UUID_16_ENCODE(BT_UUID_BASIC_AUDIO_VAL),
	0x40, 0x9c, 0x00, 2,
	SUBGROUP(1, 2),
	SUBGROUP(3, 4),
};
static struct bt_bap_broadcast_sink test_sink;
static struct bt_bap_ep endpoints[2];
static enum bt_audio_location test_location;

static void location_get(enum bt_audio_location *location)
{
	*location = test_location;
}

static void receive_base(void)
{
	struct bt_data ad = {
		.type = BT_DATA_SVC_DATA16,
		.data_len = sizeof(base_ad),
		.data = base_ad,
	};
	const struct bt_bap_base *base = bt_bap_base_get_base_from_ad(&ad);

	zassert_not_null(base);
	base_recv_cb(&test_sink, base, sizeof(base_ad) - BT_UUID_SIZE_16);
}

static void set_streaming(uint8_t count)
{
	init_routine_completed = true;
	sync_stream_cnt = count;
	for (uint8_t i = 0; i < count; i++) {
		audio_streams[i].ep = &endpoints[i];
		endpoints[i].state = BT_BAP_EP_STATE_STREAMING;
	}
}

static void stopped(uint8_t index)
{
	endpoints[index].state = BT_BAP_EP_STATE_IDLE;
	stream_stopped_cb(&audio_streams[index], BT_HCI_ERR_LOCALHOST_TERM_CONN);
}

static void before(void *fixture)
{
	ARG_UNUSED(fixture);
	RESET_FAKE(bt_bap_broadcast_sink_stop);
	RESET_FAKE(bt_bap_broadcast_sink_delete);
	sync_calls = 0;
	synced_bises = 0;
	RESET_FAKE(device_location_get);
	RESET_FAKE(zbus_chan_pub);
	device_location_get_fake.custom_fake = location_get;
	test_location = BT_AUDIO_LOCATION_FRONT_LEFT | BT_AUDIO_LOCATION_FRONT_RIGHT;
	broadcast_sink = NULL;
	zassert_ok(broadcast_sink_cleanup());
	broadcast_sink = &test_sink;
	paused = false;
	sync_stream_cnt = 0;
	memset(audio_streams, 0, sizeof(audio_streams));
	memset(endpoints, 0, sizeof(endpoints));
}

ZTEST(subgroups, test_switch_waits_for_both_bises_and_keeps_stereo_metadata)
{
	struct bt_iso_biginfo biginfo = {0};

	receive_base();
	zassert_equal(bis_index_bitfield, BIT(0) | BIT(1));
	set_streaming(2);
	zassert_ok(broadcast_sink_subgroup_toggle());
	zassert_equal(bt_bap_broadcast_sink_stop_fake.call_count, 1);
	zassert_equal(broadcast_sink_subgroup_toggle(), -EBUSY);
	syncable_cb(&test_sink, &biginfo);
	zassert_equal(sync_calls, 0);
	receive_base();
	zassert_equal(selected_subgroup, 0);
	stopped(0);
	syncable_cb(&test_sink, &biginfo);
	zassert_equal(sync_calls, 0);
	receive_base();
	zassert_equal(selected_subgroup, 0);
	stopped(1);
	receive_base();
	zassert_equal(selected_subgroup, 1);
	zassert_equal(bis_index_bitfield, BIT(2) | BIT(3));
	zassert_equal(audio_codec_info[0].frequency, 48000);
	zassert_equal(audio_codec_info[1].bitrate, 96000);
	zassert_equal(audio_codec_info[0].chan_allocation, BT_AUDIO_LOCATION_FRONT_LEFT);
	zassert_equal(audio_codec_info[1].chan_allocation, BT_AUDIO_LOCATION_FRONT_RIGHT);

	syncable_cb(&test_sink, &biginfo);
	zassert_equal(synced_bises, BIT(2) | BIT(3));
	set_streaming(2);
	zassert_ok(broadcast_sink_subgroup_toggle());
	stopped(0);
	stopped(1);
	receive_base();
	zassert_equal(selected_subgroup, 0);
	zassert_equal(bis_index_bitfield, BIT(0) | BIT(1));
}

ZTEST(subgroups, test_paused_right_receiver_switches_without_starting)
{
	test_location = BT_AUDIO_LOCATION_FRONT_RIGHT;
	receive_base();
	paused = true;
	zassert_ok(broadcast_sink_subgroup_toggle());
	receive_base();
	zassert_equal(bis_index_bitfield, BIT(3));
	zassert_equal(audio_codec_info[0].chan_allocation, BT_AUDIO_LOCATION_FRONT_RIGHT);
	uint8_t count;
	enum bt_audio_location locations;

	zassert_ok(le_audio_concurrent_sync_num_get(&count, &locations));
	zassert_equal(locations, BT_AUDIO_LOCATION_FRONT_RIGHT);
	struct bt_iso_biginfo biginfo = {0};

	syncable_cb(&test_sink, &biginfo);
	zassert_equal(sync_calls, 0);
	zassert_equal(bt_bap_broadcast_sink_stop_fake.call_count, 0);
	zassert_true(paused);
}

ZTEST(subgroups, test_missing_subgroup_and_stop_failure_preserve_selection)
{
	zassert_equal(broadcast_sink_subgroup_toggle(), -EAGAIN);
	receive_base();
	set_streaming(2);
	subgroup_count = 1;
	zassert_equal(broadcast_sink_subgroup_toggle(), -ENOENT);
	zassert_equal(bt_bap_broadcast_sink_stop_fake.call_count, 0);
	subgroup_count = 2;
	bt_bap_broadcast_sink_stop_fake.return_val = -EIO;
	zassert_equal(broadcast_sink_subgroup_toggle(), -EIO);
	zassert_equal(selected_subgroup, 0);
	zassert_equal(requested_subgroup, UINT8_MAX);
	zassert_false(subgroup_switch_pending);
	zassert_true(base_config_ready);
	zassert_equal(bis_index_bitfield, BIT(0) | BIT(1));
}

ZTEST_SUITE(subgroups, NULL, NULL, before, NULL, NULL);

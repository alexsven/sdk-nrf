/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/ztest.h>
#include <zephyr/fff.h>
#include <zephyr/bluetooth/audio/lc3.h>
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

#define SUBGROUP(bis, a, b, c)                                                                    \
	1, BT_HCI_CODING_FORMAT_LC3, 0, 0, 0, 0, 10,                                              \
	2, BT_AUDIO_CODEC_CFG_FREQ, BT_AUDIO_CODEC_CFG_FREQ_48KHZ,                                 \
	2, BT_AUDIO_CODEC_CFG_DURATION, BT_AUDIO_CODEC_CFG_DURATION_10,                            \
	3, BT_AUDIO_CODEC_CFG_FRAME_LEN, 120, 0,                                                   \
	5, 4, BT_AUDIO_METADATA_TYPE_LANG, a, b, c,                                               \
	bis, 6, 5, BT_AUDIO_CODEC_CFG_CHAN_ALLOC, 0, 0, 0, 0

static const uint8_t base_ad[] = {
	BT_UUID_16_ENCODE(BT_UUID_BASIC_AUDIO_VAL),
	0x40, 0x9c, 0x00, 4,
	SUBGROUP(1, 'e', 'n', 'g'),
	SUBGROUP(2, 's', 'p', 'a'),
	SUBGROUP(3, 'f', 'r', 'a'),
	SUBGROUP(4, 'c', 'm', 'n'),
};
static const uint8_t single_bis_base_ad[] = {
	BT_UUID_16_ENCODE(BT_UUID_BASIC_AUDIO_VAL),
	0x40, 0x9c, 0x00, 1,
	1, BT_HCI_CODING_FORMAT_LC3, 0, 0, 0, 0, 10,
	2, BT_AUDIO_CODEC_CFG_FREQ, BT_AUDIO_CODEC_CFG_FREQ_48KHZ,
	2, BT_AUDIO_CODEC_CFG_DURATION, BT_AUDIO_CODEC_CFG_DURATION_10,
	3, BT_AUDIO_CODEC_CFG_FRAME_LEN, 120, 0, 0,
	1, 6, 5, BT_AUDIO_CODEC_CFG_CHAN_ALLOC, 1, 0, 0, 0,
};
static const uint8_t nonsequential_base_ad[] = {
	BT_UUID_16_ENCODE(BT_UUID_BASIC_AUDIO_VAL),
	0x40, 0x9c, 0x00, 4,
	SUBGROUP(4, 'e', 'n', 'g'),
	SUBGROUP(3, 's', 'p', 'a'),
	SUBGROUP(2, 'f', 'r', 'a'),
	SUBGROUP(1, 'c', 'm', 'n'),
};
#define BASE_WITH_COMMON_LOCATION(location)                                                        \
	BT_UUID_16_ENCODE(BT_UUID_BASIC_AUDIO_VAL),                                                 \
	0x40, 0x9c, 0x00, 1,                                                                     \
	1, BT_HCI_CODING_FORMAT_LC3, 0, 0, 0, 0, 16,                                            \
	2, BT_AUDIO_CODEC_CFG_FREQ, BT_AUDIO_CODEC_CFG_FREQ_48KHZ,                                 \
	2, BT_AUDIO_CODEC_CFG_DURATION, BT_AUDIO_CODEC_CFG_DURATION_10,                            \
	3, BT_AUDIO_CODEC_CFG_FRAME_LEN, 120, 0,                                                  \
	5, BT_AUDIO_CODEC_CFG_CHAN_ALLOC, location, 0, 0, 0,                                      \
	0, 1, 0

static const uint8_t inherited_mono_base_ad[] = {BASE_WITH_COMMON_LOCATION(1)};
static const uint8_t inherited_stereo_base_ad[] = {BASE_WITH_COMMON_LOCATION(3)};
static const uint8_t malformed_allocation_base_ad[] = {
	BT_UUID_16_ENCODE(BT_UUID_BASIC_AUDIO_VAL),
	0x40, 0x9c, 0x00, 1,
	1, BT_HCI_CODING_FORMAT_LC3, 0, 0, 0, 0, 10,
	2, BT_AUDIO_CODEC_CFG_FREQ, BT_AUDIO_CODEC_CFG_FREQ_48KHZ,
	2, BT_AUDIO_CODEC_CFG_DURATION, BT_AUDIO_CODEC_CFG_DURATION_10,
	3, BT_AUDIO_CODEC_CFG_FRAME_LEN, 120, 0, 0,
	1, 5, 4, BT_AUDIO_CODEC_CFG_CHAN_ALLOC, 1, 0, 0,
};
static struct bt_bap_broadcast_sink test_sink;
static struct bt_bap_ep endpoints[2];
static enum bt_audio_location test_location;
static struct audio_metadata received_meta;
static unsigned int received_count;
NET_BUF_POOL_FIXED_DEFINE(test_audio_pool, 1, 120, 0, NULL);

static void location_get(enum bt_audio_location *location)
{
	*location = test_location;
}

static void receive_base_data(const uint8_t *data, size_t size)
{
	struct bt_data ad = {
		.type = BT_DATA_SVC_DATA16,
		.data_len = size,
		.data = data,
	};
	const struct bt_bap_base *base = bt_bap_base_get_base_from_ad(&ad);

	zassert_not_null(base);
	base_recv_cb(&test_sink, base, size - BT_UUID_SIZE_16);
}

static void receive_base(void)
{
	receive_base_data(base_ad, sizeof(base_ad));
}

static void capture_receive(struct net_buf *audio_frame, struct audio_metadata *meta,
			    uint8_t location_index)
{
	received_meta = *meta;
	received_count++;
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
	received_count = 0;
	receive_cb = capture_receive;
}

ZTEST(subgroups, test_cycles_all_four_mono_streams)
{
	struct bt_iso_biginfo biginfo = {0};

	test_location = BT_AUDIO_LOCATION_FRONT_RIGHT;
	receive_base();
	zassert_equal(bis_index_bitfield, BIT(0));
	for (int i = 0; i < 4; i++) {
		set_streaming(1);
		zassert_ok(broadcast_sink_stream_next());
		zassert_equal(broadcast_sink_stream_next(), -EBUSY);
		syncable_cb(&test_sink, &biginfo);
		zassert_equal(sync_calls, i);
		receive_base();
		zassert_equal(bis_index_bitfield, BIT(i));
		stopped(0);
		receive_base();
		zassert_equal(selected_subgroup, (i + 1) % 4);
		zassert_equal(bis_index_bitfield, BIT((i + 1) % 4));
		zassert_equal(audio_codec_info[0].frequency, 48000);
		zassert_equal(audio_codec_info[0].bitrate, 96000);
		zassert_equal(audio_codec_info[0].chan_allocation, BT_AUDIO_LOCATION_FRONT_RIGHT);
		syncable_cb(&test_sink, &biginfo);
		zassert_equal(synced_bises, BIT((i + 1) % 4));
	}
}

ZTEST(subgroups, test_paused_receiver_switches_without_starting)
{
	test_location = BT_AUDIO_LOCATION_FRONT_LEFT;
	receive_base();
	paused = true;
	zassert_ok(broadcast_sink_stream_next());
	receive_base();
	zassert_equal(bis_index_bitfield, BIT(1));
	zassert_equal(audio_codec_info[0].chan_allocation, BT_AUDIO_LOCATION_FRONT_LEFT);
	uint8_t count;
	enum bt_audio_location locations;

	zassert_ok(le_audio_concurrent_sync_num_get(&count, &locations));
	zassert_equal(locations, BT_AUDIO_LOCATION_FRONT_LEFT);
	struct bt_iso_biginfo biginfo = {0};

	syncable_cb(&test_sink, &biginfo);
	zassert_equal(sync_calls, 0);
	zassert_equal(bt_bap_broadcast_sink_stop_fake.call_count, 0);
	zassert_true(paused);
}

ZTEST(subgroups, test_missing_stream_and_stop_failure_preserve_selection)
{
	zassert_equal(broadcast_sink_stream_next(), -EAGAIN);
	receive_base_data(single_bis_base_ad, sizeof(single_bis_base_ad));
	set_streaming(1);
	zassert_equal(broadcast_sink_stream_next(), -ENOENT);
	zassert_equal(bt_bap_broadcast_sink_stop_fake.call_count, 0);
	init_routine_completed = false;
	receive_base();
	set_streaming(1);
	bt_bap_broadcast_sink_stop_fake.return_val = -EIO;
	zassert_equal(broadcast_sink_stream_next(), -EIO);
	zassert_equal(selected_subgroup, 0);
	zassert_equal(requested_subgroup, 0);
	zassert_false(stream_switch_pending);
	zassert_true(base_config_ready);
	zassert_equal(bis_index_bitfield, BIT(0));
}

ZTEST(subgroups, test_selected_mono_packet_routes_to_receiver_location)
{
	struct bt_audio_codec_cfg codec = BT_AUDIO_CODEC_LC3_CONFIG(
		BT_AUDIO_CODEC_CFG_FREQ_48KHZ, BT_AUDIO_CODEC_CFG_DURATION_10,
		BT_AUDIO_LOCATION_FRONT_LEFT, 120, 1, BT_AUDIO_CONTEXT_TYPE_MEDIA);
	struct bt_iso_recv_info info = {.flags = BT_ISO_FLAGS_VALID};
	struct net_buf *buf = net_buf_alloc(&test_audio_pool, K_NO_WAIT);

	zassert_not_null(buf);
	net_buf_add(buf, 120);
	test_location = BT_AUDIO_LOCATION_FRONT_RIGHT;
	receive_base();
	audio_streams[0].codec_cfg = &codec;
	stream_recv_cb(&audio_streams[0], &info, buf);
	zassert_equal(received_count, 1);
	zassert_equal(received_meta.locations, BT_AUDIO_LOCATION_FRONT_RIGHT);
	zassert_equal(received_meta.bad_data, 0);
	info.flags = 0;
	stream_recv_cb(&audio_streams[0], &info, buf);
	zassert_equal(received_count, 2);
	zassert_equal(received_meta.bad_data, BT_AUDIO_LOCATION_FRONT_RIGHT);
	test_location = BT_AUDIO_LOCATION_FRONT_LEFT;
	codec = (struct bt_audio_codec_cfg)BT_AUDIO_CODEC_LC3_CONFIG(
		BT_AUDIO_CODEC_CFG_FREQ_48KHZ, BT_AUDIO_CODEC_CFG_DURATION_10,
		BT_AUDIO_LOCATION_FRONT_RIGHT, 120, 1, BT_AUDIO_CONTEXT_TYPE_MEDIA);
	receive_base();
	info.flags = BT_ISO_FLAGS_VALID;
	stream_recv_cb(&audio_streams[0], &info, buf);
	zassert_equal(received_meta.locations, BT_AUDIO_LOCATION_FRONT_LEFT);
	zassert_equal(received_meta.bad_data, 0);
	net_buf_pull(buf, 1);
	stream_recv_cb(&audio_streams[0], &info, buf);
	zassert_equal(received_meta.bad_data, BT_AUDIO_LOCATION_FRONT_LEFT);
	codec = (struct bt_audio_codec_cfg)BT_AUDIO_CODEC_LC3_CONFIG(
		BT_AUDIO_CODEC_CFG_FREQ_48KHZ, BT_AUDIO_CODEC_CFG_DURATION_10,
		BT_AUDIO_LOCATION_MONO_AUDIO, 120, 1, BT_AUDIO_CONTEXT_TYPE_MEDIA);
	stream_recv_cb(&audio_streams[0], &info, buf);
	zassert_equal(received_meta.locations, BT_AUDIO_LOCATION_FRONT_LEFT);
	zassert_equal(received_meta.bad_data, BT_AUDIO_LOCATION_FRONT_LEFT);
	net_buf_unref(buf);
}

ZTEST(subgroups, test_selection_uses_subgroup_position_not_global_bis_number)
{
	receive_base_data(nonsequential_base_ad, sizeof(nonsequential_base_ad));
	zassert_equal(bis_index_bitfield, BIT(3));
	paused = true;
	zassert_ok(broadcast_sink_stream_next());
	receive_base_data(nonsequential_base_ad, sizeof(nonsequential_base_ad));
	zassert_equal(bis_index_bitfield, BIT(2));
	zassert_ok(broadcast_sink_stream_next());
	receive_base_data(nonsequential_base_ad, sizeof(nonsequential_base_ad));
	zassert_equal(bis_index_bitfield, BIT(1));
}

ZTEST(subgroups, test_inherited_channel_allocation_must_be_mono)
{
	receive_base_data(malformed_allocation_base_ad, sizeof(malformed_allocation_base_ad));
	zassert_false(base_config_ready);
	zassert_equal(bis_index_bitfield, 0);
	receive_base_data(inherited_stereo_base_ad, sizeof(inherited_stereo_base_ad));
	zassert_false(base_config_ready);
	zassert_equal(bis_index_bitfield, 0);
	receive_base_data(inherited_mono_base_ad, sizeof(inherited_mono_base_ad));
	zassert_true(base_config_ready);
	zassert_equal(bis_index_bitfield, BIT(0));
}

ZTEST_SUITE(subgroups, NULL, NULL, before, NULL, NULL);

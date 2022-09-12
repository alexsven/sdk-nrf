/*
 * Copyright (c) 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "le_audio.h"

#include <bluetooth/bluetooth.h>
#include <bluetooth/audio/audio.h>
/* TODO: Remove when a get_info function is implemented in host */
#include <../subsys/bluetooth/audio/endpoint.h>

#include "macros_common.h"
#include "ctrl_events.h"
#include "audio_datapath.h"

#include <logging/log.h>
LOG_MODULE_REGISTER(bis_gateway, CONFIG_LOG_BLE_LEVEL);

BUILD_ASSERT(CONFIG_BT_AUDIO_BROADCAST_SRC_STREAM_COUNT <= 2,
	     "A maximum of two streams are currently supported");

#define HCI_ISO_BUF_ALLOC_PER_CHAN 2

/* For being able to dynamically define iso_tx_pools */
#define NET_BUF_POOL_ITERATE(i, _)                                                                 \
	NET_BUF_POOL_FIXED_DEFINE(iso_tx_pool_##i, HCI_ISO_BUF_ALLOC_PER_CHAN,                     \
				  BT_ISO_SDU_BUF_SIZE(CONFIG_BT_ISO_TX_MTU), 8, NULL);
#define NET_BUF_POOL_PTR_ITERATE(i, ...) IDENTITY(&iso_tx_pool_##i)
LISTIFY(CONFIG_BT_ISO_MAX_CHAN, NET_BUF_POOL_ITERATE, (;))

/* clang-format off */
static struct net_buf_pool *iso_tx_pools[] = { LISTIFY(CONFIG_BT_ISO_MAX_CHAN,
						       NET_BUF_POOL_PTR_ITERATE, (,)) };
/* clang-format on */

static struct bt_audio_broadcast_source *broadcast_source;

static struct bt_audio_stream streams[CONFIG_BT_AUDIO_BROADCAST_SRC_STREAM_COUNT];
//static struct bt_audio_stream *streams_p[ARRAY_SIZE(streams)];

static struct bt_audio_lc3_preset lc3_preset = BT_AUDIO_LC3_BROADCAST_PRESET_48_2_1(
	BT_AUDIO_LOCATION_FRONT_LEFT, BT_AUDIO_CONTEXT_TYPE_MEDIA);

struct bt_le_ext_adv *adv;

static atomic_t iso_tx_pool_alloc[CONFIG_BT_ISO_MAX_CHAN];
static bool delete_broadcast_src;
static uint32_t seq_num[CONFIG_BT_ISO_MAX_CHAN];

static bool is_iso_buffer_full(uint8_t idx)
{
	/* net_buf_alloc allocates buffers for APP->NET transfer over HCI RPMsg,
	 * but when these buffers are released it is not guaranteed that the
	 * data has actually been sent. The data might be qued on the NET core,
	 * and this can cause delays in the audio.
	 * When stream_sent_cb() is called the data has been sent.
	 * Data will be discarded if allocation becomes too high, to avoid audio delays.
	 * If the NET and APP core operates in clock sync, discarding should not occur.
	 */

	if (atomic_get(&iso_tx_pool_alloc[idx]) >= HCI_ISO_BUF_ALLOC_PER_CHAN) {
		return true;
	}

	return false;
}

static int get_stream_index(struct bt_audio_stream *stream, uint8_t *index)
{
	for (size_t i = 0U; i < ARRAY_SIZE(streams); i++) {
		if (&streams[i] == stream) {
			*index = i;
			return 0;
		}
	}

	LOG_WRN("Stream %p not found", (void *)stream);

	return -EINVAL;
}

static void stream_sent_cb(struct bt_audio_stream *stream)
{
	static uint32_t sent_cnt[ARRAY_SIZE(streams)];
	uint8_t index;

	get_stream_index(stream, &index);

	if (atomic_get(&iso_tx_pool_alloc[index])) {
		atomic_dec(&iso_tx_pool_alloc[index]);
	} else {
		LOG_WRN("Decreasing atomic variable for stream %u failed", index);
	}

	sent_cnt[index]++;

	if ((sent_cnt[index] % 1000U) == 0U) {
		LOG_DBG("Sent %u total ISO packets on stream %u", sent_cnt[index], index);
	}
}

static void stream_started_cb(struct bt_audio_stream *stream)
{
	int ret;
	uint8_t index;

	get_stream_index(stream, &index);
	seq_num[index] = 0;

	ret = ctrl_events_le_audio_event_send(LE_AUDIO_EVT_STREAMING);
	ERR_CHK(ret);

	LOG_INF("Broadcast source %p started", (void *)stream);
}

static void stream_stopped_cb(struct bt_audio_stream *stream)
{
	int ret;

	ret = ctrl_events_le_audio_event_send(LE_AUDIO_EVT_NOT_STREAMING);
	ERR_CHK(ret);

	LOG_INF("Broadcast source %p stopped", (void *)stream);

	if (delete_broadcast_src && broadcast_source != NULL) {
		ret = bt_audio_broadcast_source_delete(broadcast_source);
		if (ret) {
			LOG_ERR("Unable to delete broadcast source %p", (void *)stream);
			delete_broadcast_src = false;
			return;
		}

		broadcast_source = NULL;

		LOG_INF("Broadcast source %p deleted", (void *)stream);

		delete_broadcast_src = false;
	}
}

static struct bt_audio_stream_ops stream_ops = { .sent = stream_sent_cb,
						 .started = stream_started_cb,
						 .stopped = stream_stopped_cb };

static void adv_create(void)
{
	int ret;

	/* Broadcast Audio Streaming Endpoint advertising data */
	NET_BUF_SIMPLE_DEFINE(ad_buf, BT_UUID_SIZE_16 + BT_AUDIO_BROADCAST_ID_SIZE);
	NET_BUF_SIMPLE_DEFINE(base_buf, 128);
	struct bt_data ext_ad[2];
	struct bt_data per_ad;
	uint32_t broadcast_id;
	char name[] = "SQ Stream";
	char name2[] = "HQ Stream";

	/* Create a non-connectable non-scannable advertising set */
	ret = bt_le_ext_adv_create(BT_LE_EXT_ADV_NCONN_NAME, NULL, &adv);
	if (ret != 0) {
		LOG_ERR("Unable to create extended advertising set: %d", ret);
		return;
	}

	/* Set periodic advertising parameters */
	ret = bt_le_per_adv_set_param(adv, BT_LE_PER_ADV_DEFAULT);
	if (ret) {
		LOG_ERR("Failed to set periodic advertising parameters (ret %d)", ret);
		return;
	}
	ret = bt_audio_broadcast_source_get_id(broadcast_source, &broadcast_id);
	if (ret != 0) {
		LOG_ERR("Unable to get broadcast ID: %d", ret);
		return;
	}

	/* Setup extended advertising data */
	net_buf_simple_add_le16(&ad_buf, BT_UUID_BROADCAST_AUDIO_VAL);
	net_buf_simple_add_le24(&ad_buf, broadcast_id);

	ext_ad[0].type = 0x30;

	if (lc3_preset.qos.sdu == 100) {
		LOG_WRN("HQ broadcast");
		ext_ad[0].data = name2;
		ext_ad[0].data_len = strlen(name2) + sizeof(ext_ad[0].type);
	} else if (lc3_preset.qos.sdu == 60) {
		LOG_WRN("SQ broadcast");
		ext_ad[0].data = name;
		ext_ad[0].data_len = strlen(name) + sizeof(ext_ad[0].type);
	}

	ext_ad[1].type = BT_DATA_SVC_DATA16;
	ext_ad[1].data_len = ad_buf.len + sizeof(ext_ad[1].type);
	ext_ad[1].data = ad_buf.data;

	ret = bt_le_ext_adv_set_data(adv, ext_ad, ARRAY_SIZE(ext_ad), NULL, 0);
	if (ret != 0) {
		LOG_ERR("Failed to set extended advertising data: %d", ret);
		return;
	}

	/* Setup periodic advertising data */
	ret = bt_audio_broadcast_source_get_base(broadcast_source, &base_buf);
	if (ret != 0) {
		LOG_ERR("Failed to get encoded BASE: %d", ret);
		return;
	}

	per_ad.type = BT_DATA_SVC_DATA16;
	per_ad.data_len = base_buf.len;
	per_ad.data = base_buf.data;

	ret = bt_le_per_adv_set_data(adv, &per_ad, 1);
	if (ret != 0) {
		LOG_ERR("Failed to set periodic advertising data: %d", ret);
		return;
	}
}

static void initialize(void)
{
	int ret;
	static bool initialized;
	struct bt_audio_broadcast_source_stream_param stream_params[ARRAY_SIZE(streams)];
	struct bt_audio_broadcast_source_subgroup_param subgroup_param;
	struct bt_audio_broadcast_source_create_param create_param;

	(void)memset(streams, 0, sizeof(streams));

	if (!initialized) {
		for (size_t i = 0U; i < ARRAY_SIZE(stream_params); i++) {
			stream_params[i].stream = &streams[i];
			bt_audio_stream_cb_register(stream_params[i].stream, &stream_ops);
		}

		subgroup_param.stream_count = 1U;
		subgroup_param.stream_params = stream_params;
		subgroup_param.codec = &lc3_preset.codec;

		create_param.subgroup_count = 1U;
		create_param.subgroup_params = &subgroup_param;
		create_param.qos = &lc3_preset.qos;

		ret = bt_audio_broadcast_source_create(&create_param, &broadcast_source);
		if (ret != 0) {
			LOG_ERR("Unable to create broadcast source: %d", ret);
			ERR_CHK(ret);
		}

		/* Create advertising set */
		adv_create(0);

		initialized = true;
	}
}

int le_audio_config_get(uint32_t *bitrate, uint32_t *sampling_rate)
{
	LOG_WRN("Not possible to get config on broadcast source");
	return -ENXIO;
}

int le_audio_volume_up(void)
{
	LOG_WRN("Not possible to increase volume on/from broadcast source");
	return -ENXIO;
}

int le_audio_volume_down(void)
{
	LOG_WRN("Not possible to decrease volume on/from broadcast source");
	return -ENXIO;
}

int le_audio_volume_mute(void)
{
	LOG_WRN("Not possible to mute volume on/from broadcast source");
	return -ENXIO;
}

int le_audio_play(void)
{
	int ret;

	ret = bt_audio_broadcast_source_start(broadcast_source, adv);
	if (ret) {
		LOG_WRN("Failed to start broadcast, ret: %d", ret);
	}

	return ret;
}

int le_audio_pause(void)
{
	int ret;

	ret = bt_audio_broadcast_source_stop(broadcast_source);
	if (ret) {
		LOG_WRN("Failed to stop broadcast, ret: %d", ret);
	}

	return ret;
}

int le_audio_send(uint8_t const *const data, size_t size)
{
	int ret;
	static bool wrn_printed[CONFIG_BT_ISO_MAX_CHAN];
	struct net_buf *buf;
	size_t num_streams = ARRAY_SIZE(streams);
	size_t data_size = size / num_streams;

	for (size_t i = 0U; i < num_streams; i++) {
		if (streams[i].ep->status.state != BT_AUDIO_EP_STATE_STREAMING) {
			LOG_DBG("Stream %d not in streaming state", i);
			continue;
		}

		if (is_iso_buffer_full(i)) {
			if (!wrn_printed[i]) {
				LOG_WRN("HCI ISO TX overrun on ch %d - Single print", i);
				wrn_printed[i] = true;
			}

			return -ENOMEM;
		}

		wrn_printed[i] = false;

		buf = net_buf_alloc(iso_tx_pools[i], K_NO_WAIT);
		if (buf == NULL) {
			/* This should never occur because of the is_iso_buffer_full() check */
			LOG_WRN("Out of TX buffers");
			return -ENOMEM;
		}

		net_buf_reserve(buf, BT_ISO_CHAN_SEND_RESERVE);
		net_buf_add_mem(buf, &data[i * data_size], data_size);

		atomic_inc(&iso_tx_pool_alloc[i]);

		ret = bt_audio_stream_send(&streams[i], buf, seq_num[i]++, BT_ISO_TIMESTAMP_NONE);
		if (ret < 0) {
			LOG_WRN("Failed to send audio data: %d", ret);
			net_buf_unref(buf);
			atomic_dec(&iso_tx_pool_alloc[i]);
			return ret;
		}
	}

#if (CONFIG_AUDIO_SOURCE_I2S)
	struct bt_iso_tx_info tx_info = { 0 };

	ret = bt_iso_chan_get_tx_sync(streams[0].iso, &tx_info);

	if (ret) {
		LOG_DBG("Error getting ISO TX anchor point: %d", ret);
	} else {
		audio_datapath_sdu_ref_update(tx_info.ts);
	}
#endif

	return 0;
}

int le_audio_enable(le_audio_receive_cb recv_cb)
{
	int ret;

	initialize();

	LOG_DBG("Creating broadcast source");

	/* Start extended advertising */
	ret = bt_le_ext_adv_start(adv, BT_LE_EXT_ADV_START_DEFAULT);
	if (ret) {
		LOG_ERR("Failed to start extended advertising: %d", ret);
		return ret;
	}

	/* Enable Periodic Advertising */
	ret = bt_le_per_adv_start(adv);
	if (ret) {
		LOG_ERR("Failed to enable periodic advertising: %d", ret);
		return ret;
	}

	LOG_DBG("Starting broadcast source");

	ret = bt_audio_broadcast_source_start(broadcast_source, adv);
	if (ret) {
		LOG_ERR("Failed to start broadcast source: %d", ret);
		return ret;
	}

	LOG_DBG("LE Audio enabled");

	return 0;
}

int le_audio_disable(void)
{
	int ret;

	if (streams[0].ep->status.state == BT_AUDIO_EP_STATE_STREAMING) {
		/* Deleting broadcast source in stream_stopped_cb() */
		delete_broadcast_src = true;

		ret = bt_audio_broadcast_source_stop(broadcast_source);
		if (ret) {
			return ret;
		}
	} else if (broadcast_source != NULL) {
		ret = bt_audio_broadcast_source_delete(broadcast_source);
		if (ret) {
			return ret;
		}

		broadcast_source = NULL;
	}

	LOG_DBG("LE Audio disabled");

	return 0;
}

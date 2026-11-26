/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "streamctrl.h"
#include "programmes.h"
#include "metadata_usb.h"
#include "audio_system.h"
#include "broadcast_source.h"
#include "bt_mgmt.h"
#include "fw_info_app.h"
#include "led.h"
#include "led_assignments.h"
#include "macros_common.h"
#include "peripherals.h"
#include "zbus_common.h"

#include <zephyr/sys/atomic.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/logging/log.h>

#if defined(CONFIG_AUDIO_PERFORMANCE_DIAGNOSTICS)
#include <nrfx.h>
#endif

LOG_MODULE_REGISTER(main, CONFIG_MAIN_LOG_LEVEL);

BUILD_ASSERT(CONFIG_AUDIO_INPUT_CHANNELS == 4 && CONFIG_AUDIO_ENCODE_CHANNELS_MAX == 4);
BUILD_ASSERT(CONFIG_LC3_ENC_CHAN_MAX >= 4);
BUILD_ASSERT(CONFIG_BT_ISO_MAX_BIG == 1);
BUILD_ASSERT(CONFIG_BT_BAP_BROADCAST_SRC_SUBGROUP_COUNT == 4);
BUILD_ASSERT(CONFIG_BT_BAP_BROADCAST_SRC_STREAM_COUNT == 4);
BUILD_ASSERT(CONFIG_BT_AUDIO_CONCURRENT_TX_STREAMS_MAX >= 4);
BUILD_ASSERT(CONFIG_BT_ISO_MAX_CHAN >= 4 && CONFIG_BT_ISO_TX_BUF_COUNT >= 4);
BUILD_ASSERT(CONFIG_BT_CTLR_ADV_ISO_STREAM_COUNT >= 4 && CONFIG_BT_CTLR_ADV_ISO_STREAM_MAX >= 4);
BUILD_ASSERT(IS_ENABLED(CONFIG_BT_BAP_BROADCAST_48_4_1));
BUILD_ASSERT(CONFIG_AUDIO_FRAME_DURATION_US == 10000);
BUILD_ASSERT(!IS_ENABLED(CONFIG_AUDIO_TEST_TONE) && !IS_ENABLED(CONFIG_MONO_TO_ALL_RECEIVERS));
BUILD_ASSERT(sizeof(CONFIG_BT_AUDIO_BROADCAST_NAME) <= BROADCAST_SOURCE_ADV_NAME_MAX);
BUILD_ASSERT(DT_PROP(DT_NODELABEL(cpuapp), clock_frequency) == 256000000,
	     "Four-channel encoding requires the 256 MHz CPU configuration");
BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(zephyr_uac2_audio_streaming) == 1 &&
		     DT_PROP(DT_NODELABEL(hp_out_terminal), front_left) &&
		     DT_PROP(DT_NODELABEL(hp_out_terminal), front_right) &&
		     DT_PROP(DT_NODELABEL(hp_out_terminal), back_left) &&
		     DT_PROP(DT_NODELABEL(hp_out_terminal), back_right),
	     "Use the multichannel_auracast/usb.overlay playback-only descriptors");

ZBUS_MSG_SUBSCRIBER_DEFINE(le_audio_evt_sub);
ZBUS_CHAN_DECLARE(le_audio_chan);
ZBUS_CHAN_DECLARE(bt_mgmt_chan);
ZBUS_CHAN_DECLARE(sdu_ref_chan);
ZBUS_OBS_DECLARE(sdu_ref_msg_listen);

static atomic_t strm_state = ATOMIC_INIT(STATE_PAUSED);

NET_BUF_SIMPLE_DEFINE_STATIC(uuid_data, 128);
NET_BUF_SIMPLE_DEFINE_STATIC(base_data, 256);
static struct bt_data ext_adv_buf[CONFIG_EXT_ADV_BUF_MAX];
static struct bt_data per_adv_buf[1];
#if defined(CONFIG_AURACAST)
static uint8_t
	pba_data[BROADCAST_SOURCE_PBA_HEADER_SIZE + CONFIG_BT_AUDIO_BROADCAST_PBA_METADATA_SIZE];
#endif
static struct broadcast_source_ext_adv_data ext_adv_data = {
	.uuid_buf = &uuid_data,
#if defined(CONFIG_AURACAST)
	.pba_buf = pba_data,
#endif
};
static struct broadcast_source_per_adv_data per_adv_data = {.base_buf = &base_data};

uint8_t stream_state_get(void)
{
	return atomic_get(&strm_state);
}

void streamctrl_send(const struct net_buf *const audio_frame)
{
	if (stream_state_get() == STATE_STREAMING) {
		int ret = programmes_send(audio_frame);

		if (ret) {
			LOG_WRN_RATELIMIT("Four-channel send failed: %d", ret);
		}
	}
}

static void bt_mgmt_evt_handler(const struct zbus_channel *chan)
{
	const struct bt_mgmt_msg *msg = zbus_chan_const_msg(chan);

	if (msg->event != BT_MGMT_EXT_ADV_WITH_PA_READY || msg->index != 0) {
		LOG_WRN("Unexpected Bluetooth management event: %u, index %u", msg->event,
			msg->index);
		return;
	}

	int ret = broadcast_source_start(0, msg->ext_adv);

	ERR_CHK_MSG(ret, "Failed to start four-BIS broadcaster");
}

ZBUS_LISTENER_DEFINE(bt_mgmt_evt_listen, bt_mgmt_evt_handler);

static int broadcast_create(void)
{
	static enum bt_audio_location location = BT_AUDIO_LOCATION_MONO_AUDIO;
	static struct subgroup_config subgroups[4];
	static struct broadcast_source_big broadcast_param = {
		.subgroups = subgroups,
		.num_subgroups = ARRAY_SIZE(subgroups),
		.packing = BT_ISO_PACKING_SEQUENTIAL,
	};
	const struct bt_bap_lc3_preset preset = BT_BAP_LC3_BROADCAST_PRESET_NRF_AUDIO;

	for (size_t i = 0; i < ARRAY_SIZE(subgroups); i++) {
		subgroups[i].location = &location;
		subgroups[i].num_bises = 1;
		subgroups[i].context = BT_AUDIO_CONTEXT_TYPE_MEDIA;
		subgroups[i].group_lc3_preset = preset;

		int ret = programmes_metadata_set(&subgroups[i].group_lc3_preset.codec_cfg, i);

		if (ret < 0) {
			LOG_ERR("Failed to set programme %u metadata: %d", i, ret);
			return ret;
		}
	}

	return broadcast_source_enable(&broadcast_param, 0);
}

static int advertising_start(void)
{
	memcpy(ext_adv_data.brdcst_name_buf, CONFIG_BT_AUDIO_BROADCAST_NAME,
	       sizeof(CONFIG_BT_AUDIO_BROADCAST_NAME));
	ext_adv_buf[0].type = BT_DATA_UUID16_ALL;
	ext_adv_buf[0].data = uuid_data.data;

	int ret = bt_mgmt_manufacturer_uuid_populate(&uuid_data, CONFIG_BT_DEVICE_MANUFACTURER_ID);

	if (ret) {
		return ret;
	}

	ret = broadcast_source_ext_adv_populate(0, false, 0, &ext_adv_data, &ext_adv_buf[1],
						ARRAY_SIZE(ext_adv_buf) - 1);
	if (ret < 0) {
		return ret;
	}

	size_t ext_adv_count = ret + 1;

	ext_adv_buf[0].data_len = uuid_data.len;
	ret = broadcast_source_per_adv_populate(0, &per_adv_data, per_adv_buf,
						ARRAY_SIZE(per_adv_buf));
	if (ret < 0) {
		return ret;
	}

	/* Account for the advertising data length and type bytes as well as the BASE. */
	if (base_data.len + 2 > CONFIG_BT_CTLR_ADV_DATA_LEN_MAX) {
		LOG_ERR("BASE advertising data too large: %u > %u", base_data.len + 2,
			CONFIG_BT_CTLR_ADV_DATA_LEN_MAX);
		return -EMSGSIZE;
	}

	LOG_INF("Advertising four 48 kHz mono language programmes (BASE %u bytes)", base_data.len);
	ret = bt_mgmt_adv_start(0, ext_adv_buf, ext_adv_count, per_adv_buf, ret, false);
	if (ret == 0) {
		metadata_usb_publish();
	}
	return ret;
}

int main(void)
{
#if defined(CONFIG_AUDIO_PERFORMANCE_DIAGNOSTICS)
	/* Zephyr assigns OSCILLATORS to the non-secure peripheral alias during boot. */
	uint32_t current_freq = NRF_OSCILLATORS_NS->PLL.CURRENTFREQ;

	LOG_INF("PERF CPU configured %u Hz, PLL requested/current 0x%x/0x%x (256 MHz = 0x%x)",
		DT_PROP(DT_NODELABEL(cpuapp), clock_frequency),
		NRF_OSCILLATORS_NS->PLL.FREQ, current_freq,
		OSCILLATORS_PLL_CURRENTFREQ_CURRENTFREQ_CK256M);
#endif
	int ret = peripherals_init();

	ERR_CHK(ret);
	ret = fw_info_app_print();
	ERR_CHK(ret);
	ret = bt_mgmt_init();
	ERR_CHK(ret);
	ret = metadata_usb_init(&base_data);
	ERR_CHK_MSG(ret, "Failed to initialize USB metadata interface");
	ret = audio_system_init();
	ERR_CHK(ret);
	ret = audio_system_encoder_num_ch_set(MULTICHANNEL_AUDIO_LOCATIONS);
	ERR_CHK(ret);
	ret = audio_system_config_set(48000, CONFIG_BT_AUDIO_BITRATE_BROADCAST_SRC, VALUE_NOT_SET);
	ERR_CHK(ret);

	ret = zbus_chan_add_obs(&le_audio_chan, &le_audio_evt_sub, ZBUS_ADD_OBS_TIMEOUT_MS);
	ERR_CHK(ret);
	ret = zbus_chan_add_obs(&bt_mgmt_chan, &bt_mgmt_evt_listen, ZBUS_ADD_OBS_TIMEOUT_MS);
	ERR_CHK(ret);
	ret = zbus_chan_add_obs(&sdu_ref_chan, &sdu_ref_msg_listen, ZBUS_ADD_OBS_TIMEOUT_MS);
	ERR_CHK(ret);

	ret = broadcast_create();
	ERR_CHK_MSG(ret, "Failed to create four-subgroup broadcast");
	ret = advertising_start();
	ERR_CHK_MSG(ret, "Failed to start advertising");

	uint8_t streaming_bises = 0;
	bool audio_started = false;

	while (true) {
		const struct zbus_channel *chan;
		struct le_audio_msg msg;

		ret = zbus_sub_wait_msg(&le_audio_evt_sub, &chan, &msg, K_FOREVER);
		ERR_CHK(ret);

		if (msg.event != LE_AUDIO_EVT_STREAMING &&
		    msg.event != LE_AUDIO_EVT_NOT_STREAMING) {
			if (msg.event != LE_AUDIO_EVT_STREAM_SENT) {
				LOG_WRN("Unexpected LE Audio event: %u", msg.event);
			}
			continue;
		}

		if (msg.idx.lvl1 != 0 || msg.idx.lvl2 >= 4 || msg.idx.lvl3 != 0) {
			LOG_ERR("Invalid BIG/subgroup/BIS event index: %u/%u/%u", msg.idx.lvl1,
				msg.idx.lvl2, msg.idx.lvl3);
			continue;
		}

		uint8_t bis_bit = BIT(msg.idx.lvl2);

		if (msg.event == LE_AUDIO_EVT_STREAMING) {
			streaming_bises |= bis_bit;
			if (streaming_bises == BIT_MASK(4) &&
			    stream_state_get() != STATE_STREAMING) {
				if (!audio_started) {
					audio_system_start();
					audio_started = true;
				}
				atomic_set(&strm_state, STATE_STREAMING);
				audio_system_encoder_start();
				ret = led_blink(LED_AUDIO_CONN_STATUS);
				ERR_CHK(ret);
				LOG_INF("Streaming English, Spanish, French and Mandarin");
			}
		} else {
			streaming_bises &= ~bis_bit;
			atomic_set(&strm_state, STATE_PAUSED);
			audio_system_encoder_stop();
			if (streaming_bises == 0 && audio_started) {
				audio_system_stop();
				audio_started = false;
				ret = led_on(LED_AUDIO_CONN_STATUS);
				ERR_CHK(ret);
			}
		}
	}
}

/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "programmes.h"
#include "broadcast_source.h"

#include <audio_defines.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(programmes, LOG_LEVEL_INF);

NET_BUF_POOL_FIXED_DEFINE(programme_pool, 1, CONFIG_BT_ISO_TX_MTU,
			  sizeof(struct audio_metadata), NULL);

int programmes_metadata_set(struct bt_audio_codec_cfg *codec_cfg, uint8_t subgroup)
{
	static const struct {
		const char *name;
		uint8_t language[BT_AUDIO_LANG_SIZE];
	} programmes[] = {
		{"English", {'e', 'n', 'g'}},
		{"Spanish", {'s', 'p', 'a'}},
		{"French", {'f', 'r', 'a'}},
		{"Mandarin", {'c', 'm', 'n'}},
	};

	if (codec_cfg == NULL || subgroup >= ARRAY_SIZE(programmes)) {
		LOG_ERR("Invalid programme metadata arguments");
		return -EINVAL;
	}

	int ret = bt_audio_codec_cfg_meta_set_program_info(
		codec_cfg, (const uint8_t *)programmes[subgroup].name,
		strlen(programmes[subgroup].name));

	if (ret < 0) {
		LOG_ERR("Failed to set programme %u name: %d", subgroup, ret);
		return ret;
	}

	ret = bt_audio_codec_cfg_meta_set_lang(codec_cfg, programmes[subgroup].language);
	if (ret < 0) {
		LOG_ERR("Failed to set programme %u language: %d", subgroup, ret);
		return ret;
	}

	return 0;
}

int programmes_send(const struct net_buf *audio_frame)
{
	if (audio_frame == NULL) {
		LOG_ERR("Missing encoded audio frame");
		return -EINVAL;
	}

	const struct audio_metadata *meta = net_buf_user_data(audio_frame);

	if (meta->data_coding != LC3 || meta->interleaved ||
	    meta->locations != MULTICHANNEL_AUDIO_LOCATIONS || meta->sample_rate_hz != 48000 ||
	    meta->data_len_us != 10000 || meta->bytes_per_location == 0 ||
	    meta->bytes_per_location > CONFIG_BT_ISO_TX_MTU ||
	    audio_frame->len != 4 * meta->bytes_per_location) {
		LOG_ERR("Invalid four-channel LC3 frame");
		return -EINVAL;
	}

	int first_error = 0;
	static const enum bt_audio_location input_locations[] = {
		BT_AUDIO_LOCATION_FRONT_LEFT, BT_AUDIO_LOCATION_FRONT_RIGHT,
		BT_AUDIO_LOCATION_BACK_LEFT, BT_AUDIO_LOCATION_BACK_RIGHT,
	};

	for (uint8_t subgroup = 0; subgroup < ARRAY_SIZE(input_locations); subgroup++) {
		struct net_buf *programme = net_buf_alloc(&programme_pool, K_NO_WAIT);

		if (programme == NULL) {
			LOG_ERR("Could not allocate mono programme buffer");
			return -ENOMEM;
		}

		struct audio_metadata *programme_meta = net_buf_user_data(programme);

		*programme_meta = *meta;
		/* Use a single internal location so the bad-data bit remains representable. */
		programme_meta->locations = BT_AUDIO_LOCATION_FRONT_LEFT;
		programme_meta->bad_data = (meta->bad_data & input_locations[subgroup])
						   ? BT_AUDIO_LOCATION_FRONT_LEFT
						   : 0;
		net_buf_add_mem(programme, audio_frame->data + subgroup * meta->bytes_per_location,
				meta->bytes_per_location);

		/* The TX path copies each BIS into its own ISO buffer before returning. */
		int ret = broadcast_source_send(programme, 0, subgroup);

		net_buf_unref(programme);
		if (ret) {
			LOG_WRN_RATELIMIT("Programme %u send failed: %d", subgroup, ret);
			if (first_error == 0) {
				first_error = ret;
			}
		}
	}

	return first_error;
}

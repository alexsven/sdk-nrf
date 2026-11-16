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

NET_BUF_POOL_FIXED_DEFINE(programme_pool, 1, 2 * CONFIG_BT_ISO_TX_MTU,
			  sizeof(struct audio_metadata), NULL);

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
	const size_t stereo_size = 2 * meta->bytes_per_location;

	for (uint8_t subgroup = 0; subgroup < 2; subgroup++) {
		struct net_buf *programme = net_buf_alloc(&programme_pool, K_NO_WAIT);

		if (programme == NULL) {
			LOG_ERR("Could not allocate stereo programme buffer");
			return -ENOMEM;
		}

		struct audio_metadata *programme_meta = net_buf_user_data(programme);

		*programme_meta = *meta;
		programme_meta->locations =
			BT_AUDIO_LOCATION_FRONT_LEFT | BT_AUDIO_LOCATION_FRONT_RIGHT;
		uint32_t left =
			subgroup == 0 ? BT_AUDIO_LOCATION_FRONT_LEFT : BT_AUDIO_LOCATION_BACK_LEFT;
		uint32_t right = subgroup == 0 ? BT_AUDIO_LOCATION_FRONT_RIGHT
					       : BT_AUDIO_LOCATION_BACK_RIGHT;

		programme_meta->bad_data =
			((meta->bad_data & left) ? BT_AUDIO_LOCATION_FRONT_LEFT : 0) |
			((meta->bad_data & right) ? BT_AUDIO_LOCATION_FRONT_RIGHT : 0);
		net_buf_add_mem(programme, audio_frame->data + subgroup * stereo_size, stereo_size);

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

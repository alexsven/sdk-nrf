/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef _BCT_TEST_
#define _BCT_TEST_

#include <zephyr/bluetooth/audio/cap.h>

#define ADV_NAME_MAX (28)
#define NAME_LEN     sizeof(CONFIG_TARGET_BROADCAST_NAME) + 1

struct bct_test_values_subgroup {
	uint8_t phy;
	bool framed;
	uint8_t rtn;
	uint8_t sdu_size;
	uint8_t mtl_ms;
	uint32_t frame_interval_us;
	uint32_t pd_us;
	uint32_t sampling_rate_hz;
	uint32_t bitrate_bps;
	uint32_t frame_duration_us;
	uint8_t octets_per_frame;
	char language[4]; /* Allow space for '\0' */
	enum bt_audio_context context;
	/* Program info : News bool immediate_rendering_flag; */
	uint8_t num_bis;
	enum bt_audio_location location[CONFIG_BT_BAP_BROADCAST_SNK_STREAM_COUNT];
};

struct bct_test_values_big {
	struct bct_test_values_subgroup *subgroups;
	uint8_t num_subgroups;
	uint8_t packing;
	bool encryption;
	uint8_t broadcast_code[BT_AUDIO_BROADCAST_CODE_SIZE + 1];
	char adv_name[ADV_NAME_MAX + 1];
	char broadcast_name[NAME_LEN];
	uint32_t broadcast_id;
};

#endif /* _BCT_TEST_ */

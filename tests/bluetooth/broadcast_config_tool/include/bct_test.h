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
	uint32_t sampling_rate_hz;  /* Done */
	uint32_t frame_duration_us; /* Done */
	uint8_t octets_per_frame;   /* Done */
	char language[4];	    /* Allow space for '\0' */
	enum bt_audio_context context;
	/* Program info : News bool immediate_rendering_flag; */
	uint8_t num_bis;
	enum bt_audio_location locations[CONFIG_BT_BAP_BROADCAST_SNK_STREAM_COUNT];
};

struct bct_test_values_big {
	struct bct_test_values_subgroup *subgroups;
	uint8_t num_subgroups; /* Done */
	uint8_t framing;
	uint32_t pd_us;	 /* Done */
	bool encryption; /* Done */
	uint8_t broadcast_code[BT_AUDIO_BROADCAST_CODE_SIZE + 1];
	char adv_name[ADV_NAME_MAX + 1];       /* Done */
	char broadcast_name[ADV_NAME_MAX + 1]; /* Done */
	uint32_t broadcast_id;		       /* Done */
};

static inline char *chan_location_bit_to_str(enum bt_audio_location chan_allocation)
{
	switch (chan_allocation) {
	case BT_AUDIO_LOCATION_MONO_AUDIO:
		return "Mono";
	case BT_AUDIO_LOCATION_FRONT_LEFT:
		return "Front left";
	case BT_AUDIO_LOCATION_FRONT_RIGHT:
		return "Front right";
	case BT_AUDIO_LOCATION_FRONT_CENTER:
		return "Front center";
	case BT_AUDIO_LOCATION_LOW_FREQ_EFFECTS_1:
		return "Low frequency effects 1";
	case BT_AUDIO_LOCATION_BACK_LEFT:
		return "Back left";
	case BT_AUDIO_LOCATION_BACK_RIGHT:
		return "Back right";
	case BT_AUDIO_LOCATION_FRONT_LEFT_OF_CENTER:
		return "Front left of center";
	case BT_AUDIO_LOCATION_FRONT_RIGHT_OF_CENTER:
		return "Front right of center";
	case BT_AUDIO_LOCATION_BACK_CENTER:
		return "Back center";
	case BT_AUDIO_LOCATION_LOW_FREQ_EFFECTS_2:
		return "Low frequency effects 2";
	case BT_AUDIO_LOCATION_SIDE_LEFT:
		return "Side left";
	case BT_AUDIO_LOCATION_SIDE_RIGHT:
		return "Side right";
	case BT_AUDIO_LOCATION_TOP_FRONT_LEFT:
		return "Top front left";
	case BT_AUDIO_LOCATION_TOP_FRONT_RIGHT:
		return "Top front right";
	case BT_AUDIO_LOCATION_TOP_FRONT_CENTER:
		return "Top front center";
	case BT_AUDIO_LOCATION_TOP_CENTER:
		return "Top center";
	case BT_AUDIO_LOCATION_TOP_BACK_LEFT:
		return "Top back left";
	case BT_AUDIO_LOCATION_TOP_BACK_RIGHT:
		return "Top back right";
	case BT_AUDIO_LOCATION_TOP_SIDE_LEFT:
		return "Top side left";
	case BT_AUDIO_LOCATION_TOP_SIDE_RIGHT:
		return "Top side right";
	case BT_AUDIO_LOCATION_TOP_BACK_CENTER:
		return "Top back center";
	case BT_AUDIO_LOCATION_BOTTOM_FRONT_CENTER:
		return "Bottom front center";
	case BT_AUDIO_LOCATION_BOTTOM_FRONT_LEFT:
		return "Bottom front left";
	case BT_AUDIO_LOCATION_BOTTOM_FRONT_RIGHT:
		return "Bottom front right";
	case BT_AUDIO_LOCATION_FRONT_LEFT_WIDE:
		return "Front left wide";
	case BT_AUDIO_LOCATION_FRONT_RIGHT_WIDE:
		return "Front right wde";
	case BT_AUDIO_LOCATION_LEFT_SURROUND:
		return "Left surround";
	case BT_AUDIO_LOCATION_RIGHT_SURROUND:
		return "Right surround";
	default:
		return "Unknown location";
	}
}

static inline char *parental_rating_to_str(enum bt_audio_parental_rating parental_rating)
{
	switch (parental_rating) {
	case BT_AUDIO_PARENTAL_RATING_NO_RATING:
		return "No rating";
	case BT_AUDIO_PARENTAL_RATING_AGE_ANY:
		return "Any";
	case BT_AUDIO_PARENTAL_RATING_AGE_5_OR_ABOVE:
		return "Age 5 or above";
	case BT_AUDIO_PARENTAL_RATING_AGE_6_OR_ABOVE:
		return "Age 6 or above";
	case BT_AUDIO_PARENTAL_RATING_AGE_7_OR_ABOVE:
		return "Age 7 or above";
	case BT_AUDIO_PARENTAL_RATING_AGE_8_OR_ABOVE:
		return "Age 8 or above";
	case BT_AUDIO_PARENTAL_RATING_AGE_9_OR_ABOVE:
		return "Age 9 or above";
	case BT_AUDIO_PARENTAL_RATING_AGE_10_OR_ABOVE:
		return "Age 10 or above";
	case BT_AUDIO_PARENTAL_RATING_AGE_11_OR_ABOVE:
		return "Age 11 or above";
	case BT_AUDIO_PARENTAL_RATING_AGE_12_OR_ABOVE:
		return "Age 12 or above";
	case BT_AUDIO_PARENTAL_RATING_AGE_13_OR_ABOVE:
		return "Age 13 or above";
	case BT_AUDIO_PARENTAL_RATING_AGE_14_OR_ABOVE:
		return "Age 14 or above";
	case BT_AUDIO_PARENTAL_RATING_AGE_15_OR_ABOVE:
		return "Age 15 or above";
	case BT_AUDIO_PARENTAL_RATING_AGE_16_OR_ABOVE:
		return "Age 16 or above";
	case BT_AUDIO_PARENTAL_RATING_AGE_17_OR_ABOVE:
		return "Age 17 or above";
	case BT_AUDIO_PARENTAL_RATING_AGE_18_OR_ABOVE:
		return "Age 18 or above";
	default:
		return "Unknown rating";
	}
}

static inline char *context_bit_to_str(enum bt_audio_context context)
{
	switch (context) {
	case BT_AUDIO_CONTEXT_TYPE_PROHIBITED:
		return "Prohibited";
	case BT_AUDIO_CONTEXT_TYPE_UNSPECIFIED:
		return "Unspecified";
	case BT_AUDIO_CONTEXT_TYPE_CONVERSATIONAL:
		return "Conversational";
	case BT_AUDIO_CONTEXT_TYPE_MEDIA:
		return "Media";
	case BT_AUDIO_CONTEXT_TYPE_GAME:
		return "Game";
	case BT_AUDIO_CONTEXT_TYPE_INSTRUCTIONAL:
		return "Instructional";
	case BT_AUDIO_CONTEXT_TYPE_VOICE_ASSISTANTS:
		return "Voice assistant";
	case BT_AUDIO_CONTEXT_TYPE_LIVE:
		return "Live";
	case BT_AUDIO_CONTEXT_TYPE_SOUND_EFFECTS:
		return "Sound effects";
	case BT_AUDIO_CONTEXT_TYPE_NOTIFICATIONS:
		return "Notifications";
	case BT_AUDIO_CONTEXT_TYPE_RINGTONE:
		return "Ringtone";
	case BT_AUDIO_CONTEXT_TYPE_ALERTS:
		return "Alerts";
	case BT_AUDIO_CONTEXT_TYPE_EMERGENCY_ALARM:
		return "Emergency alarm";
	default:
		return "Unknown";
	}
}

#endif /* _BCT_TEST_ */

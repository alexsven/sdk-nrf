/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef MULTICHANNEL_AURACAST_PROGRAMMES_H_
#define MULTICHANNEL_AURACAST_PROGRAMMES_H_

#include <zephyr/bluetooth/audio/audio.h>
#include <zephyr/net_buf.h>

#define MULTICHANNEL_AUDIO_LOCATIONS                                                               \
	(BT_AUDIO_LOCATION_FRONT_LEFT | BT_AUDIO_LOCATION_FRONT_RIGHT |                            \
	 BT_AUDIO_LOCATION_BACK_LEFT | BT_AUDIO_LOCATION_BACK_RIGHT)

/** Split four planar LC3 channels into two stereo subgroups; input remains owned by the caller. */
int programmes_send(const struct net_buf *audio_frame);

#endif /* MULTICHANNEL_AURACAST_PROGRAMMES_H_ */

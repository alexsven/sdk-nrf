/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef TEST_BROADCAST_SOURCE_H_
#define TEST_BROADCAST_SOURCE_H_

#include <zephyr/net_buf.h>

int broadcast_source_send(const struct net_buf *const audio_frame, uint8_t big_index,
			  uint8_t subgroup_index);

#endif /* TEST_BROADCAST_SOURCE_H_ */

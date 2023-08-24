/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef _UNICAST_SERVER_H_
#define _UNICAST_SERVER_H_

#include "le_audio.h"

#include <audio_defines.h>

int unicast_server_config_get(uint32_t *bitrate, uint32_t *sampling_rate, uint32_t *pres_delay);

void unicast_server_adv_get(const struct bt_data **adv, size_t *adv_size, bool periodic);

/**
 * @brief	Start the Bluetooth LE Audio unicast (CIS) server.
 *
 * @note	This would typically be active on a headset, mic, hearing aid etc.
 *
 * @return	0 for success, error otherwise.
 */
int unicast_server_start(void);

/**
 * @brief	Stop the Bluetooth LE Audio unicast (CIS) server.
 *
 * @return	0 for success, error otherwise.
 */
int unicast_server_stop(void);

/**
 * @brief	Send data from the LE Audio unicast (CIS) server, if configured as a source.
 *
 * @param[in]	enc_audio	Encoded audio struct.
 *
 * @return	0 for success, error otherwise.
 */
int unicast_server_send(struct encoded_audio enc_audio);

/**
 * @brief	Enable the Bluetooth LE Audio unicast (CIS) server.
 *
 * @return	0 for success, error otherwise.
 */
int unicast_server_enable(le_audio_receive_cb rx_cb);

/**
 * @brief	Disable the Bluetooth LE Audio unicast (CIS) server.
 *
 * @return	0 for success, error otherwise.
 */
int unicast_server_disable(void);

#endif /* _UNICAST_SERVER_H_ */

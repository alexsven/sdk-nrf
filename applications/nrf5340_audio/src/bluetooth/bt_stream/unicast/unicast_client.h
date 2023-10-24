/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef _UNICAST_CLIENT_H_
#define _UNICAST_CLIENT_H_

#include "le_audio.h"

#include <zephyr/bluetooth/audio/audio.h>
#include <audio_defines.h>

enum unicast_discover_dir {
	UNICAST_SERVER_SINK = BT_AUDIO_DIR_SINK,
	UNICAST_SERVER_SOURCE = BT_AUDIO_DIR_SOURCE,
	UNICAST_SERVER_BIDIR = (BT_AUDIO_DIR_SINK | BT_AUDIO_DIR_SOURCE)
};

/**
 * @brief	Get configuration for audio stream.
 *
 * @param[in]	conn			Pointer to the conn to get the config for.
 * @param[in]	dir			Direction to get the configuration from.
 * @param[out]	bitrate			Pointer to the bitrate used; can be NULL.
 * @param[out]	sampling_rate_hz	Pointer to the sampling rate used; can be NULL.
 *
 * @return	0 for success, error otherwise.
 */
int unicast_client_config_get(struct bt_conn *conn, enum bt_audio_dir dir, uint32_t *bitrate,
			      uint32_t *sampling_rate);

/**
 * @brief	Start service discovery for a Bluetooth LE Audio unicast (CIS) server.
 *
 * @param[in]	conn	Pointer to the connection.
 * @param[in]	dir	Direction of the stream.
 *
 * @return 0 for success, error otherwise.
 */
int unicast_client_discover(struct bt_conn *conn, enum unicast_discover_dir dir);

/**
 * @brief	Handle a disconnected Bluetooth LE Audio unicast (CIS) server.
 *
 * @param[in]	conn	Pointer to the connection.
 */
void unicast_client_conn_disconnected(struct bt_conn *conn);

/**
 * @brief	Start the Bluetooth LE Audio unicast (CIS) client.
 *
 * @return	0 for success, error otherwise.
 */
int unicast_client_start(void);

/**
 * @brief	Stop the Bluetooth LE Audio unicast (CIS) client.
 *
 * @return	0 for success, error otherwise.
 */
int unicast_client_stop(void);

/**
 * @brief	Send encoded audio using the Bluetooth LE Audio unicast.
 *
 * @param[in]	enc_audio	Encoded audio struct.
 *
 * @return	0 for success, error otherwise.
 */
int unicast_client_send(struct le_audio_encoded_audio enc_audio);

/**
 * @brief       Disable the Bluetooth LE Audio unicast (CIS) client.
 *
 * @return      0 for success, error otherwise.
 */
int unicast_client_disable(void);

/**
 * @brief	Enable the Bluetooth LE Audio unicast (CIS) client.
 *
 * @param[in]   recv_cb	Callback for handling received data.
 *
 * @return	0 for success, error otherwise.
 */
int unicast_client_enable(le_audio_receive_cb recv_cb);

#endif /* _UNICAST_CLIENT_H_ */

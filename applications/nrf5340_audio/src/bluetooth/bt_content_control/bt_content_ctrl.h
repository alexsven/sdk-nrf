/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef _BT_CONTENT_CTRL_H_
#define _BT_CONTENT_CTRL_H_

#include <zephyr/bluetooth/conn.h>

/**
 * @brief	Start content.
 *
 * @param	conn	Pointer to the connection to control, can be NULL.
 *
 * @return	0 for success, error otherwise.
 */
int bt_content_ctrl_start(struct bt_conn *conn);

/**
 * @brief	Stop content.
 *
 * @param	conn	Pointer to the connection to control, can be NULL.
 *
 * @return	0 for success, error otherwise.
 */
int bt_content_ctrl_stop(struct bt_conn *conn);

/**
 * @brief	Handle disconnected connection for content control services.
 *
 * @param	conn	Pointer to the disconnected connection.
 *
 * @return	0 for success, error otherwise.
 */
int bt_content_ctrl_conn_disconnected(struct bt_conn *conn);

/**
 * @brief	Discover content control services for a given conn pointer.
 *
 * @param	conn	Pointer to conn to discover on.
 *
 * @return	0 for success, error otherwise.
 */
int bt_content_ctrl_discover(struct bt_conn *conn);

/**
 * @brief	Initialize content control module.
 *
 * @return	0 for success, error otherwise.
 */
int bt_content_ctrl_init(void);

#endif /* _BT_CONTENT_CTRL_H_ */

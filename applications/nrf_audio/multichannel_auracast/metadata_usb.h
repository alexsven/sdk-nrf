/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef METADATA_USB_H_
#define METADATA_USB_H_

#include <zephyr/net_buf.h>

int metadata_usb_init(const struct net_buf_simple *base);
void metadata_usb_publish(void);

#endif

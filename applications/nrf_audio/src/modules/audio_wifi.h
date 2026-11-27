/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef AUDIO_WIFI_H_
#define AUDIO_WIFI_H_

#include <zephyr/kernel.h>

int audio_wifi_init(void);
int audio_wifi_start(struct k_msgq *queue);
void audio_wifi_stop(void);

#endif

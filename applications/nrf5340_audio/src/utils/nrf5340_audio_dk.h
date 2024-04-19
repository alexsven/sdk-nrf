/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef _NRF5340_AUDIO_DK_H_
#define _NRF5340_AUDIO_DK_H_

#include "led.h"

/**
 * @brief	Initialize the hardware related modules on the nRF5340 Audio DK/PCA10121.
 *
 * @param[in]	led_override	Override the default center LED color.
 * @param[in]	color		The color to set center LED to.
 *
 * @return	0 if successful, error otherwise.
 */
int nrf5340_audio_dk_init(bool led_override, enum led_color color);

#endif /* _NRF5340_AUDIO_DK_H_ */
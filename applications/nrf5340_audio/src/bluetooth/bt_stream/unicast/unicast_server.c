/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/*This file will replace CIS headset*/

/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef _UNICAST_SERVER_H_
#define _UNICAST_SERVER_H_

#include "le_audio.h"

#include <audio_defines.h>

typedef void (*audio_rx_cb)(audio_data audio);

int unicast_server_start(void);
{
	return -1;
}

int unicast_server_stop(void);
{
	return -1;
}

int unicast_server_send(struct audio_data tx);
{
	return -1;
}

int unicast_server_enable(audio_rx_cb rx_cb);
{
	return -1;
}

int unicast_server_disable(void);
{
	return -1;
}

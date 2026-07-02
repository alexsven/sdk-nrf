/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/** This file implements controller time management for 54 Series devices
 */

#include <zephyr/kernel.h>
#include <nrfx_grtc.h>
#include "current_time_grtc.h"

static uint8_t grtc_channel;

int current_time_grtc_init(void)
{
	int ret;

	ret = nrfx_grtc_channel_alloc(&grtc_channel);
	if (ret < 0) {
		printk("Failed allocating GRTC channel (ret: %d)\n", ret);
		return ret;
	}

	nrf_grtc_sys_counter_compare_event_enable(NRF_GRTC, grtc_channel);

	return 0;
}

uint64_t current_time_grtc_us_get(void)
{
	return nrfx_grtc_syscounter_get();
}

void current_time_grtc_trigger_set(uint64_t timestamp_us)
{
	int ret;

	nrfx_grtc_channel_t chan_data = {
		.channel = grtc_channel,
	};

	ret = nrfx_grtc_syscounter_cc_absolute_set(&chan_data, timestamp_us, false);
	if (ret != 0) {
		printk("Failed setting CC (ret: %d)\n", ret);
	}
}

uint32_t current_time_grtc_trigger_event_addr_get(void)
{
	return nrf_grtc_event_address_get(NRF_GRTC,
					  nrf_grtc_sys_counter_compare_event_get(grtc_channel));
}

SYS_INIT(current_time_grtc_init, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);

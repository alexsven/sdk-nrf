/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef _CURRENT_TIME_GRTC_H_
#define _CURRENT_TIME_GRTC_H_

 #include <stdint.h>

/** @brief Obtain the current time from the GRTC.
 *
 * The timestamps are based upon this clock.
 *
 * @return The current time.
 */
uint64_t current_time_grtc_us_get(void);

/** @brief Set the controller to trigger a PPI event at the given timestamp.
 *
 * @param timestamp_us The timestamp where it will trigger.
 */
void current_time_grtc_trigger_set(uint64_t timestamp_us);

/** @brief Get the address of the event that will trigger.
 *
 * @return The address of the event that will trigger.
 */
uint32_t current_time_grtc_trigger_event_addr_get(void);

#endif /* _CURRENT_TIME_GRTC_H_ */

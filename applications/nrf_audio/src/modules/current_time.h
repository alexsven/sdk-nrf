/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/** @file
 * @defgroup current_time Current Time
 * @{
 * @brief Current time API for applications.
 *
 * This module provides precise timing functionality for audio synchronization across
 * multiple devices.
 */

#ifndef _CURRENT_TIME_H_
#define _CURRENT_TIME_H_

#include <stdint.h>

/**
 * @brief Obtain the current time in microseconds.
 */
uint32_t current_time_us_get(void);

/**
 * @}
 */

#endif /* _CURRENT_TIME_H_ */

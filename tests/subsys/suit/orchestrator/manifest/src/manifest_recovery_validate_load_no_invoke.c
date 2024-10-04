/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <stdint.h>
#include <stddef.h>

/** @brief Valid SUIT envelope, based on ../recovery_validate_load_no_invoke.yaml
 *
 */
const uint8_t manifest_recovery_validate_load_no_invoke_buf[] = {
	0xD8, 0x6B, 0xA2, 0x02, 0x58, 0x7A, 0x82, 0x58, 0x24, 0x82, 0x2F, 0x58, 0x20, 0x86, 0x3C,
	0xD7, 0x87, 0x4A, 0xEE, 0xBE, 0x78, 0xA7, 0x57, 0x7C, 0xAE, 0xCB, 0xBA, 0x7D, 0x5E, 0x3D,
	0xB1, 0xAC, 0x8B, 0x36, 0xD8, 0xB1, 0x79, 0xAA, 0xD4, 0xEA, 0xB2, 0xEA, 0x66, 0xA7, 0x74,
	0x58, 0x51, 0xD2, 0x84, 0x4A, 0xA2, 0x01, 0x26, 0x04, 0x45, 0x1A, 0x40, 0x00, 0x00, 0x00,
	0xA0, 0xF6, 0x58, 0x40, 0x29, 0xB9, 0x06, 0xB0, 0xC0, 0x19, 0xB2, 0x8D, 0x6D, 0x24, 0xB4,
	0xF1, 0x80, 0x4D, 0x56, 0xA8, 0x00, 0xC3, 0x62, 0x6C, 0xBA, 0x30, 0x11, 0xF7, 0x89, 0xC5,
	0xC9, 0xB8, 0x69, 0x7F, 0x61, 0x8C, 0x1C, 0xA2, 0xA2, 0xE9, 0xE1, 0xC2, 0xF8, 0xE5, 0x3F,
	0xF6, 0x1B, 0x8F, 0x8E, 0xB3, 0xED, 0x72, 0x0B, 0x1E, 0x00, 0x41, 0x64, 0x8A, 0x0F, 0x32,
	0xD9, 0xED, 0xD6, 0x9B, 0xCA, 0x94, 0xCC, 0xD4, 0x03, 0x58, 0x71, 0xA6, 0x01, 0x01, 0x02,
	0x01, 0x03, 0x58, 0x3F, 0xA3, 0x02, 0x81, 0x82, 0x4A, 0x69, 0x43, 0x41, 0x4E, 0x44, 0x5F,
	0x4D, 0x46, 0x53, 0x54, 0x41, 0x00, 0x04, 0x58, 0x27, 0x82, 0x14, 0xA2, 0x01, 0x50, 0x76,
	0x17, 0xDA, 0xA5, 0x71, 0xFD, 0x5A, 0x85, 0x8F, 0x94, 0xE2, 0x8D, 0x73, 0x5C, 0xE9, 0xF4,
	0x02, 0x50, 0x74, 0xA0, 0xC6, 0xE7, 0xA9, 0x2A, 0x56, 0x00, 0x9C, 0x5D, 0x30, 0xEE, 0x87,
	0x8B, 0x06, 0xBA, 0x01, 0xA1, 0x00, 0xA0, 0x07, 0x43, 0x82, 0x0C, 0x00, 0x08, 0x43, 0x82,
	0x0C, 0x00, 0x05, 0x82, 0x4C, 0x6B, 0x49, 0x4E, 0x53, 0x54, 0x4C, 0x44, 0x5F, 0x4D, 0x46,
	0x53, 0x54, 0x50, 0x74, 0xA0, 0xC6, 0xE7, 0xA9, 0x2A, 0x56, 0x00, 0x9C, 0x5D, 0x30, 0xEE,
	0x87, 0x8B, 0x06, 0xBA};

const size_t manifest_recovery_validate_load_no_invoke_len =
	sizeof(manifest_recovery_validate_load_no_invoke_buf);

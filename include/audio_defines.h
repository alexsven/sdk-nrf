/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/** @file
 *  @brief Globally accessible audio related defines
 */

#ifndef _AUDIO_DEFINES_H_
#define _AUDIO_DEFINES_H_

#include <zephyr/kernel.h>

/**
 * @brief Audio channel assignment values
 */
enum audio_channel {
	AUDIO_CH_L,
	AUDIO_CH_R,
	AUDIO_CH_NUM,
};

/**
 * @brief Value to define the data type carried by the block.
 */
enum ablk_type {
	/* The audio data block is raw PCM */
	ABLK_TYPE_PCM = 1,

	/* The audio data block is coded in LC3 */
	ABLK_TYPE_LC3
};

/**
 * @brief Value to define the PCM interleaving type.
 */
enum ablk_interleaved {
	/* Audio samples are deinterleaved within the buffer */
	ABLK_DEINTERLEAVED = 1,

	/* Audio samples are interleaved within the buffer */
	ABLK_INTERLEAVED
};

/**
 * @brief A structure giving the description of the audio block
 */
struct ablk_block {
	/* Indicates the data type/encoding of the block. */
	enum ablk_type data_type;

	/* A pointer to the raw or coded data (e.g., PCM, LC3, etc.) buffer. */
	void *data;

	/* The size in bytes of the data buffer.
	 * To get the size of each channel, this value must be divided by the number of
	 * used channels.
	 */
	size_t data_size;

	/* Frame length in microseconds.
	 * This may be aligned with the Bluetooth connection interval.
	 * Typical values may be 7500 or 10000 us.
	 */
	uint32_t frame_len_us;

	/* The sample rate of the block of data.
	 * This may typically be 16000, 24000, 44100 or 48000 Hz.
	 */
	uint32_t sample_rate_hz;

	/* Number of valid bits for a sample (bit depth).
	 * Typically 16 or 24.
	 */
	uint8_t bits_per_sample;

	/* Bit rate of the block in bits/sec.
	 * For PCM data, this value can be calculated from other parameters.
	 * Though, for encoded data, this can not be inferred.
	 */
	uint32_t bitrate;

	/* Number of bits used to carry a sample of size bits_per_sample.
	 * For example, say we have a 24 bit sample stored in a 32 bit
	 * word (int32_t), then:
	 *     bits_per_sample = 24
	 *     carrier_size    = 32
	 */
	uint8_t carrier_size;

	/* A flag indicating if the samples are interleaved or not */
	enum ablk_interleaved interleaved;

	/* A 32 bit mask indicating which channel(s) are active within
	 * the block. A bit set indicates the channel is active and
	 * a count of these will give the number of channels within the
	 * audio stream.
	 * Note: This will follow the ANSI/CTA-861-G’s Table 34 codes
	 * for speaker placement (as used by Bluetooth Low Energy Audio).
	 */
	uint32_t channel_map;

	/* Reference time stamp (e.g. ISO timestamp
	 * reference from BLE controller).
	 */
	uint32_t reference_ts_us;

	/* The timestamp for when the data was received. */
	uint32_t data_rx_ts_us;

	/* A Boolean flag to indicate this frame has errors in the
	 * data (true = bad, false = good).
	 * Note: Timestamps may still be valid for a bad frame.
	 */
	bool bad_frame;

	/* A pointer to private data associated with the data block */
	void *user_data;

	/* Size of user data*/
	size_t user_data_size;
};

#endif /* _AUDIO_DEFINES_H_ */

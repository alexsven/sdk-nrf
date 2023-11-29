/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/** @file
 * @brief Sample Rate Converter library header.
 */

#ifndef __SAMPLE_RATE_CONVERTER__
#define __SAMPLE_RATE_CONVERTER__

/**
 * @defgroup sample_rate_converter Sample Rate Converter library
 * @brief Implements functionality to increase or decrease the sample rate of a data array using
 * the CMSIS DSP filtering library.
 *
 * @{
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/ring_buffer.h>
#include <dsp/filtering_functions.h>

/**
 * Maximum size for the internal state buffers.
 *
 * The internal state buffer must for each context fulfill the following equations:
 *	Interpolation:
 *		number of filter taps + block size - 1
 *	Decimation:
 *		(number of filter taps / conversion ratio) + block size - 1
 *
 * The equation for interpolation is used as size as this gives the largest number.
 */
#define SAMPLE_RATE_CONVERTER_STATE_BUFFER_SIZE                                                    \
	CONFIG_SAMPLE_RATE_CONVERTER_BLOCK_SIZE + CONFIG_SAMPLE_RATE_CONVERTER_MAX_FILTER_SIZE - 1

/** Filter types supported by the sample rate converter*/
enum sample_rate_converter_filter {
	SAMPLE_RATE_FILTER_SIMPLE,
	SAMPLE_RATE_FILTER_SMALL,
};

#ifdef CONFIG_SAMPLE_RATE_CONVERTER_BIT_DEPTH_16
/* The input buffer must be big enough to store 2 spillover samples */
#define SAMPLE_RATE_CONVERT_INPUT_BUF_SIZE 2 * sizeof(uint16_t)
/* The output buffer must be big enough to store 6 spillover samples */
#define SAMPLE_RATE_CONVERTER_RINGBUF_SIZE                                                         \
	(CONFIG_SAMPLE_RATE_CONVERTER_BLOCK_SIZE + 6) * sizeof(uint16_t)
#elif CONFIG_SAMPLE_RATE_CONVERTER_BIT_DEPTH_32
/* The input buffer must be big enough to store 2 spillover samples */
#define SAMPLE_RATE_CONVERT_INPUT_BUF_SIZE 2 * sizeof(uint32_t)
/* The output buffer must be big enough to store 6 spillover samples */
#define SAMPLE_RATE_CONVERTER_RINGBUF_SIZE                                                         \
	(CONFIG_SAMPLE_RATE_CONVERTER_BLOCK_SIZE + 6) * sizeof(uint32_t)
#endif

/** Buffer used for storing input bytes to the sample rate converter*/
struct buf_ctx {
	uint8_t buf[SAMPLE_RATE_CONVERT_INPUT_BUF_SIZE];
	size_t bytes_in_buf;
};

/** Context for the sample rate conversion */
struct sample_rate_converter_ctx {
	int input_sample_rate;
	int output_sample_rate;
	enum sample_rate_converter_filter filter_type;
	struct buf_ctx input_buf;
	struct ring_buf output_ringbuf;
	uint8_t output_ringbuf_data[SAMPLE_RATE_CONVERTER_RINGBUF_SIZE];
	union {
#ifdef CONFIG_SAMPLE_RATE_CONVERTER_BIT_DEPTH_16
		arm_fir_interpolate_instance_q15 fir_interpolate_q15;
		arm_fir_decimate_instance_q15 fir_decimate_q15;
#elif CONFIG_SAMPLE_RATE_CONVERTER_BIT_DEPTH_32
		arm_fir_interpolate_instance_q31 fir_interpolate_q31;
		arm_fir_decimate_instance_q31 fir_decimate_q31;
#endif
	};
#ifdef CONFIG_SAMPLE_RATE_CONVERTER_BIT_DEPTH_16
	q15_t state_buf_15[SAMPLE_RATE_CONVERTER_STATE_BUFFER_SIZE];
#elif CONFIG_SAMPLE_RATE_CONVERTER_BIT_DEPTH_32
	q31_t state_buf_31[SAMPLE_RATE_CONVERTER_STATE_BUFFER_SIZE];
#endif
};

/**
 * @brief	Process input samples and produce output samples with new samplerate
 *
 * @details	Takes samples with the input sample rate, and converts them to the new requested
 *		sample rate by filtering the samples before adding or removing samples. The context
 *		for the sample rate conversion does not need to be initialized before calling
 *		process, and if any parameters change between calls the context will be
 *		re-inititalized. As the process has requirements for the number of input samples
 *		based on the conversion ratio, the module will buffer both input and output bytes
 *		when needed to meet this criteria.
 *
 * @param[in,out]	ctx			Pointer to sample rate conversion context
 * @param[in]		filter			Filter type to be used for the conversion
 * @param[in]		input			Pointer to samples to process
 * @param[in]		input_size		Size of the input in bytes
 * @param[in]		input_sample_rate	Sample rate of the input bytes
 * @param[out]		output			Array that output will be written
 * @param[in]		output_size		Number of bytes requested as output
 * @param[in]		output_sample_rate	Sample rate of output
 *
 * @retval 0 On success
 * @retval -EINVAL Invalid parameters for sample rate conversion
 * @retval -EFAULT Output ring buffer error
 */
int sample_rate_converter_process(struct sample_rate_converter_ctx *ctx,
				  enum sample_rate_converter_filter filter, void *input,
				  size_t input_size, uint32_t input_sample_rate, void *output,
				  size_t output_size, uint32_t output_sample_rate);

/**
 * @}
 */

#endif /* __ SAMPLE_RATE_CONVERTER__ */

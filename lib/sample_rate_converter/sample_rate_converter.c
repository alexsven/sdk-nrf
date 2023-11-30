/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "sample_rate_converter.h"
#include "sample_rate_converter_filter.h"

#include <errno.h>
#include <stdbool.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(sample_rate_converter, CONFIG_SAMPLE_RATE_CONVERTER_LOG_LEVEL);

/**
 * The input buffer must be able to store maximum two samples in addition to the block size to meet
 * filter requirements.
 */
#define SAMPLE_RATE_CONVERTER_INPUT_BUF_NUMBER_SAMPLES
#define INTERNAL_INPUT_BUF_NUMBER_SAMPLES                                                          \
	CONFIG_SAMPLE_RATE_CONVERTER_BLOCK_SIZE +                                                  \
		SAMPLE_RATE_CONVERTER_INPUT_BUFFER_NUMBER_OVERFLOW_SAMPLES

#ifdef CONFIG_SAMPLE_RATE_CONVERTER_BIT_DEPTH_16
#define SAMPLE_RATE_CONVERTER_INTERNAL_INPUT_BUF_SIZE                                              \
	INTERNAL_INPUT_BUF_NUMBER_SAMPLES * sizeof(uint16_t)
#define SAMLPE_RATE_CONVERTER_INTERNAL_OUTPUT_BUF_SIZE                                             \
	(CONFIG_SAMPLE_RATE_CONVERTER_BLOCK_SIZE * sizeof(uint16_t))
#elif CONFIG_SAMPLE_RATE_CONVERTER_BIT_DEPTH_32
#define SAMPLE_RATE_CONVERTER_INTERNAL_INPUT_BUF_SIZE                                              \
	INTERNAL_INPUT_BUF_NUMBER_SAMPLES * sizeof(uint32_t)
#define SAMLPE_RATE_CONVERTER_INTERNAL_OUTPUT_BUF_SIZE                                             \
	(CONFIG_SAMPLE_RATE_CONVERTER_BLOCK_SIZE * sizeof(uint32_t))
#endif

static int validate_sample_rates(uint32_t input_sample_rate, uint32_t output_sample_rate)
{
	if (input_sample_rate > output_sample_rate) {
		if (input_sample_rate != 48000) {
			LOG_ERR("Invalid input sample rate for downsampling %d", input_sample_rate);
			return -EINVAL;
		}

		if ((output_sample_rate != 24000) && (output_sample_rate != 16000)) {
			LOG_ERR("Invalid output sample rate for downsampling %d",
				output_sample_rate);
			return -EINVAL;
		}
	} else if (input_sample_rate < output_sample_rate) {
		if (output_sample_rate != 48000) {
			LOG_ERR("Invalid output sample rate for upsampling: %d",
				output_sample_rate);
			return -EINVAL;
		}

		if ((input_sample_rate != 24000) && (input_sample_rate != 16000)) {
			LOG_ERR("Invalid input sample rate for upsampling: %d", input_sample_rate);
			return -EINVAL;
		}
	} else {
		LOG_ERR("Input and out samplerates are the same");
		return -EINVAL;
	}
	return 0;
}

static inline bool is_upsampling(uint32_t input_rate, uint32_t output_rate)
{
	return input_rate < output_rate;
}

/**
 * When upsampling from 16kHz to 48kHz the input and output bytes must be buffered. This is to
 * fulfill the requirement for the filter that the number of input bytes must be divisible by the
 * conversion factor. This function returns true when this is the case.
 */
static inline bool conversion_needs_buffering(struct sample_rate_converter_ctx *ctx,
					      uint8_t conversion_ratio)
{
	return (is_upsampling(ctx->input_sample_rate, ctx->output_sample_rate) &&
		(conversion_ratio == 3));
}

static inline uint8_t calculate_conversion_ratio(uint32_t input_sample_rate,
						 uint32_t output_sample_rate)
{
	if (input_sample_rate > output_sample_rate) {
		return input_sample_rate / output_sample_rate;
	} else {
		return output_sample_rate / input_sample_rate;
	}
}

/**
 * @brief Initializes the sample rate converter context.
 *
 * @details Validates and sets all sample rate conversion parameters for the context. If buffering
 *	    is needed for the conversion, the input buffer will be padded with two samples to
 *	    ensure there will always be enough samples for a valid conversion.
 *
 * @param[in,out]	ctx			Pointer to the sample rate conversion context.
 * @param[in]		input_sample_rate	Sample rate of the input samples.
 * @param[in]		output_sample_rate	Sample rate of the output samples.
 * @param[in]		filter			Filter type to use in the conversion.
 *
 * @retval 0 On success.
 * @retval -EINVAL Invalid parameters used to initialize the conversion.
 * @retval Other errno codes from CMSIS DSP library.
 */
static int sample_rate_converter_reconfigure(struct sample_rate_converter_ctx *ctx,
					     uint32_t input_sample_rate,
					     uint32_t output_sample_rate,
					     enum sample_rate_converter_filter filter)
{
	int ret;
	arm_status arm_err;
	uint8_t conversion_ratio;
	uint8_t *filter_coeffs;
	size_t filter_size;

	__ASSERT(ctx != NULL, "Context cannot be NULL");

	ret = validate_sample_rates(input_sample_rate, output_sample_rate);
	if (ret) {
		LOG_ERR("Invalid sample rate given (%d)", ret);
		return ret;
	}

	ctx->input_sample_rate = input_sample_rate;
	ctx->output_sample_rate = output_sample_rate;

	conversion_ratio = calculate_conversion_ratio(input_sample_rate, output_sample_rate);

	ctx->filter_type = filter;
	ret = sample_rate_converter_filter_get(filter, conversion_ratio, (void **)&filter_coeffs,
					       &filter_size);
	if (ret) {
		LOG_ERR("Failed to get filter (%d)", ret);
		return ret;
	} else if (filter_size > CONFIG_SAMPLE_RATE_CONVERTER_MAX_FILTER_SIZE) {
		LOG_ERR("Filter is larger than max size");
		return -EINVAL;
	}

	if (conversion_needs_buffering(ctx, conversion_ratio)) {
		LOG_DBG("Conversion needs buffering, start with the input buffer filled");
#ifdef CONFIG_SAMPLE_RATE_CONVERTER_BIT_DEPTH_16
		ctx->input_buf.bytes_in_buf =
			SAMPLE_RATE_CONVERTER_INPUT_BUFFER_NUMBER_OVERFLOW_SAMPLES *
			sizeof(uint16_t);
#elif CONFIG_SAMPLE_RATE_CONVERTER_BIT_DEPTH_32
		ctx->input_buf.bytes_in_buf =
			SAMPLE_RATE_CONVERTER_INPUT_BUFFER_NUMBER_OVERFLOW_SAMPLES *
			sizeof(uint32_t);
#endif
		memset(ctx->input_buf.buf, 0, ctx->input_buf.bytes_in_buf);
	} else {
		ctx->input_buf.bytes_in_buf = 0;
	}

	/* Ring buffer will be used to store spillover output samples */
	ring_buf_init(&ctx->output_ringbuf, ARRAY_SIZE(ctx->output_ringbuf_data),
		      ctx->output_ringbuf_data);

#ifdef CONFIG_SAMPLE_RATE_CONVERTER_BIT_DEPTH_16
	if (is_upsampling(ctx->input_sample_rate, ctx->output_sample_rate)) {
		arm_err = arm_fir_interpolate_init_q15(&ctx->fir_interpolate_q15, conversion_ratio,
						       filter_size, (q15_t *)filter_coeffs,
						       ctx->state_buf_15,
						       CONFIG_SAMPLE_RATE_CONVERTER_BLOCK_SIZE);

	} else {
		arm_err = arm_fir_decimate_init_q15(&ctx->fir_decimate_q15, filter_size,
						    conversion_ratio, (q15_t *)filter_coeffs,
						    ctx->state_buf_15,
						    CONFIG_SAMPLE_RATE_CONVERTER_BLOCK_SIZE);
	}
#elif CONFIG_SAMPLE_RATE_CONVERTER_BIT_DEPTH_32
	if (is_upsampling(ctx->input_sample_rate, ctx->output_sample_rate)) {
		arm_err = arm_fir_interpolate_init_q31(&ctx->fir_interpolate_q31, conversion_ratio,
						       filter_size, (q31_t *)filter_coeffs,
						       ctx->state_buf_31,
						       CONFIG_SAMPLE_RATE_CONVERTER_BLOCK_SIZE);
	} else {
		arm_err = arm_fir_decimate_init_q31(&ctx->fir_decimate_q31, filter_size,
						    conversion_ratio, (q31_t *)filter_coeffs,
						    ctx->state_buf_31,
						    CONFIG_SAMPLE_RATE_CONVERTER_BLOCK_SIZE);
	}
#endif
	if (arm_err == ARM_MATH_LENGTH_ERROR) {
		LOG_ERR("Filter size is not a multiple of conversion ratio");
		return -EINVAL;
	} else if (arm_err != ARM_MATH_SUCCESS) {
		LOG_ERR("Unknown error during interpolator/decimator initialization (%d)", arm_err);
		return -EINVAL;
	}

	LOG_DBG("Sample rate converter initialized. Input sample rate: %d, Output sample rate: %d, "
		"conversion ratio: %d, filter type: %d",
		ctx->input_sample_rate, ctx->output_sample_rate, conversion_ratio,
		ctx->filter_type);
	return 0;
}

int sample_rate_converter_open(struct sample_rate_converter_ctx *ctx)
{
	if (ctx == NULL) {
		LOG_ERR("Context cannot be NULL");
		return -EINVAL;
	}

	memset(ctx, 0, sizeof(struct sample_rate_converter_ctx));

	return 0;
}

int sample_rate_converter_process(struct sample_rate_converter_ctx *ctx,
				  enum sample_rate_converter_filter filter, void *input,
				  size_t input_size, uint32_t input_sample_rate, void *output,
				  size_t output_size, uint32_t output_sample_rate)
{
	int ret;
	uint8_t *read_ptr;
	uint8_t *write_ptr;
	uint8_t conversion_ratio;
	size_t bytes_produced;
	size_t samples_to_process;

	uint8_t internal_input_buf[SAMPLE_RATE_CONVERTER_INTERNAL_INPUT_BUF_SIZE];
	uint8_t internal_output_buf[SAMLPE_RATE_CONVERTER_INTERNAL_OUTPUT_BUF_SIZE];

#if CONFIG_SAMPLE_RATE_CONVERTER_BIT_DEPTH_16
	size_t bytes_per_sample = sizeof(uint16_t);
#elif CONFIG_SAMPLE_RATE_CONVERTER_BIT_DEPTH_32
	size_t bytes_per_sample = sizeof(uint32_t);
#endif

	size_t samples_in = input_size / bytes_per_sample;

	if ((ctx == NULL) || (input == NULL) || (output == NULL)) {
		LOG_ERR("Null pointer received");
		return -EINVAL;
	}

	if ((ctx->input_sample_rate != input_sample_rate) ||
	    (ctx->output_sample_rate != output_sample_rate) || (ctx->filter_type != filter)) {
		LOG_DBG("State has changed, re-initializing filter");
		ret = sample_rate_converter_reconfigure(ctx, input_sample_rate, output_sample_rate,
							filter);
		if (ret) {
			LOG_ERR("Failed to initialize converter (%d)", ret);
			return ret;
		}
	}

	conversion_ratio = calculate_conversion_ratio(input_sample_rate, output_sample_rate);

	if (!conversion_needs_buffering(ctx, conversion_ratio)) {
		write_ptr = output;
		read_ptr = input;
		samples_to_process = samples_in;
	} else {
		read_ptr = internal_input_buf;
		write_ptr = internal_output_buf;

		if (((samples_in + (ctx->input_buf.bytes_in_buf * bytes_per_sample)) %
		     conversion_ratio) == 0) {
			size_t extra_samples = conversion_ratio - (samples_in % conversion_ratio);

			LOG_DBG("Using %d extra samples from input buffer", extra_samples);
			samples_to_process = samples_in + extra_samples;
		} else {
			size_t extra_samples = (samples_in % conversion_ratio);

			LOG_DBG("Storing %d samples in input buffer for next iteration",
				extra_samples);
			samples_to_process = samples_in - extra_samples;
		}

		/* Merge bytes in input buffer and incoming bytes into the internal buffer for
		 * processing
		 */
		memcpy(internal_input_buf, ctx->input_buf.buf, ctx->input_buf.bytes_in_buf);
		memcpy(internal_input_buf + ctx->input_buf.bytes_in_buf, input, input_size);
	}

#ifdef CONFIG_SAMPLE_RATE_CONVERTER_BIT_DEPTH_16
	if (is_upsampling(ctx->input_sample_rate, ctx->output_sample_rate)) {
		arm_fir_interpolate_q15(&ctx->fir_interpolate_q15, (q15_t *)read_ptr,
					(q15_t *)write_ptr, samples_to_process);
		bytes_produced = samples_to_process * conversion_ratio * bytes_per_sample;
	} else {
		arm_fir_decimate_q15(&ctx->fir_decimate_q15, (q15_t *)read_ptr, (q15_t *)write_ptr,
				     samples_to_process);
		bytes_produced = (samples_to_process / conversion_ratio) * bytes_per_sample;
	}
#elif CONFIG_SAMPLE_RATE_CONVERTER_BIT_DEPTH_32
	if (is_upsampling(ctx->input_sample_rate, ctx->output_sample_rate)) {
		arm_fir_interpolate_q31(&ctx->fir_interpolate_q31, (q31_t *)read_ptr,
					(q31_t *)write_ptr, samples_to_process);
		bytes_produced = samples_to_process * conversion_ratio * bytes_per_sample;
	} else {
		arm_fir_decimate_q31(&ctx->fir_decimate_q31, (q31_t *)read_ptr, (q31_t *)write_ptr,
				     samples_to_process);
		bytes_produced = (samples_to_process / conversion_ratio) * bytes_per_sample;
	}
#endif

	if (!conversion_needs_buffering(ctx, conversion_ratio)) {
		/* Nothing needs to be done in output buffer */
		return 0;
	}

	if (samples_to_process < samples_in) {
		size_t number_overflow_samples = samples_in - samples_to_process;

		ctx->input_buf.bytes_in_buf += number_overflow_samples * bytes_per_sample;
		memcpy(ctx->input_buf.buf, read_ptr + (samples_to_process * bytes_per_sample),
		       ctx->input_buf.bytes_in_buf);
		LOG_DBG("%d overflow samples stored in buffer", number_overflow_samples);
	} else if ((ctx->input_buf.bytes_in_buf) &&
		   (((samples_in + (ctx->input_buf.bytes_in_buf * bytes_per_sample)) %
		     conversion_ratio) == 0)) {
		uint8_t overflow_samples_used = conversion_ratio - (samples_in % conversion_ratio);

		ctx->input_buf.bytes_in_buf -= overflow_samples_used * bytes_per_sample;
		LOG_DBG("%d overflow samples has been used", overflow_samples_used);
	}

	int bytes_to_write = bytes_produced;
	uint8_t *ringbuf_write_ptr = (uint8_t *)internal_output_buf;

	LOG_DBG("Writing %d bytes to output buffer", bytes_to_write);
	while (bytes_to_write) {
		uint8_t *data;
		size_t ringbuf_write_size =
			ring_buf_put_claim(&ctx->output_ringbuf, &data, bytes_to_write);
		if (ringbuf_write_size == 0) {
			LOG_ERR("Ring buffer storage exhausted");
			return -EFAULT;
		}

		memcpy(data, ringbuf_write_ptr, ringbuf_write_size);

		ret = ring_buf_put_finish(&ctx->output_ringbuf, ringbuf_write_size);
		if (ret) {
			LOG_ERR("Ringbuf err: %d", ret);
			return -EFAULT;
		}

		ringbuf_write_ptr += ringbuf_write_size;
		bytes_to_write -= ringbuf_write_size;
	}

	int bytes_to_read = output_size;
	uint8_t *ringbuf_output_ptr = (uint8_t *)output;

	LOG_DBG("Reading %d bytes from output_buffer", bytes_to_read);
	while (bytes_to_read) {
		uint8_t *data;
		size_t ringbuf_read_size =
			ring_buf_get_claim(&ctx->output_ringbuf, &data, bytes_to_read);

		if (ringbuf_read_size == 0) {
			LOG_ERR("Ring buffer storage empty");
			return -EFAULT;
		}

		memcpy(ringbuf_output_ptr, data, ringbuf_read_size);
		ringbuf_output_ptr += ringbuf_read_size;
		bytes_to_read -= ringbuf_read_size;

		ret = ring_buf_get_finish(&ctx->output_ringbuf, ringbuf_read_size);
		if (ret) {
			LOG_ERR("Ring buf read err: %d", ret);
			return -EFAULT;
		}
	}

	return 0;
}

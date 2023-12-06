/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/ztest.h>
#include <zephyr/tc_util.h>
#include <sample_rate_converter.h>

struct sample_rate_converter_ctx conv_ctx;

static void test_setup(void *f)
{
	sample_rate_converter_open(&conv_ctx);
}

#ifdef CONFIG_SAMPLE_RATE_CONVERTER_BIT_DEPTH_16
ZTEST(suite_sample_rate_converter, test_init_valid_decimate_24khz_16bit)
{
	int ret;

	uint32_t input_sample_rate = 48000;
	uint32_t output_sample_rate = 24000;
	uint32_t conversion_ratio = input_sample_rate / output_sample_rate;

	uint16_t input_samples[] = {1000, 2000, 3000, 4000,  5000,  6000,
				    7000, 8000, 9000, 10000, 11000, 12000};
	size_t num_samples = ARRAY_SIZE(input_samples);
	size_t expected_output_samples = num_samples / conversion_ratio;
	uint16_t output_samples[expected_output_samples];

	enum sample_rate_converter_filter filter = SAMPLE_RATE_FILTER_SIMPLE;
	size_t output_written;

	ret = sample_rate_converter_process(
		&conv_ctx, filter, input_samples, num_samples * sizeof(uint16_t), input_sample_rate,
		output_samples, expected_output_samples * sizeof(uint16_t), &output_written,
		output_sample_rate);

	zassert_equal(ret, 0, "Sample rate conversion process failed");
	zassert_equal(conv_ctx.input_sample_rate, input_sample_rate,
		      "Input sample rate not as expected");
	zassert_equal(conv_ctx.output_sample_rate, output_sample_rate,
		      "Output sample rate not as expected");
	zassert_equal(conv_ctx.conversion_ratio, conversion_ratio,
		      "Conversion ratio not as expected");
	zassert_equal(conv_ctx.conversion_direction, CONVERSION_DIR_DOWN,
		      "Conversion direction is not as expected");
	zassert_equal(conv_ctx.filter_type, filter, "Filter not as expected");
	zassert_equal(conv_ctx.input_buf.bytes_in_buf, 0, "Bytes in input buffer not as expected");
	zassert_equal(ring_buf_size_get(&conv_ctx.output_ringbuf), 0,
		      "Number of bytes in output ringbuffer not as expected %d",
		      ring_buf_size_get(&conv_ctx.output_ringbuf));

	zassert_equal(output_written, expected_output_samples * sizeof(uint16_t),
		      "Output size was not as expected (%d)", output_written);

	/* Verify output bytes, expected two input samples per output sample */
	for (int i = 0; i < expected_output_samples; i++) {
		zassert_within(output_samples[i], input_samples[i * 2], 550);
	}
}

ZTEST(suite_sample_rate_converter, test_init_valid_decimate_16khz_16bit)
{
	int ret;

	uint32_t input_sample_rate = 48000;
	uint32_t output_sample_rate = 16000;
	uint32_t conversion_ratio = input_sample_rate / output_sample_rate;

	int16_t input_samples[] = {1000, 2000, 3000, 4000,  5000,  6000,
				   7000, 8000, 9000, 10000, 11000, 12000};
	size_t num_samples = ARRAY_SIZE(input_samples);
	size_t expected_output_samples = num_samples / conversion_ratio;
	int16_t output_samples[expected_output_samples];

	enum sample_rate_converter_filter filter = SAMPLE_RATE_FILTER_SIMPLE;
	size_t output_written;

	ret = sample_rate_converter_process(
		&conv_ctx, filter, input_samples, num_samples * sizeof(uint16_t), input_sample_rate,
		output_samples, expected_output_samples * sizeof(uint16_t), &output_written,
		output_sample_rate);

	zassert_equal(ret, 0, "Sample rate conversion process failed");
	zassert_equal(conv_ctx.input_sample_rate, input_sample_rate,
		      "Input sample rate not as expected");
	zassert_equal(conv_ctx.output_sample_rate, output_sample_rate,
		      "Output sample rate not as expected");
	zassert_equal(conv_ctx.conversion_ratio, conversion_ratio,
		      "Conversion ratio not as expected");
	zassert_equal(conv_ctx.conversion_direction, CONVERSION_DIR_DOWN,
		      "Conversion direction is not as expected");
	zassert_equal(conv_ctx.filter_type, filter, "Filter set incorrectly");
	zassert_equal(conv_ctx.input_buf.bytes_in_buf, 0, "Bytes in input buffer not as expected");
	zassert_equal(ring_buf_size_get(&conv_ctx.output_ringbuf), 0,
		      "Number of bytes in output ringbuffer not as expected %d",
		      ring_buf_size_get(&conv_ctx.output_ringbuf));

	zassert_equal(output_written, expected_output_samples * sizeof(uint16_t),
		      "Output size was not as expected (%d)", output_written);

	/* Verify output bytes, expected three input samples per output sample */
	for (int i = 0; i < expected_output_samples; i++) {
		zassert_within(output_samples[i], input_samples[i * 3], 1100);
	}
}

ZTEST(suite_sample_rate_converter, test_init_valid_interpolate_24khz_16bit)
{
	int ret;

	uint32_t input_sample_rate = 24000;
	uint32_t output_sample_rate = 48000;
	uint32_t conversion_ratio = output_sample_rate / input_sample_rate;
	enum sample_rate_converter_filter filter = SAMPLE_RATE_FILTER_SIMPLE;

	uint16_t input_samples[] = {2000, 4000, 6000, 8000, 10000, 12000};
	size_t num_samples = ARRAY_SIZE(input_samples);
	size_t expected_output_samples = num_samples * conversion_ratio;
	uint16_t output_samples[expected_output_samples];
	size_t output_written;

	ret = sample_rate_converter_process(
		&conv_ctx, filter, input_samples, num_samples * sizeof(uint16_t), input_sample_rate,
		output_samples, expected_output_samples * sizeof(uint16_t), &output_written,
		output_sample_rate);

	zassert_equal(ret, 0, "Sample rate conversion process failed");
	zassert_equal(conv_ctx.input_sample_rate, input_sample_rate,
		      "Input sample rate not as expected");
	zassert_equal(conv_ctx.output_sample_rate, output_sample_rate,
		      "Output sample rate not as expected");
	zassert_equal(conv_ctx.conversion_ratio, conversion_ratio,
		      "Conversion ratio not as expected");
	zassert_equal(conv_ctx.conversion_direction, CONVERSION_DIR_UP,
		      "Conversion direction is not as expected");
	zassert_equal(conv_ctx.filter_type, filter, "Filter set incorrectly");
	zassert_equal(conv_ctx.input_buf.bytes_in_buf, 0, "Bytes in input buffer not as expected");
	zassert_equal(ring_buf_size_get(&conv_ctx.output_ringbuf), 0,
		      "Number of bytes in output ringbuffer not as expected %d",
		      ring_buf_size_get(&conv_ctx.output_ringbuf));

	zassert_equal(output_written, expected_output_samples * sizeof(uint16_t),
		      "Output size was not as expected (%d)", output_written);

	/* Verify output bytes, expect two samples in output per input */
	for (int i = 0; i < expected_output_samples; i++) {
		zassert_true(output_samples[i] < input_samples[(i / 2)],
			     "Output samples is not smaller than corresponding input");
	}
}

ZTEST(suite_sample_rate_converter, test_init_valid_interpolate_16khz_16bit)
{
	int ret;

	uint32_t input_sample_rate = 16000;
	uint32_t output_sample_rate = 48000;
	uint32_t conversion_ratio = output_sample_rate / input_sample_rate;
	enum sample_rate_converter_filter filter = SAMPLE_RATE_FILTER_SIMPLE;

	size_t num_samples = 4;
	uint16_t input_one[] = {1000, 2000, 3000, 4000};
	uint16_t input_two[] = {5000, 6000, 7000, 8000};
	uint16_t input_three[] = {9000, 10000, 11000, 12000};
	uint16_t input_four[] = {13000, 14000, 15000, 16000};

	size_t output_written;
	size_t expected_output_samples = num_samples * conversion_ratio;
	uint16_t output_samples[expected_output_samples];

	/* First run */
	ret = sample_rate_converter_process(
		&conv_ctx, filter, input_one, num_samples * sizeof(uint16_t), input_sample_rate,
		output_samples, expected_output_samples * sizeof(uint16_t), &output_written,
		output_sample_rate);

	zassert_equal(ret, 0, "Sample rate conversion process failed");
	zassert_equal(conv_ctx.input_sample_rate, input_sample_rate,
		      "Input sample rate not as expected %d", conv_ctx.input_sample_rate);
	zassert_equal(conv_ctx.output_sample_rate, output_sample_rate,
		      "Output sample rate not as expected");
	zassert_equal(conv_ctx.conversion_ratio, conversion_ratio,
		      "Conversion ratio not as expected");
	zassert_equal(conv_ctx.conversion_direction, CONVERSION_DIR_UP,
		      "Conversion direction is not as expected");
	zassert_equal(conv_ctx.filter_type, filter, "Filter set incorrectly");
	zassert_equal(conv_ctx.input_buf.bytes_in_buf, 0,
		      "Bytes in input buffer not as expected %d", conv_ctx.input_buf.bytes_in_buf);
	zassert_equal(ring_buf_size_get(&conv_ctx.output_ringbuf), 12,
		      "Number of bytes in output ringbuffer not as expected %d",
		      ring_buf_size_get(&conv_ctx.output_ringbuf));

	zassert_equal(output_written, expected_output_samples * sizeof(uint16_t),
		      "Output size was not as expected (%d)", output_written);

	/* Verify output bytes, expected three samples in output per input */
	for (int i = 0; i < expected_output_samples; i++) {
		zassert(output_samples[i] < input_one[(i / 3)],
			"Output samples is not smaller than corresponding input");
	}

	/* Second run */
	ret = sample_rate_converter_process(
		&conv_ctx, filter, input_two, num_samples * sizeof(uint16_t), input_sample_rate,
		output_samples, expected_output_samples * sizeof(uint16_t), &output_written,
		output_sample_rate);

	zassert_equal(ret, 0, "Sample rate conversion process failed");
	zassert_equal(conv_ctx.input_sample_rate, input_sample_rate,
		      "Input sample rate not as expected %d", conv_ctx.input_sample_rate);
	zassert_equal(conv_ctx.output_sample_rate, output_sample_rate,
		      "Output sample rate not as expected");
	zassert_equal(conv_ctx.conversion_ratio, conversion_ratio,
		      "Conversion ratio not as expected");
	zassert_equal(conv_ctx.conversion_direction, CONVERSION_DIR_UP,
		      "Conversion direction is not as expected");
	zassert_equal(conv_ctx.filter_type, filter, "Filter set incorrectly");
	zassert_equal(conv_ctx.input_buf.bytes_in_buf, 2,
		      "Bytes in input buffer not as expected %d", conv_ctx.input_buf.bytes_in_buf);
	zassert_equal(ring_buf_size_get(&conv_ctx.output_ringbuf), 6,
		      "Number of bytes in output ringbuffer not as expected %d",
		      ring_buf_size_get(&conv_ctx.output_ringbuf));

	uint16_t expected_input_buf_two[] = {8000};
	zassert_mem_equal(conv_ctx.input_buf.buf, expected_input_buf_two, 1 * sizeof(uint16_t));

	zassert_equal(output_written, expected_output_samples * sizeof(uint16_t),
		      "Output size was not as expected (%d)", output_written);

	/* Verify output bytes, expected three samples in output per input */
	for (int i = 0; i < expected_output_samples; i++) {
		zassert(output_samples[i] < input_two[(i / 3)],
			"Output samples is not smaller than corresponding input");
	}

	/* Third run */
	ret = sample_rate_converter_process(
		&conv_ctx, filter, input_three, num_samples * sizeof(uint16_t), input_sample_rate,
		output_samples, expected_output_samples * sizeof(uint16_t), &output_written,
		output_sample_rate);

	zassert_equal(ret, 0, "Sample rate conversion process failed");
	zassert_equal(conv_ctx.input_sample_rate, input_sample_rate,
		      "Input sample rate not as expected %d", conv_ctx.input_sample_rate);
	zassert_equal(conv_ctx.output_sample_rate, output_sample_rate,
		      "Output sample rate not as expected");
	zassert_equal(conv_ctx.conversion_ratio, conversion_ratio,
		      "Conversion ratio not as expected");
	zassert_equal(conv_ctx.conversion_direction, CONVERSION_DIR_UP,
		      "Conversion direction is not as expected");
	zassert_equal(conv_ctx.filter_type, filter, "Filter set incorrectly");
	zassert_equal(conv_ctx.input_buf.bytes_in_buf, 4,
		      "Bytes in input buffer not as expected %d", conv_ctx.input_buf.bytes_in_buf);
	zassert_equal(ring_buf_size_get(&conv_ctx.output_ringbuf), 0,
		      "Number of bytes in output ringbuffer not as expected %d",
		      ring_buf_size_get(&conv_ctx.output_ringbuf));

	uint16_t expected_input_buf_three[] = {11000, 12000};
	zassert_mem_equal(conv_ctx.input_buf.buf, expected_input_buf_three, 2 * sizeof(uint16_t));

	zassert_equal(output_written, expected_output_samples * sizeof(uint16_t),
		      "Output size was not as expected (%d)", output_written);

	/* Verify output bytes, expected three samples in output per input */
	for (int i = 0; i < expected_output_samples; i++) {
		zassert(output_samples[i] < input_three[(i / 3)],
			"Output samples is not smaller than corresponding input");
	}

	/* Fourth run */
	ret = sample_rate_converter_process(
		&conv_ctx, filter, input_four, num_samples * sizeof(uint16_t), input_sample_rate,
		output_samples, expected_output_samples * sizeof(uint16_t), &output_written,
		output_sample_rate);

	zassert_equal(ret, 0, "Sample rate conversion process failed");
	zassert_equal(conv_ctx.input_sample_rate, input_sample_rate,
		      "Input sample rate not as expected %d", conv_ctx.input_sample_rate);
	zassert_equal(conv_ctx.output_sample_rate, output_sample_rate,
		      "Output sample rate not as expected");
	zassert_equal(conv_ctx.conversion_ratio, conversion_ratio,
		      "Conversion ratio not as expected");
	zassert_equal(conv_ctx.conversion_direction, CONVERSION_DIR_UP,
		      "Conversion direction is not as expected");
	zassert_equal(conv_ctx.filter_type, filter, "Filter set incorrectly");
	zassert_equal(conv_ctx.input_buf.bytes_in_buf, 0,
		      "Bytes in input buffer not as expected %d", conv_ctx.input_buf.bytes_in_buf);
	zassert_equal(ring_buf_size_get(&conv_ctx.output_ringbuf), 12,
		      "Number of bytes in output ringbuffer not as expected %d",
		      ring_buf_size_get(&conv_ctx.output_ringbuf));

	zassert_equal(output_written, expected_output_samples * sizeof(uint16_t),
		      "Output size was not as expected (%d)", output_written);

	/* Verify output bytes, expected three samples in output per input */
	for (int i = 0; i < expected_output_samples; i++) {
		zassert(output_samples[i] < input_four[(i / 3)],
			"Output samples is not smaller than corresponding input");
	}
}
#endif /* CONFIG_SAMPLE_RATE_CONVERTER_BIT_DEPTH_16 */

#if CONFIG_SAMPLE_RATE_CONVERTER_BIT_DEPTH_32
ZTEST(suite_sample_rate_converter, test_init_valid_decimate_24khz_32bit)
{
	int ret;

	uint32_t input_sample_rate = 48000;
	uint32_t output_sample_rate = 24000;
	uint32_t conversion_ratio = input_sample_rate / output_sample_rate;

	uint32_t input_samples[] = {1000, 2000, 3000, 4000,  5000,  6000,
				    7000, 8000, 9000, 10000, 11000, 12000};
	size_t num_samples = ARRAY_SIZE(input_samples);
	size_t expected_output_samples = num_samples / conversion_ratio;
	uint32_t output_samples[expected_output_samples];

	enum sample_rate_converter_filter filter = SAMPLE_RATE_FILTER_SIMPLE;
	size_t output_size;

	ret = sample_rate_converter_process(
		&conv_ctx, filter, input_samples, num_samples * sizeof(uint32_t), input_sample_rate,
		output_samples, expected_output_samples * sizeof(uint16_t), &output_written,
		output_sample_rate);

	zassert_equal(ret, 0, "Sample rate conversion process failed");
	zassert_equal(conv_ctx.input_sample_rate, input_sample_rate,
		      "Input sample rate not as expected");
	zassert_equal(conv_ctx.output_sample_rate, output_sample_rate,
		      "Output sample rate not as expected");
	zassert_equal(conv_ctx.conversion_ratio, conversion_ratio,
		      "Conversion ratio not as expected");
	zassert_equal(conv_ctx.conversion_direction, CONVERSION_DIR_DOWN,
		      "Conversion direction is not as expected");
	zassert_equal(conv_ctx.filter_type, filter, "Filter set incorrectly");
	zassert_equal(conv_ctx.input_buf.bytes_in_buf, 0, "Bytes in input buffer not as expected");
	zassert_equal(ring_buf_size_get(&conv_ctx.output_ringbuf), 0,
		      "Number of bytes in output ringbuffer not as expected %d",
		      ring_buf_size_get(&conv_ctx.output_ringbuf));

	zassert_equal(output_written, expected_output_samples * sizeof(uint32_t),
		      "Output size was not as expected (%d)", output_written);

	/* Verify output bytes, expected two input samples per output sample */
	for (int i = 0; i < expected_output_samples; i++) {
		zassert_within(output_samples[i], input_samples[i * 2], 550);
	}
}

ZTEST(suite_sample_rate_converter, test_init_valid_decimate_16khz_32bit)
{
	int ret;

	uint32_t input_sample_rate = 48000;
	uint32_t output_sample_rate = 16000;
	uint32_t conversion_ratio = input_sample_rate / output_sample_rate;

	int32_t input_samples[] = {1000, 2000, 3000, 4000,  5000,  6000,
				   7000, 8000, 9000, 10000, 11000, 12000};
	size_t num_samples = ARRAY_SIZE(input_samples);
	size_t expected_output_samples = num_samples / conversion_ratio;
	int32_t output_samples[expected_output_samples];

	enum sample_rate_converter_filter filter = SAMPLE_RATE_FILTER_SIMPLE;
	size_t output_written;

	ret = sample_rate_converter_process(
		&conv_ctx, filter, input_samples, num_samples * sizeof(uint32_t), input_sample_rate,
		output_samples, expected_output_samples * sizeof(uint32_t), &output_written,
		output_sample_rate);

	zassert_equal(ret, 0, "Sample rate conversion process failed");
	zassert_equal(conv_ctx.input_sample_rate, input_sample_rate,
		      "Input sample rate not as expected");
	zassert_equal(conv_ctx.output_sample_rate, output_sample_rate,
		      "Output sample rate not as expected");
	zassert_equal(conv_ctx.conversion_ratio, conversion_ratio,
		      "Conversion ratio not as expected");
	zassert_equal(conv_ctx.conversion_direction, CONVERSION_DIR_DOWN,
		      "Conversion direction is not as expected");
	zassert_equal(conv_ctx.filter_type, filter, "Filter set incorrectly");
	zassert_equal(conv_ctx.input_buf.bytes_in_buf, 0, "Bytes in input buffer not as expected");
	zassert_equal(ring_buf_size_get(&conv_ctx.output_ringbuf), 0,
		      "Number of bytes in output ringbuffer not as expected %d",
		      ring_buf_size_get(&conv_ctx.output_ringbuf));

	zassert_equal(output_written, expected_output_samples * sizeof(uint32_t),
		      "Output size was not as expected (%d)", output_written);

	/* Verify output bytes, expected three input samples per output sample */
	for (int i = 0; i < expected_output_samples; i++) {
		zassert_within(output_samples[i], input_samples[i * 3], 1100);
	}
}

ZTEST(suite_sample_rate_converter, test_init_valid_interpolate_24khz_32bit)
{
	int ret;

	uint32_t input_sample_rate = 24000;
	uint32_t output_sample_rate = 48000;
	uint32_t conversion_ratio = output_sample_rate / input_sample_rate;
	enum sample_rate_converter_filter filter = SAMPLE_RATE_FILTER_SIMPLE;

	uint32_t input_samples[] = {2000, 4000, 6000, 8000, 10000, 12000};
	size_t num_samples = ARRAY_SIZE(input_samples);
	size_t expected_output_samples = num_samples * conversion_ratio;
	uint32_t output_samples[expected_output_samples];

	size_t output_written;

	ret = sample_rate_converter_process(
		&conv_ctx, filter, input_samples, num_samples * sizeof(uint32_t), input_sample_rate,
		output_samples, expected_output_samples * sizeof(uint16_t), &output_written,
		output_sample_rate);

	zassert_equal(ret, 0, "Sample rate conversion process failed");
	zassert_equal(conv_ctx.input_sample_rate, input_sample_rate,
		      "Input sample rate not as expected");
	zassert_equal(conv_ctx.output_sample_rate, output_sample_rate,
		      "Output sample rate not as expected");
	zassert_equal(conv_ctx.conversion_ratio, conversion_ratio,
		      "Conversion ratio not as expected");
	zassert_equal(conv_ctx.conversion_direction, CONVERSION_DIR_UP,
		      "Conversion direction is not as expected");
	zassert_equal(conv_ctx.filter_type, filter, "Filter set incorrectly");
	zassert_equal(conv_ctx.input_buf.bytes_in_buf, 0, "Bytes in input buffer not as expected");
	zassert_equal(ring_buf_size_get(&conv_ctx.output_ringbuf), 0,
		      "Number of bytes in output ringbuffer not as expected %d",
		      ring_buf_size_get(&conv_ctx.output_ringbuf));

	zassert_equal(output_written, expected_output_samples * sizeof(uint32_t),
		      "Output size was not as expected (%d)", output_written);

	/* Verify output bytes, expected two samples in output per input */
	for (int i = 0; i < expected_output_samples; i++) {
		zassert_true(output_samples[i] < input_samples[(i / 2)], "not correct");
	}
}

ZTEST(suite_sample_rate_converter, test_init_valid_interpolate_16khz_32bit)
{
	int ret;

	uint32_t input_sample_rate = 16000;
	uint32_t output_sample_rate = 48000;
	uint32_t conversion_ratio = output_sample_rate / input_sample_rate;
	enum sample_rate_converter_filter filter = SAMPLE_RATE_FILTER_SIMPLE;

	size_t num_samples = 4;
	int32_t input_one[] = {1000, 2000, 3000, 4000};
	int32_t input_two[] = {5000, 6000, 7000, 8000};
	int32_t input_three[] = {9000, 10000, 11000, 12000};
	int32_t input_four[] = {13000, 14000, 15000, 16000};

	size_t output_size;
	size_t expected_output_samples = num_samples * conversion_ratio;
	int32_t output_samples[expected_output_samples];

	/* First run */
	ret = sample_rate_converter_process(
		&conv_ctx, filter, input_one, num_samples * sizeof(uint32_t), input_sample_rate,
		output_samples, expected_output_samples * sizeof(uint32_t), &output_written,
		output_sample_rate);

	zassert_equal(ret, 0, "Sample rate conversion process failed");
	zassert_equal(conv_ctx.input_sample_rate, input_sample_rate,
		      "Input sample rate not as expected %d", conv_ctx.input_sample_rate);
	zassert_equal(conv_ctx.output_sample_rate, output_sample_rate,
		      "Output sample rate not as expected");
	zassert_equal(conv_ctx.conversion_ratio, conversion_ratio,
		      "Conversion ratio not as expected");
	zassert_equal(conv_ctx.conversion_direction, CONVERSION_DIR_UP,
		      "Conversion direction is not as expected");
	zassert_equal(conv_ctx.filter_type, filter, "Filter set incorrectly");
	zassert_equal(conv_ctx.input_buf.bytes_in_buf, 0,
		      "Bytes in input buffer not as expected (%d)",
		      conv_ctx.input_buf.bytes_in_buf);
	zassert_equal(ring_buf_size_get(&conv_ctx.output_ringbuf), 24,
		      "Number of bytes in output ringbuffer not as expected %d",
		      ring_buf_size_get(&conv_ctx.output_ringbuf));

	zassert_equal(output_written, expected_output_samples * sizeof(uint32_t),
		      "Output size was not as expected (%d)", output_written);

	/* Verify output bytes, expected three samples in output per input */
	for (int i = 0; i < expected_output_samples; i++) {
		zassert(output_samples[i] < input_one[(i / 3)],
			"Output samples is not smaller than corresponding input");
	}

	/* Second run */
	ret = sample_rate_converter_process(
		&conv_ctx, filter, input_two, num_samples * sizeof(uint32_t), input_sample_rate,
		output_samples, expected_output_samples * sizeof(uint32_t), &output_written,
		output_sample_rate);

	zassert_equal(ret, 0, "Sample rate conversion process failed");
	zassert_equal(conv_ctx.input_sample_rate, input_sample_rate,
		      "Input sample rate not as expected %d", conv_ctx.input_sample_rate);
	zassert_equal(conv_ctx.output_sample_rate, output_sample_rate,
		      "Output sample rate not as expected");
	zassert_equal(conv_ctx.conversion_ratio, conversion_ratio,
		      "Conversion ratio not as expected");
	zassert_equal(conv_ctx.conversion_direction, CONVERSION_DIR_UP,
		      "Conversion direction is not as expected");
	zassert_equal(conv_ctx.filter_type, filter, "Filter set incorrectly");
	zassert_equal(conv_ctx.input_buf.bytes_in_buf, 4,
		      "Bytes in input buffer not as expected %d", conv_ctx.input_buf.bytes_in_buf);
	zassert_equal(ring_buf_size_get(&conv_ctx.output_ringbuf), 12,
		      "Number of bytes in output ringbuffer not as expected %d",
		      ring_buf_size_get(&conv_ctx.output_ringbuf));

	uint32_t expected_input_buf_two[] = {8000};
	zassert_mem_equal(conv_ctx.input_buf.buf, expected_input_buf_two, 1 * sizeof(uint32_t));

	zassert_equal(output_written, expected_output_samples * sizeof(uint32_t),
		      "Output size was not as expected (%d)", output_written);

	/* Verify output bytes, expected three samples in output per input */
	for (int i = 0; i < expected_output_samples; i++) {
		zassert(output_samples[i] < input_two[(i / 3)],
			"Output samples is not smaller than corresponding input");
	}

	/* Third run */
	ret = sample_rate_converter_process(
		&conv_ctx, filter, input_three, num_samples * sizeof(uint32_t), input_sample_rate,
		output_samples, expected_output_samples, &output_written, output_sample_rate);

	zassert_equal(ret, 0, "Sample rate conversion process failed");
	zassert_equal(conv_ctx.input_sample_rate, input_sample_rate,
		      "Input sample rate not as expected %d", conv_ctx.input_sample_rate);
	zassert_equal(conv_ctx.output_sample_rate, output_sample_rate,
		      "Output sample rate not as expected");
	zassert_equal(conv_ctx.conversion_ratio, conversion_ratio,
		      "Conversion ratio not as expected");
	zassert_equal(conv_ctx.conversion_direction, CONVERSION_DIR_UP,
		      "Conversion direction is not as expected");
	zassert_equal(conv_ctx.filter_type, filter, "Filter set incorrectly");
	zassert_equal(conv_ctx.input_buf.bytes_in_buf, 8,
		      "Bytes in input buffer not as expected %d", conv_ctx.input_buf.bytes_in_buf);
	zassert_equal(ring_buf_size_get(&conv_ctx.output_ringbuf), 0,
		      "Number of bytes in output ringbuffer not as expected %d",
		      ring_buf_size_get(&conv_ctx.output_ringbuf));

	uint32_t expected_input_buf_three[] = {11000, 12000};
	zassert_mem_equal(conv_ctx.input_buf.buf, expected_input_buf_three, 2 * sizeof(uint32_t));

	zassert_equal(output_written, expected_output_samples * sizeof(uint32_t),
		      "Output size was not as expected (%d)", output_written);

	/* Verify output bytes, expected three samples in output per input */
	for (int i = 0; i < expected_output_samples; i++) {
		zassert(output_samples[i] < input_three[(i / 3)],
			"Output samples is not smaller than corresponding input");
	}

	/* Fourth run */
	ret = sample_rate_converter_process(
		&conv_ctx, filter, input_four, num_samples * sizeof(uint32_t), input_sample_rate,
		output_samples, expected_output_samples, &output_written, output_sample_rate);

	zassert_equal(ret, 0, "Sample rate conversion process failed");
	zassert_equal(conv_ctx.input_sample_rate, input_sample_rate,
		      "Input sample rate not as expected %d", conv_ctx.input_sample_rate);
	zassert_equal(conv_ctx.output_sample_rate, output_sample_rate,
		      "Output sample rate not as expected");
	zassert_equal(conv_ctx.conversion_ratio, conversion_ratio,
		      "Conversion ratio not as expected");
	zassert_equal(conv_ctx.conversion_direction, CONVERSION_DIR_UP,
		      "Conversion direction is not as expected");
	zassert_equal(conv_ctx.filter_type, filter, "Filter set incorrectly");
	zassert_equal(conv_ctx.input_buf.bytes_in_buf, 0,
		      "Bytes in input buffer not as expected %d", conv_ctx.input_buf.bytes_in_buf);
	zassert_equal(ring_buf_size_get(&conv_ctx.output_ringbuf), 24,
		      "Number of bytes in output ringbuffer not as expected %d",
		      ring_buf_size_get(&conv_ctx.output_ringbuf));

	zassert_equal(output_written, expected_output_samples * sizeof(uint32_t),
		      "Output size was not as expected (%d)", output_written);

	/* Verify output bytes, expected three samples in output per input */
	for (int i = 0; i < expected_output_samples; i++) {
		zassert(output_samples[i] < input_four[(i / 3)],
			"Output samples is not smaller than corresponding input");
	}
}
#endif /* CONFIG_SAMPLE_RATE_CONVERTER_BIT_DEPTH_32 */

ZTEST(suite_sample_rate_converter, test_init_valid_sample_rates_changed)
{
	int ret;

	enum sample_rate_converter_filter filter = SAMPLE_RATE_FILTER_SIMPLE;

	uint32_t original_input_rate = 48000;
	uint32_t original_output_rate = 16000;

	uint32_t new_input_rate = 16000;
	uint32_t new_output_rate = 48000;
	uint32_t new_conversion_ratio = new_output_rate / new_input_rate;

	uint16_t input_samples[] = {1000, 2000, 3000, 4000,  5000,  6000,
				    7000, 8000, 9000, 10000, 11000, 12000};
	size_t num_samples = ARRAY_SIZE(input_samples);
	size_t expected_output_samples = num_samples * new_conversion_ratio;
	uint16_t output_bytes[expected_output_samples];
	size_t output_written;

	conv_ctx.input_sample_rate = original_input_rate;
	conv_ctx.output_sample_rate = original_output_rate;

	ret = sample_rate_converter_process(
		&conv_ctx, filter, input_samples, num_samples * sizeof(uint16_t), new_input_rate,
		output_bytes, expected_output_samples * sizeof(uint16_t), &output_written,
		new_output_rate);

	zassert_equal(ret, 0, "Sample rate conversion process failed");
	zassert_equal(conv_ctx.input_sample_rate, new_input_rate,
		      "Input sample rate not as expected");
	zassert_equal(conv_ctx.output_sample_rate, new_output_rate,
		      "Output sample rate not as expected");
}

ZTEST(suite_sample_rate_converter, test_init_invalid_sample_rates)
{
	int ret;

	uint16_t input_samples[] = {1000, 2000, 3000, 4000,  5000,  6000,
				    7000, 8000, 9000, 10000, 11000, 12000};
	size_t num_samples = ARRAY_SIZE(input_samples);
	size_t expected_output_samples = num_samples / 3;
	uint16_t output_samples[expected_output_samples];
	size_t output_written;

	uint32_t input_rate;
	uint32_t output_rate;

	/* Downsampling from NOT 48000 */
	input_rate = 24000;
	output_rate = 16000;

	ret = sample_rate_converter_process(
		&conv_ctx, SAMPLE_RATE_FILTER_SIMPLE, input_samples, num_samples * sizeof(uint16_t),
		input_rate, output_samples, expected_output_samples, &output_written, output_rate);

	zassert_equal(ret, -EINVAL, "Sample rate conversion process did not fail");

	/* Downsampling to invalid rate */
	input_rate = 48000;
	output_rate = 20000;

	ret = sample_rate_converter_process(
		&conv_ctx, SAMPLE_RATE_FILTER_SIMPLE, input_samples, num_samples * sizeof(uint16_t),
		input_rate, output_samples, expected_output_samples, &output_written, output_rate);

	zassert_equal(ret, -EINVAL, "Sample rate conversion process did not fail");

	/* Upsampling to NOT 48000 */
	input_rate = 16000;
	output_rate = 24000;

	ret = sample_rate_converter_process(
		&conv_ctx, SAMPLE_RATE_FILTER_SIMPLE, input_samples, num_samples * sizeof(uint16_t),
		input_rate, output_samples, expected_output_samples, &output_written, output_rate);

	zassert_equal(ret, -EINVAL, "Sample rate conversion process did not fail");

	/* Upsampling to invalid rate */
	input_rate = 24000;
	output_rate = 30000;

	ret = sample_rate_converter_process(
		&conv_ctx, SAMPLE_RATE_FILTER_SIMPLE, input_samples, num_samples * sizeof(uint16_t),
		input_rate, output_samples, expected_output_samples, &output_written, output_rate);

	zassert_equal(ret, -EINVAL, "Sample rate conversion process did not fail");
}

ZTEST(suite_sample_rate_converter, test_init_invalid_sample_rates_equal)
{
	int ret;

	uint16_t input_samples[] = {1000, 2000, 3000, 4000,  5000,  6000,
				    7000, 8000, 9000, 10000, 11000, 12000};
	size_t num_samples = ARRAY_SIZE(input_samples);
	size_t expected_output_samples = num_samples / 3;
	uint16_t output_samples[expected_output_samples];
	size_t output_written;

	uint32_t sample_rate = 48000;

	ret = sample_rate_converter_process(
		&conv_ctx, SAMPLE_RATE_FILTER_SIMPLE, input_samples, num_samples * sizeof(uint16_t),
		sample_rate, output_samples, expected_output_samples, &output_written, sample_rate);

	zassert_equal(-EINVAL, ret,
		      "Process did not fail when input and out sample rate is the same");
}

ZTEST(suite_sample_rate_converter, test_init_valid_filter_changed)
{
	int ret;

	uint32_t input_sample_rate = 48000;
	uint32_t output_sample_rate = 24000;
	uint32_t conversion_ratio = input_sample_rate / output_sample_rate;

	uint16_t input_samples[] = {1000, 2000, 3000, 4000,  5000,  6000,
				    7000, 8000, 9000, 10000, 11000, 12000};
	size_t num_samples = ARRAY_SIZE(input_samples);
	size_t expected_output_samples = num_samples / conversion_ratio;
	uint16_t output_samples[expected_output_samples];
	size_t output_written;

	enum sample_rate_converter_filter original_filter = SAMPLE_RATE_FILTER_SMALL;
	enum sample_rate_converter_filter new_filter = SAMPLE_RATE_FILTER_SIMPLE;

	conv_ctx.input_sample_rate = input_sample_rate;
	conv_ctx.output_sample_rate = output_sample_rate;
	conv_ctx.filter_type = original_filter;

	ret = sample_rate_converter_process(
		&conv_ctx, new_filter, input_samples, num_samples * sizeof(uint16_t),
		input_sample_rate, output_samples, expected_output_samples * sizeof(uint16_t),
		&output_written, output_sample_rate);

	zassert_equal(ret, 0, "Sample rate conversion process failed");
	zassert_equal(conv_ctx.input_sample_rate, input_sample_rate,
		      "Input sample rate not as expected");
	zassert_equal(conv_ctx.output_sample_rate, output_sample_rate,
		      "Output sample rate not as expected");
	zassert_equal(conv_ctx.filter_type, new_filter, "Filter set incorrectly");
}

ZTEST(suite_sample_rate_converter, test_invalid_process_ctx_null_ptr)
{
	int ret;

	uint16_t input_samples[] = {1000, 2000, 3000, 40000};
	size_t num_samples = ARRAY_SIZE(input_samples);
	size_t expected_output_samples = num_samples / 3;
	uint16_t output_samples[expected_output_samples];
	size_t output_written;

	uint32_t input_sample_rate = 48000;
	uint32_t output_sample_rate = 24000;
	enum sample_rate_converter_filter filter = SAMPLE_RATE_FILTER_SIMPLE;

	ret = sample_rate_converter_process(
		NULL, filter, input_samples, num_samples * sizeof(uint16_t), input_sample_rate,
		output_samples, expected_output_samples, &output_written, output_sample_rate);

	zassert_equal(ret, -EINVAL, "Sample rate conversion process did not fail");
}

ZTEST(suite_sample_rate_converter, test_invalid_process_buffer_null_ptr)
{
	int ret;

	uint16_t input_samples[] = {1000, 2000, 3000, 40000};
	size_t num_samples = ARRAY_SIZE(input_samples);
	size_t expected_output_samples = num_samples / 3;
	uint16_t output_samples[expected_output_samples];
	size_t output_written;

	uint32_t input_sample_rate = 48000;
	uint32_t output_sample_rate = 24000;
	enum sample_rate_converter_filter filter = SAMPLE_RATE_FILTER_SIMPLE;

	ret = sample_rate_converter_process(
		&conv_ctx, filter, NULL, num_samples * sizeof(uint16_t), input_sample_rate,
		output_samples, expected_output_samples, &output_written, output_sample_rate);

	zassert_equal(ret, -EINVAL,
		      "Sample rate conversion process did not fail when input pointer is NULL");

	ret = sample_rate_converter_process(&conv_ctx, filter, NULL, num_samples * sizeof(uint16_t),
					    input_sample_rate, NULL, expected_output_samples,
					    &output_written, output_sample_rate);

	zassert_equal(ret, -EINVAL,
		      "Sample rate conversion process did not fail when output pointer is NULL");
}

ZTEST(suite_sample_rate_converter, test_invalid_process_output_written_null_ptr)
{
	int ret;

	uint16_t input_samples[] = {1000, 2000, 3000, 40000};
	size_t num_samples = ARRAY_SIZE(input_samples);
	size_t expected_output_samples = num_samples / 3;
	uint16_t output_samples[expected_output_samples];

	uint32_t input_sample_rate = 48000;
	uint32_t output_sample_rate = 24000;
	enum sample_rate_converter_filter filter = SAMPLE_RATE_FILTER_SIMPLE;

	ret = sample_rate_converter_process(
		&conv_ctx, filter, input_samples, num_samples * sizeof(uint16_t), input_sample_rate,
		output_samples, expected_output_samples, NULL, output_sample_rate);

	zassert_equal(
		ret, -EINVAL,
		"Sample rate conversion process did not fail when output size pouinter is NULL");
}

ZTEST(suite_sample_rate_converter, test_invalid_open_null_ptr)
{
	int ret;

	ret = sample_rate_converter_open(NULL);

	zassert_equal(ret, -EINVAL, "Call to open did not fail");
}

ZTEST(suite_sample_rate_converter, test_valid_process_zero_size_input)
{
	int ret;

	uint16_t input_samples[] = {1000, 2000, 3000, 40000};
	size_t num_samples = ARRAY_SIZE(input_samples);
	size_t expected_output_samples = num_samples * 2;
	uint16_t output_samples[expected_output_samples];
	size_t output_written;

	uint32_t input_sample_rate = 24000;
	uint32_t output_sample_rate = 48000;
	uint32_t conversion_ratio = output_sample_rate / input_sample_rate;
	enum sample_rate_converter_filter filter = SAMPLE_RATE_FILTER_SIMPLE;

	ret = sample_rate_converter_process(&conv_ctx, filter, input_samples, 0, input_sample_rate,
					    output_samples, expected_output_samples,
					    &output_written, output_sample_rate);

	zassert_equal(ret, 0, "Sample rate conversion process failed");
	zassert_equal(conv_ctx.input_sample_rate, input_sample_rate,
		      "Input sample rate not as expected");
	zassert_equal(conv_ctx.output_sample_rate, output_sample_rate,
		      "Output sample rate not as expected");
	zassert_equal(conv_ctx.conversion_ratio, conversion_ratio,
		      "Conversion ratio not as expected");
	zassert_equal(conv_ctx.conversion_direction, CONVERSION_DIR_UP,
		      "Conversion direction is not as expected");
	zassert_equal(conv_ctx.filter_type, filter, "Filter not as expected");
	zassert_equal(conv_ctx.input_buf.bytes_in_buf, 0, "Bytes in input buffer not as expected");
	zassert_equal(ring_buf_size_get(&conv_ctx.output_ringbuf), 0,
		      "Number of bytes in output ringbuffer not as expected %d",
		      ring_buf_size_get(&conv_ctx.output_ringbuf));

	zassert_equal(output_written, 0, "Received %d output samples when none was expected",
		      output_written);
}

ZTEST(suite_sample_rate_converter, test_valid_process_input_one_sample_interpolate)
{
	int ret;

	uint32_t input_sample_rate = 24000;
	uint32_t output_sample_rate = 48000;
	uint32_t conversion_ratio = output_sample_rate / input_sample_rate;
	enum sample_rate_converter_filter filter = SAMPLE_RATE_FILTER_SIMPLE;

	uint16_t input_samples[] = {1000};
	size_t num_samples = ARRAY_SIZE(input_samples);
	size_t expected_output_samples = num_samples * conversion_ratio;
	uint16_t output_samples[expected_output_samples];
	size_t output_written;

	ret = sample_rate_converter_process(
		&conv_ctx, filter, input_samples, num_samples * sizeof(uint16_t), input_sample_rate,
		output_samples, expected_output_samples * sizeof(uint16_t), &output_written,
		output_sample_rate);

	zassert_equal(ret, 0, "Sample rate conversion process failed");
	zassert_equal(conv_ctx.input_sample_rate, input_sample_rate,
		      "Input sample rate not as expected");
	zassert_equal(conv_ctx.output_sample_rate, output_sample_rate,
		      "Output sample rate not as expected");
	zassert_equal(conv_ctx.conversion_ratio, conversion_ratio,
		      "Conversion ratio not as expected");
	zassert_equal(conv_ctx.conversion_direction, CONVERSION_DIR_UP,
		      "Conversion direction is not as expected");
	zassert_equal(conv_ctx.filter_type, filter, "Filter not as expected");
	zassert_equal(conv_ctx.input_buf.bytes_in_buf, 0, "Bytes in input buffer not as expected");
	zassert_equal(ring_buf_size_get(&conv_ctx.output_ringbuf), 0,
		      "Number of bytes in output ringbuffer not as expected %d",
		      ring_buf_size_get(&conv_ctx.output_ringbuf));

	zassert_equal(output_written, expected_output_samples * sizeof(uint16_t),
		      "Received %d output samples when none was expected", output_written);

	zassert_within(output_samples[0], input_samples[0], 550);
	zassert_within(output_samples[1], input_samples[0], 550);
}

ZTEST(suite_sample_rate_converter, test_valid_process_input_two_samples_decimate)
{
	int ret;

	uint32_t input_sample_rate = 48000;
	uint32_t output_sample_rate = 24000;
	uint32_t conversion_ratio = input_sample_rate / output_sample_rate;
	enum sample_rate_converter_filter filter = SAMPLE_RATE_FILTER_SIMPLE;

	uint16_t input_samples[] = {1000, 2000};
	size_t num_samples = ARRAY_SIZE(input_samples);
	size_t expected_output_samples = num_samples / conversion_ratio;
	uint16_t output_samples[expected_output_samples];
	size_t output_written;

	ret = sample_rate_converter_process(
		&conv_ctx, filter, input_samples, num_samples * sizeof(uint16_t), input_sample_rate,
		output_samples, expected_output_samples * sizeof(uint16_t), &output_written,
		output_sample_rate);

	zassert_equal(ret, 0, "Sample rate conversion process failed");
	zassert_equal(conv_ctx.input_sample_rate, input_sample_rate,
		      "Input sample rate not as expected");
	zassert_equal(conv_ctx.output_sample_rate, output_sample_rate,
		      "Output sample rate not as expected");
	zassert_equal(conv_ctx.conversion_ratio, conversion_ratio,
		      "Conversion ratio not as expected");
	zassert_equal(conv_ctx.conversion_direction, CONVERSION_DIR_DOWN,
		      "Conversion direction is not as expected");
	zassert_equal(conv_ctx.filter_type, filter, "Filter not as expected");
	zassert_equal(conv_ctx.input_buf.bytes_in_buf, 0, "Bytes in input buffer not as expected");
	zassert_equal(ring_buf_size_get(&conv_ctx.output_ringbuf), 0,
		      "Number of bytes in output ringbuffer not as expected %d",
		      ring_buf_size_get(&conv_ctx.output_ringbuf));

	zassert_equal(output_written, expected_output_samples * sizeof(uint16_t),
		      "Received %d output samples when none was expected", output_written);

	zassert_within(output_samples[0], input_samples[0], 550);
}

ZTEST(suite_sample_rate_converter, test_invalid_process_input_samples_less_than_ratio)
{
	int ret;

	uint32_t input_sample_rate = 48000;
	uint32_t output_sample_rate = 24000;
	uint32_t conversion_ratio = input_sample_rate / output_sample_rate;
	enum sample_rate_converter_filter filter = SAMPLE_RATE_FILTER_SIMPLE;

	uint16_t input_samples[] = {1000};
	size_t num_samples = ARRAY_SIZE(input_samples);
	size_t expected_output_samples = num_samples / conversion_ratio;
	uint16_t output_samples[expected_output_samples];
	size_t output_written;

	ret = sample_rate_converter_process(
		&conv_ctx, filter, input_samples, num_samples * sizeof(uint16_t), input_sample_rate,
		output_samples, expected_output_samples, &output_written, output_sample_rate);

	zassert_equal(ret, -EINVAL, "Sample rate conversion process failed");
}

ZTEST(suite_sample_rate_converter, test_invalid_process_input_not_multiple)
{
	int ret;

	uint32_t input_sample_rate = 48000;
	uint32_t output_sample_rate = 24000;
	enum sample_rate_converter_filter filter = SAMPLE_RATE_FILTER_SIMPLE;

	uint8_t input_samples[] = {1, 2, 3, 4, 5};
	size_t num_bytes = ARRAY_SIZE(input_samples);
	size_t expected_output_samples = 1;
	uint16_t output_samples[expected_output_samples];
	size_t output_written;

	ret = sample_rate_converter_process(
		&conv_ctx, filter, input_samples, num_bytes, input_sample_rate, output_samples,
		expected_output_samples, &output_written, output_sample_rate);

	zassert_equal(ret, -EINVAL,
		      "Sample rate conversion process did not fail when number of input is not a "
		      "16- or 32-bit multiple");
}

ZTEST(suite_sample_rate_converter, test_invalid_process_input_array_too_large)
{
	int ret;

	uint32_t input_sample_rate = 48000;
	uint32_t output_sample_rate = 24000;
	uint32_t conversion_ratio = input_sample_rate / output_sample_rate;

	size_t num_samples = 500;
	uint16_t input_samples[num_samples];
	size_t expected_output_samples = num_samples / conversion_ratio;
	uint16_t output_samples[expected_output_samples];

	enum sample_rate_converter_filter filter = SAMPLE_RATE_FILTER_SIMPLE;
	size_t output_written;

	ret = sample_rate_converter_process(
		&conv_ctx, filter, input_samples, num_samples * sizeof(uint16_t), input_sample_rate,
		output_samples, expected_output_samples, &output_written, output_sample_rate);

	zassert_equal(
		ret, -EINVAL,
		"Sample rate conversion did not fail when number of input samples is too large");
}

ZTEST(suite_sample_rate_converter, test_invalid_process_output_buf_too_small)
{
	int ret;

	uint32_t input_sample_rate = 48000;
	uint32_t output_sample_rate = 24000;
	uint32_t conversion_ratio = input_sample_rate / output_sample_rate;

	uint16_t input_samples[] = {1000, 2000, 3000, 4000,  5000,  6000,
				    7000, 8000, 9000, 10000, 11000, 12000};
	size_t num_samples = ARRAY_SIZE(input_samples);
	size_t expected_output_samples = num_samples / conversion_ratio;
	/* expected_output_samples is 6, so this is set to 5 */
	uint16_t output_samples[5];

	enum sample_rate_converter_filter filter = SAMPLE_RATE_FILTER_SIMPLE;
	size_t output_written;

	ret = sample_rate_converter_process(
		&conv_ctx, filter, input_samples, num_samples * sizeof(uint16_t), input_sample_rate,
		output_samples, expected_output_samples, &output_written, output_sample_rate);

	zassert_equal(ret, -EINVAL,
		      "Sample rate conversion process did not fail when output buffer is to small");
}

ZTEST(suite_sample_rate_converter, test_invalid_bytes_produced_too_large_for_internal_buf)
{
	int ret;

	uint32_t input_sample_rate = 24000;
	uint32_t output_sample_rate = 48000;
	uint32_t conversion_ratio = output_sample_rate / input_sample_rate;

	size_t num_samples = 300;
	uint16_t input_samples[num_samples];
	size_t expected_output_samples = num_samples * conversion_ratio;
	uint16_t output_samples[expected_output_samples];

	enum sample_rate_converter_filter filter = SAMPLE_RATE_FILTER_SIMPLE;
	size_t output_written;

	ret = sample_rate_converter_process(
		&conv_ctx, filter, input_samples, num_samples * sizeof(uint16_t), input_sample_rate,
		output_samples, expected_output_samples * sizeof(uint16_t), &output_written,
		output_sample_rate);

	zassert_equal(ret, -EINVAL,
		      "Sample rate conversion did not fail when the number of produced output "
		      "samples is larger than the internal output buf");
}

ZTEST_SUITE(suite_sample_rate_converter, NULL, NULL, test_setup, NULL, NULL);

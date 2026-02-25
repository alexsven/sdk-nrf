/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/timing/timing.h>
#include <zephyr/sys/printk.h>
#include <nrfx_clock.h>
#include "tone.h"
#include "sw_codec_lc3.h"
#include <lc3.h>

#define FRAME_DURATION_US     10000
#define SAMPLE_RATE	      48000
#define FRAME_SAMPLES	      ((SAMPLE_RATE * FRAME_DURATION_US) / 1000000)
#define PCM_BIT_DEPTH	      16
#define BITRATE		      96000
#define NUM_CHANNELS	      1
#define AUDIO_CH	      0
#define HFCLKAUDIO_12_288_MHZ 0x9BA6
#define ITERATIONS	      100
#define EMPTY_CELL_SIZE	      12
#define DIVIDER_CELL_SIZE     9

static uint16_t frequencies[] = {100, 200, 400, 800, 1000, 2000, 4000, 8000, 10000};
static uint32_t sample_rates[] = {16000, 24000, 48000};

static uint8_t encoded_data[400];
static uint16_t encoded_size;

struct lc3_results {
	uint16_t enc_times_us[ARRAY_SIZE(sample_rates)][ARRAY_SIZE(frequencies)];
	char *enc_name;
};

int pcm_buf_gen_and_populate(int16_t *pcm_buf, size_t pcm_bytes_req, uint16_t tone_freq_hz,
			     uint32_t sample_rate)
{
	int ret;

	int16_t tone[960]; /* Max samples for 48kHz @ 10ms */
	size_t tone_size;

	ret = tone_gen(tone, &tone_size, tone_freq_hz, sample_rate, 1);
	if (ret) {
		printk("Failed to generate tone: %d\n", ret);
		return ret;
	}

	size_t num_copies_to_fill_pcm_buf = pcm_bytes_req / tone_size;

	for (size_t i = 0; i < num_copies_to_fill_pcm_buf; i++) {
		memcpy(&pcm_buf[i * (tone_size / sizeof(int16_t))], tone, tone_size);
	}

	return 0;
}

int t2_software_lc3_run(struct lc3_results *results)
{
	int ret;
	timing_t start_time, end_time;
	uint64_t total_cycles;
	uint64_t total_ns;
	uint16_t pcm_bytes_req;

	results->enc_name = "T2";

	/* Initialize LC3 codec */
	ret = sw_codec_lc3_init(NULL, NULL, FRAME_DURATION_US);
	if (ret) {
		printk("Failed to initialize LC3 codec: %d\n", ret);
		return ret;
	}

	/* Loop through each sample rate */
	for (size_t sr_idx = 0; sr_idx < ARRAY_SIZE(sample_rates); sr_idx++) {
		uint32_t current_sample_rate = sample_rates[sr_idx];
		uint32_t current_frame_samples =
			(current_sample_rate * FRAME_DURATION_US) / 1000000;
		int16_t pcm_data_current[960]; /* Max samples for 48kHz @ 10ms */

		/* Initialize LC3 encoder for current sample rate */
		ret = sw_codec_lc3_enc_init(current_sample_rate, PCM_BIT_DEPTH, FRAME_DURATION_US,
					    BITRATE, NUM_CHANNELS, &pcm_bytes_req);
		if (ret) {
			printk("Failed to initialize LC3 encoder for %d Hz: %d\n",
			       current_sample_rate, ret);
			return ret;
		}

		for (size_t i = 0; i < ARRAY_SIZE(frequencies); i++) {

			ret = pcm_buf_gen_and_populate(pcm_data_current, pcm_bytes_req,
						       frequencies[i], current_sample_rate);
			if (ret) {
				printk("Failed to generate and populate PCM buffer: %d\n", ret);
				return ret;
			}

			uint32_t avg_enc_time_us = 0;

			for (int j = 0; j < ITERATIONS; j++) {

				start_time = timing_counter_get();
				ret = sw_codec_lc3_enc_run(
					pcm_data_current, current_frame_samples * sizeof(int16_t),
					LC3_USE_BITRATE_FROM_INIT, AUDIO_CH, sizeof(encoded_data),
					encoded_data, &encoded_size);
				end_time = timing_counter_get();

				if (ret) {
					printk("Failed to encode: %d\n", ret);
					return ret;
				}

				total_cycles = timing_cycles_get(&start_time, &end_time);
				total_ns = timing_cycles_to_ns(total_cycles);
				avg_enc_time_us += total_ns / 1000;
			}

			avg_enc_time_us /= ITERATIONS;
			results->enc_times_us[sr_idx][i] = avg_enc_time_us;
		}

		/* Uninitialize encoder for this sample rate */
		sw_codec_lc3_enc_uninit_all();
	}

	return 0;
}

int liblc3_run(struct lc3_results *results)
{
	int ret;
	timing_t start_time, end_time;
	uint64_t total_cycles;
	uint64_t total_ns;

	results->enc_name = "LIB";

	/* Benchmark liblc3 codec */
	for (size_t sr_idx = 0; sr_idx < ARRAY_SIZE(sample_rates); sr_idx++) {
		uint32_t current_sample_rate = sample_rates[sr_idx];
		uint32_t current_frame_samples =
			(current_sample_rate * FRAME_DURATION_US) / 1000000;
		int16_t pcm_data_current[960]; /* Max samples for 48kHz @ 10ms */
		uint8_t encoded_data_liblc3[400];

		/* Calculate frame size for current sample rate and bitrate */
		int frame_bytes = lc3_frame_bytes(FRAME_DURATION_US, BITRATE);
		if (frame_bytes < 0) {
			printk("Failed to get frame bytes for liblc3\n");
			return -1;
		}

		/* Get encoder memory size and allocate */
		unsigned encoder_size = lc3_encoder_size(FRAME_DURATION_US, current_sample_rate);
		if (encoder_size == 0) {
			printk("Failed to get encoder size for %d Hz\n", current_sample_rate);
			continue;
		}

		/* Use static memory for encoder */
		lc3_encoder_mem_48k_t encoder_mem;
		lc3_encoder_t encoder =
			lc3_setup_encoder(FRAME_DURATION_US, current_sample_rate, 0, &encoder_mem);
		if (!encoder) {
			printk("Failed to setup liblc3 encoder for %d Hz\n", current_sample_rate);
			continue;
		}

		for (size_t i = 0; i < ARRAY_SIZE(frequencies); i++) {
			ret = pcm_buf_gen_and_populate(pcm_data_current,
						       current_frame_samples * sizeof(int16_t),
						       frequencies[i], current_sample_rate);
			if (ret) {
				printk("Failed to generate PCM data: %d\n", ret);
				return ret;
			}

			uint32_t avg_enc_time_us = 0;

			for (int j = 0; j < ITERATIONS; j++) {
				start_time = timing_counter_get();
				ret = lc3_encode(encoder, LC3_PCM_FORMAT_S16, pcm_data_current, 1,
						 frame_bytes, encoded_data_liblc3);
				end_time = timing_counter_get();

				if (ret != 0) {
					printk("Failed to encode with liblc3: %d\n", ret);
					return ret;
				}

				total_cycles = timing_cycles_get(&start_time, &end_time);
				total_ns = timing_cycles_to_ns(total_cycles);
				avg_enc_time_us += total_ns / 1000;
			}

			avg_enc_time_us /= ITERATIONS;
			results->enc_times_us[sr_idx][i] = avg_enc_time_us;
		}
	}

	return 0;
}

void empty_cell_print(size_t cell_size)
{
	printk("|");
	for (size_t i = 0; i < cell_size; i++) {
		printk(" ");
	}
}

void divider_line_print(size_t num_cells, size_t cell_size)
{
	for (size_t i = 0; i < num_cells; i++) {
		printk("|");
		for (size_t j = 0; j < cell_size; j++) {
			printk("-");
		}
	}
	printk("|\n");
}

void divider_line_no_bar_print(size_t cell_size)
{
	for (size_t j = 0; j < cell_size; j++) {
		printk("-");
	}
	printk("\n");
}

void results_print(struct lc3_results *t2_results, struct lc3_results *liblc3_results)
{
	for (size_t sr_idx = 0; sr_idx < ARRAY_SIZE(sample_rates); sr_idx++) {
		printk("|  \033[1m%5d Hz \033[0m ", sample_rates[sr_idx]);

		for (size_t i = 0; i < ARRAY_SIZE(frequencies); i++) {
			printk("|   \033[33m%4d\033[0m  ", t2_results->enc_times_us[sr_idx][i]);
		}

		printk("|\n");

		empty_cell_print(EMPTY_CELL_SIZE);

		for (size_t i = 0; i < ARRAY_SIZE(frequencies); i++) {
			printk("|   \033[32m%4d\033[0m  ", liblc3_results->enc_times_us[sr_idx][i]);
		}

		printk("|\n");

		empty_cell_print(EMPTY_CELL_SIZE);
		divider_line_print(ARRAY_SIZE(frequencies), DIVIDER_CELL_SIZE);

		printk("|        \033[1mDiff\033[0m");

		for (size_t i = 0; i < ARRAY_SIZE(frequencies); i++) {
			int t2_time = t2_results->enc_times_us[sr_idx][i];
			int liblc3_time = liblc3_results->enc_times_us[sr_idx][i];

			printk("|%4d\033[1m(%2d%%)\033[0m", t2_time - liblc3_time,
			       (t2_time - liblc3_time) * 100 / t2_time);
		}

		printk("|\n");

		divider_line_no_bar_print(EMPTY_CELL_SIZE + 2 +
					  ((DIVIDER_CELL_SIZE + 1) * (ARRAY_SIZE(frequencies))));
	}
}

int main(void)
{
	int ret;
	struct lc3_results t2_results;
	struct lc3_results liblc3_results;

	/* Enable HIGH frequency clock */
	ret = nrfx_clock_divider_set(NRF_CLOCK_DOMAIN_HFCLK, NRF_CLOCK_HFCLK_DIV_1);
	if (ret) {
		return ret;
	}

	nrfx_clock_hfclkaudio_config_set(HFCLKAUDIO_12_288_MHZ);

	NRF_CLOCK->TASKS_HFCLKAUDIOSTART = 1;

	/* Wait for ACLK to start */
	while (!NRF_CLOCK_EVENT_HFCLKAUDIOSTARTED) {
		k_sleep(K_MSEC(1));
	}

	printk("LC3 Encoding Benchmark\n");
	printk("Frame duration: %d us\n", FRAME_DURATION_US);

	/* Benchmark encoding */
	timing_init();
	timing_start();

	printk("Benchmarking LC3 encoding for different tone frequencies...\n");
	printk("Comparing \033[33mT2 software LC3\033[0m implementation against "
	       "\033[32mliblc3\033[0m\n");

	empty_cell_print(EMPTY_CELL_SIZE);
	divider_line_print(ARRAY_SIZE(frequencies), DIVIDER_CELL_SIZE);
	printk("| Time in µs ");

	/* Print frequency headers */
	for (size_t i = 0; i < ARRAY_SIZE(frequencies); i++) {
		printk("|\033[1m%6d Hz\033[0m", frequencies[i]);
	}

	printk("|\n");
	divider_line_no_bar_print(EMPTY_CELL_SIZE + 2 +
				  ((DIVIDER_CELL_SIZE + 1) * (ARRAY_SIZE(frequencies))));

	t2_software_lc3_run(&t2_results);

	liblc3_run(&liblc3_results);

	timing_stop();

	/* Print results */
	results_print(&t2_results, &liblc3_results);
	return 0;
}
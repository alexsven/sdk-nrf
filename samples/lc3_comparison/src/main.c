/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/timing/timing.h>
#include <zephyr/sys/printk.h>
#include <zephyr/fs/fs.h>
#include <zephyr/storage/disk_access.h>
#include <ff.h>
#include <nrfx_clock.h>
#include "tone.h"
#include "sw_codec_lc3.h"
#include <lc3.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

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

#define TEST_FILE_16KHZ "/SD:/TEST_A~3.WAV"
#define TEST_FILE_24KHZ "/SD:/TEST_A~2.WAV"
#define TEST_FILE_48KHZ "/SD:/TEST_A~1.WAV"

#define WAV_FORMAT_PCM	   1
#define MAX_WAV_FRAME_SIZE 960

/* WAV header structure */
struct wav_header {
	/* RIFF Header */
	char riff_header[4];
	uint32_t wav_size; /* File size excluding first eight bytes */
	char wav_head[4];  /* Contains "WAVE" */

	/* Format Header */
	char fmt_header[4];
	uint32_t wav_chunk_size; /* Should be 16 for PCM */
	short audio_format;	 /* Should be 1 for PCM */
	short num_channels;
	uint32_t sample_rate;
	uint32_t byte_rate;
	short block_alignment; /* num_channels * Bytes Per Sample */
	short bit_depth;

	/* Data */
	char data_header[4];
	uint32_t data_bytes; /* Number of bytes in data */
} __packed;

/* Structure to hold WAV file processing information */
struct wav_file_info {
	struct fs_file_t file;
	struct wav_header header;
	uint32_t current_sample_rate;
	uint32_t samples_per_frame;
	uint32_t bytes_per_frame;
	uint32_t total_samples;
	uint32_t total_frames;
};

static uint16_t frequencies[] = {100, 200, 400, 800, 1000, 2000, 4000, 8000, 10000};
static uint32_t sample_rates[] = {48000, 24000, 16000};

static uint8_t encoded_data[400];
static uint16_t encoded_size;

/* SD card filesystem implementation based on nRF5340 audio application */
static const char *sd_root_path = "/SD:";
static FATFS fat_fs;
static bool sd_init_success = false;

static struct fs_mount_t mnt_pt = {
	.type = FS_FATFS,
	.fs_data = &fat_fs,
};

struct lc3_results {
	uint16_t enc_times_us[ARRAY_SIZE(sample_rates)][ARRAY_SIZE(frequencies)];
	char *enc_name;
};

static int sd_card_init(void)
{
	int ret;
	static const char *sd_dev = "SD";
	uint64_t sd_card_size_bytes;
	uint32_t sector_count;
	size_t sector_size;

	ret = disk_access_init(sd_dev);
	if (ret) {
		printk("SD card init failed, please check if SD card inserted: %d\n", ret);
		return -ENODEV;
	}

	ret = disk_access_ioctl(sd_dev, DISK_IOCTL_GET_SECTOR_COUNT, &sector_count);
	if (ret) {
		printk("Unable to get sector count: %d\n", ret);
		return ret;
	}

	ret = disk_access_ioctl(sd_dev, DISK_IOCTL_GET_SECTOR_SIZE, &sector_size);
	if (ret) {
		printk("Unable to get sector size: %d\n", ret);
		return ret;
	}

	sd_card_size_bytes = (uint64_t)sector_count * sector_size;

	mnt_pt.mnt_point = sd_root_path;

	ret = fs_mount(&mnt_pt);
	if (ret) {
		printk("Mount disk failed, could be format issue (should be FAT/exFAT): %d\n", ret);
		return ret;
	}

	sd_init_success = true;

	return 0;
}

static int sd_card_list_root_files(void)
{
	int ret;
	struct fs_dir_t dirp;
	static struct fs_dirent entry;
	int file_count = 0;

	if (!sd_init_success) {
		printk("SD card not initialized\n");
		return -ENODEV;
	}

	fs_dir_t_init(&dirp);
	ret = fs_opendir(&dirp, sd_root_path);
	if (ret) {
		printk("Failed to open SD card root directory: %d\n", ret);
		return ret;
	}

	printk("\n=== SD Card Root Directory Contents ===\n");

	while (1) {
		ret = fs_readdir(&dirp, &entry);
		if (ret) {
			printk("Failed to read directory: %d\n", ret);
			break;
		}

		if (entry.name[0] == 0) {
			/* End of directory */
			break;
		}

		/* Skip hidden files and directories */
		if (entry.name[0] == '.') {
			continue;
		}

		file_count++;
		if (entry.type == FS_DIR_ENTRY_FILE) {
			printk("[FILE] %s (size: %zu bytes)\n", entry.name, entry.size);
		} else if (entry.type == FS_DIR_ENTRY_DIR) {
			printk("[DIR]  %s/\n", entry.name);
		} else {
			printk("[UNK]  %s\n", entry.name);
		}
	}

	fs_closedir(&dirp);

	if (file_count == 0) {
		printk("No files found in SD card root directory\n");
	} else {
		printk("Total items found: %d\n", file_count);
	}
	printk("========================================\n\n");

	return 0;
}

static int pcm_buf_gen_and_populate(int16_t *pcm_buf, size_t pcm_bytes_req, uint16_t tone_freq_hz,
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

static int open_and_validate_wav_file(const char *wav_filename, size_t sr_idx,
				      struct wav_file_info *wav_info)
{
	int ret;

	/* Check if SD card is properly initialized */
	if (!sd_init_success) {
		printk("SD card not initialized\n");
		return -ENODEV;
	}

	wav_info->current_sample_rate = sample_rates[sr_idx];

	/* Initialize and open WAV file */
	fs_file_t_init(&wav_info->file);

	ret = fs_open(&wav_info->file, wav_filename, FS_O_READ);
	if (ret < 0) {
		printk("Failed to open WAV file %s: %d\n", wav_filename, ret);
		return ret;
	}

	/* Read WAV header */
	ssize_t bytes_read = fs_read(&wav_info->file, &wav_info->header, sizeof(wav_info->header));

	if (bytes_read != sizeof(wav_info->header)) {
		printk("Failed to read WAV header from %s: %d\n", wav_filename, (int)bytes_read);
		fs_close(&wav_info->file);
		return -EIO;
	}

	/* Validate WAV header */
	if (memcmp(wav_info->header.riff_header, "RIFF", 4) != 0 ||
	    memcmp(wav_info->header.wav_head, "WAVE", 4) != 0 ||
	    memcmp(wav_info->header.fmt_header, "fmt ", 4) != 0 ||
	    memcmp(wav_info->header.data_header, "data", 4) != 0) {
		printk("Invalid WAV file format in %s\n", wav_filename);
		fs_close(&wav_info->file);
		return -EINVAL;
	}

	/* Validate audio format */
	if (wav_info->header.audio_format != WAV_FORMAT_PCM || wav_info->header.num_channels != 1 ||
	    wav_info->header.bit_depth != 16) {
		printk("Unsupported WAV format in %s (format:%d, ch:%d, bits:%d)\n", wav_filename,
		       wav_info->header.audio_format, wav_info->header.num_channels,
		       wav_info->header.bit_depth);
		fs_close(&wav_info->file);
		return -ENOTSUP;
	}

	/* Calculate frame information using actual WAV file sample rate */
	wav_info->current_sample_rate = wav_info->header.sample_rate;
	wav_info->samples_per_frame = (wav_info->current_sample_rate * FRAME_DURATION_US) / 1000000;
	wav_info->bytes_per_frame = wav_info->samples_per_frame * sizeof(int16_t);
	wav_info->total_samples = wav_info->header.data_bytes / sizeof(int16_t);
	wav_info->total_frames = wav_info->total_samples / wav_info->samples_per_frame;

	if (wav_info->total_frames == 0) {
		printk("WAV file %s too short for even one frame\n", wav_filename);
		fs_close(&wav_info->file);
		return -EINVAL;
	}

	return 0;
}

static int t2_software_lc3_sine_wave_run(struct lc3_results *results)
{
	int ret;
	timing_t start_time, end_time;
	uint64_t total_cycles;
	uint64_t total_ns;
	uint16_t pcm_bytes_req;

	results->enc_name = "T2";

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

static int liblc3_sine_wave_run(struct lc3_results *results)
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

static int t2_software_lc3_wav_file_run_single(struct wav_file_info *const wav_info)
{
	int ret;
	uint16_t pcm_bytes_req;
	int16_t pcm_data_current[960]; /* Max samples for 48kHz @ 10ms */

	/* Initialize LC3 encoder for this sample rate */
	ret = sw_codec_lc3_enc_init(wav_info->current_sample_rate, PCM_BIT_DEPTH, FRAME_DURATION_US,
				    BITRATE, NUM_CHANNELS, &pcm_bytes_req);
	if (ret) {
		printk("Failed to initialize LC3 encoder for %d Hz: %d\n",
		       wav_info->current_sample_rate, ret);
		return ret;
	}

	/* Process entire file */
	uint64_t cycles;
	timing_t start_time, end_time;
	uint32_t encode_time_us;
	uint64_t total_encode_time_us = 0;
	uint32_t successful_frames = 0;
	uint16_t max_time_encoding_us = 0;
	uint16_t min_time_encoding_us = UINT16_MAX;

	/* Reset file position to start of audio data */
	fs_seek(&wav_info->file, sizeof(struct wav_header), FS_SEEK_SET);

	for (uint32_t frame = 0; frame < wav_info->total_frames; frame++) {
		/* Read one frame of audio data */
		ret = fs_read(&wav_info->file, pcm_data_current, wav_info->bytes_per_frame);
		if (ret != wav_info->bytes_per_frame) {
			if (ret > 0) {
				printk("Partial frame read: %d/%d bytes\n", ret,
				       wav_info->bytes_per_frame);
			}
			break; /* End of file or read error */
		}

		/* Encode this frame */
		start_time = timing_counter_get();
		ret = sw_codec_lc3_enc_run(pcm_data_current, wav_info->bytes_per_frame,
					   LC3_USE_BITRATE_FROM_INIT, AUDIO_CH,
					   sizeof(encoded_data), encoded_data, &encoded_size);
		end_time = timing_counter_get();

		if (ret || encoded_size == 0) {
			printk("Encode failed at frame %d: %d\n", frame, ret);
			break;
		}

		cycles = timing_cycles_get(&start_time, &end_time);
		encode_time_us = timing_cycles_to_ns(cycles) / 1000;
		total_encode_time_us += encode_time_us;
		successful_frames++;

		min_time_encoding_us = MIN(min_time_encoding_us, encode_time_us);
		max_time_encoding_us = MAX(max_time_encoding_us, encode_time_us);
	}

	if (successful_frames > 0) {
		uint16_t avg_encode_time_us = total_encode_time_us / successful_frames;
		printk("\033[33m  T2 %d Hz: %d frames\n\tavg %d us/frame, min %d us, max %d "
		       "us\033[0m\n",
		       wav_info->current_sample_rate, successful_frames, avg_encode_time_us,
		       min_time_encoding_us, max_time_encoding_us);
	} else {
		printk("No successful frames for T2 %d Hz\n", wav_info->current_sample_rate);
		ret = -EIO;
	}

	/* Cleanup */
	sw_codec_lc3_enc_uninit_all();
	return ret;
}

static int liblc3_wav_file_run_single(struct wav_file_info *const wav_info)
{
	int ret;
	lc3_encoder_mem_48k_t encoder_mem;
	int16_t pcm_data_current[960]; /* Max samples for 48kHz @ 10ms */

	lc3_encoder_t encoder = lc3_setup_encoder(FRAME_DURATION_US, wav_info->current_sample_rate,
						  0, &encoder_mem);
	if (!encoder) {
		printk("Failed to setup liblc3 encoder for %d Hz\n", wav_info->current_sample_rate);
		return -ENOMEM;
	}

	uint64_t cycles;
	timing_t start_time, end_time;
	uint32_t encode_time_us;
	uint32_t total_encode_time_us = 0;
	uint32_t successful_frames = 0;
	uint16_t max_time_encoding_us = 0;
	uint16_t min_time_encoding_us = UINT16_MAX;

	/* Calculate frame size for current sample rate and bitrate */
	int frame_bytes = lc3_frame_bytes(FRAME_DURATION_US, BITRATE);

	if (frame_bytes < 0) {
		printk("Failed to get frame bytes for liblc3\n");
		return -EIO;
	}

	/* Reset file position to start of audio data */
	fs_seek(&wav_info->file, sizeof(struct wav_header), FS_SEEK_SET);

	for (uint32_t frame = 0; frame < wav_info->total_frames; frame++) {
		ssize_t bytes_read =
			fs_read(&wav_info->file, pcm_data_current, wav_info->bytes_per_frame);
		if (bytes_read != wav_info->bytes_per_frame) {
			if (bytes_read > 0) {
				printk("Partial frame read at frame %d: %d/%d bytes\n", frame,
				       (int)bytes_read, wav_info->bytes_per_frame);
			}
			break;
		}

		start_time = timing_counter_get();
		ret = lc3_encode(encoder, LC3_PCM_FORMAT_S16, pcm_data_current, 1, frame_bytes,
				 encoded_data);
		end_time = timing_counter_get();

		if (ret) {
			printk("Encoding failed at frame %d: %d\n", frame, ret);
			break;
		}

		cycles = timing_cycles_get(&start_time, &end_time);
		encode_time_us = timing_cycles_to_ns(cycles) / 1000;
		total_encode_time_us += encode_time_us;
		successful_frames++;

		max_time_encoding_us = MAX(max_time_encoding_us, encode_time_us);
		min_time_encoding_us = MIN(min_time_encoding_us, encode_time_us);
	}

	if (successful_frames > 0) {
		uint16_t avg_encode_time_us = total_encode_time_us / successful_frames;
		printk("\033[32m  liblc3 %d Hz: %d frames\n\tavg %d us/frame, min %d us, max %d "
		       "us\033[0m\n",
		       wav_info->current_sample_rate, successful_frames, avg_encode_time_us,
		       min_time_encoding_us, max_time_encoding_us);
		ret = 0;
	} else {
		printk("No successful frames for liblc3 %d Hz\n", wav_info->current_sample_rate);
		ret = -EIO;
	}

	return ret;
}

static void empty_cell_print(size_t cell_size)
{
	printk("|");
	for (size_t i = 0; i < cell_size; i++) {
		printk(" ");
	}
}

static void divider_line_print(size_t num_cells, size_t cell_size)
{
	for (size_t i = 0; i < num_cells; i++) {
		printk("|");
		for (size_t j = 0; j < cell_size; j++) {
			printk("-");
		}
	}
	printk("|\n");
}

static void divider_line_no_bar_print(size_t cell_size)
{
	for (size_t j = 0; j < cell_size; j++) {
		printk("-");
	}
	printk("\n");
}

static void results_print(struct lc3_results *t2_results, struct lc3_results *liblc3_results)
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

			printk("|%4d\033[1m(%2d%%)\033[0m", liblc3_time - t2_time,
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

	timing_init();
	timing_start();

	/* Initialize LC3 codec */
	ret = sw_codec_lc3_init(NULL, NULL, FRAME_DURATION_US);
	if (ret) {
		printk("Failed to initialize LC3 codec: %d\n", ret);
		return ret;
	}

	printk("Benchmarking LC3 encoding for different tone frequencies...\n");
	printk("Comparing \033[33mT2 software LC3\033[0m implementation against "
	       "\033[32mliblc3\033[0m\n");

	printk("Frame duration: %d us\n", FRAME_DURATION_US);

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

	ret = t2_software_lc3_sine_wave_run(&t2_results);
	if (ret) {
		printk("T2 software LC3 benchmark failed: %d\n", ret);
		return ret;
	}

	ret = liblc3_sine_wave_run(&liblc3_results);
	if (ret) {
		printk("liblc3 benchmark failed: %d\n", ret);
		return ret;
	}

	timing_stop();

	/* Print results */
	results_print(&t2_results, &liblc3_results);

	/* Initialize and mount SD card filesystem */
	ret = sd_card_init();
	if (ret) {
		printk("Failed to initialize SD card, skipping WAV file tests: %d\n", ret);
		return 0;
	}

	if (!sd_init_success) {
		printk("SD card not properly initialized, skipping WAV file tests\n");
		return 0;
	}

	/* List files in SD card root directory for verification */
	// sd_card_list_root_files();

	/* Run WAV file comparison if filesystem support is available */
	printk("\n\n=== WAV File Benchmark ===\n");
	printk("Testing with real audio data from WAV files...\n");

	timing_start();

	/* Test each sample rate with its corresponding WAV file */
	for (size_t sr_idx = 0; sr_idx < ARRAY_SIZE(sample_rates); sr_idx++) {
		char wav_file_path[64];
		struct wav_file_info wav_info;
		uint32_t sample_rate = sample_rates[sr_idx];

		if (sample_rate == 16000) {
			strncpy(wav_file_path, TEST_FILE_16KHZ, sizeof(wav_file_path));
		} else if (sample_rate == 24000) {
			strncpy(wav_file_path, TEST_FILE_24KHZ, sizeof(wav_file_path));
		} else if (sample_rate == 48000) {
			strncpy(wav_file_path, TEST_FILE_48KHZ, sizeof(wav_file_path));
		}

		printk("\n--- Testing %d kHz ---\n", sample_rate / 1000);

		/* Open and validate WAV file using shared function */
		ret = open_and_validate_wav_file(wav_file_path, sr_idx, &wav_info);
		if (ret != 0) {
			return ret;
		}

		/* Test T2 Software LC3 with this sample rate's WAV file */
		ret = t2_software_lc3_wav_file_run_single(&wav_info);
		if (ret == 0) {
			/* Test liblc3 with the same file */
			ret = liblc3_wav_file_run_single(&wav_info);

			if (ret != 0) {
				printk("liblc3 WAV benchmark failed for %d kHz: %d\n",
				       sample_rate / 1000, ret);
			}
		} else {
			printk("Failed to run T2 WAV benchmark for %d kHz: %d\n",
			       sample_rate / 1000, ret);
		}

		fs_close(&wav_info.file);
	}

	timing_stop();

	printk("\n=== Benchmark Completed ===\n");

	return 0;
}
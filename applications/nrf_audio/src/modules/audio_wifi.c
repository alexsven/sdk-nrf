/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "audio_wifi.h"

#include <errno.h>
#include <string.h>

#include <audio_defines.h>
#include <zephyr/bluetooth/audio/audio.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/socket.h>
#include <zephyr/net_buf.h>
#include <zephyr/sys/byteorder.h>

LOG_MODULE_REGISTER(audio_wifi, CONFIG_LOG_DEFAULT_LEVEL);

#define AUDIO_PORT	   5004
#define PACKET_SAMPLES	   240
#define FRAME_SAMPLES	   480
#define SAMPLE_BYTES	   4
#define PACKET_BYTES	   (12 + PACKET_SAMPLES * SAMPLE_BYTES)
#define RING_SAMPLES	   4800
#define PREFILL_SAMPLES	   1440
#define CORRECTION_MARGIN  240
#define MAX_GAP_PACKETS	   4
#define SESSION_TIMEOUT_MS 2000

BUILD_ASSERT(CONFIG_AUDIO_SAMPLE_RATE_HZ == 48000);
BUILD_ASSERT(CONFIG_AUDIO_BIT_DEPTH_BITS == 16);
BUILD_ASSERT(CONFIG_AUDIO_INPUT_CHANNELS == 2);
BUILD_ASSERT(CONFIG_AUDIO_FRAME_DURATION_US == 10000);
BUILD_ASSERT(IS_ENABLED(CONFIG_BT_BAP_BROADCAST_SOURCE));
BUILD_ASSERT(!IS_ENABLED(CONFIG_STREAM_BIDIRECTIONAL));

NET_BUF_POOL_FIXED_DEFINE(wifi_frame_pool, 4, FRAME_SAMPLES *SAMPLE_BYTES,
			  sizeof(struct audio_metadata), NULL);
K_MUTEX_DEFINE(audio_lock);
K_SEM_DEFINE(playback_start, 0, 1);

static int16_t ring[RING_SAMPLES][2];
static size_t read_pos;
static size_t sample_count;
static bool playing;
static bool primed;
static bool session_valid;
static uint32_t session;
static uint32_t next_sequence;
static int64_t last_packet_ms;
static struct k_msgq *output_queue;

static void reset_buffer(void)
{
	read_pos = 0;
	sample_count = 0;
	primed = false;
}

static void append_samples(const uint8_t *pcm)
{
	for (size_t i = 0; i < PACKET_SAMPLES; i++) {
		size_t pos = (read_pos + sample_count) % RING_SAMPLES;

		for (size_t ch = 0; ch < 2; ch++) {
			ring[pos][ch] = pcm ? (int16_t)sys_get_le16(pcm + i * 4 + ch * 2) : 0;
		}
		sample_count++;
	}
}

static void packet_receive(const uint8_t *packet, size_t len)
{
	if (len != PACKET_BYTES || memcmp(packet, "NAW1", 4) != 0) {
		LOG_WRN_RATELIMIT("Invalid Wi-Fi audio packet");
		return;
	}

	uint32_t new_session = sys_get_be32(packet + 4);
	uint32_t sequence = sys_get_be32(packet + 8);
	int64_t now = k_uptime_get();

	k_mutex_lock(&audio_lock, K_FOREVER);
	if (!playing) {
		goto out;
	}

	if (!session_valid || now - last_packet_ms > SESSION_TIMEOUT_MS) {
		reset_buffer();
		session = new_session;
		next_sequence = sequence;
		session_valid = true;
		LOG_INF("Wi-Fi audio session started");
	} else if (session != new_session) {
		LOG_WRN_RATELIMIT("Ignoring another Wi-Fi audio session");
		goto out;
	}

	int32_t gap = (int32_t)(sequence - next_sequence);

	if (gap < 0) {
		LOG_WRN_RATELIMIT("Late or duplicate Wi-Fi audio packet");
		goto out;
	}
	if (gap > MAX_GAP_PACKETS) {
		LOG_WRN("Wi-Fi audio sequence gap %d; rebuffering", gap);
		reset_buffer();
		gap = 0;
	}
	if (sample_count + (gap + 1) * PACKET_SAMPLES > RING_SAMPLES) {
		LOG_WRN("Wi-Fi audio buffer overrun; rebuffering");
		reset_buffer();
		gap = 0;
	}
	if (gap) {
		LOG_WRN_RATELIMIT("Wi-Fi audio lost %d packets", gap);
	}
	for (int32_t i = 0; i < gap; i++) {
		append_samples(NULL);
	}
	append_samples(packet + 12);
	next_sequence = sequence + 1;
	last_packet_ms = now;
out:
	k_mutex_unlock(&audio_lock);
}

static void receive_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);
	uint8_t packet[PACKET_BYTES + 1];
	struct sockaddr_in address = {
		.sin_family = AF_INET,
		.sin_port = htons(AUDIO_PORT),
		.sin_addr = {.s_addr = INADDR_ANY},
	};

	while (true) {
		int sock = zsock_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

		if (sock < 0) {
			LOG_ERR("Cannot create Wi-Fi audio socket: %d", errno);
			k_sleep(K_SECONDS(2));
			continue;
		}
		if (zsock_bind(sock, (struct sockaddr *)&address, sizeof(address)) < 0) {
			LOG_ERR("Cannot bind Wi-Fi audio socket: %d", errno);
			zsock_close(sock);
			k_sleep(K_SECONDS(2));
			continue;
		}
		LOG_INF("Wi-Fi audio listening on UDP port %d", AUDIO_PORT);
		while (true) {
			ssize_t len = zsock_recv(sock, packet, sizeof(packet), 0);

			if (len < 0) {
				LOG_ERR("Wi-Fi audio receive failed: %d", errno);
				break;
			}
			packet_receive(packet, len);
		}
		zsock_close(sock);
		k_sleep(K_SECONDS(2));
	}
}

static void frame_submit(void)
{
	struct net_buf *frame = net_buf_alloc(&wifi_frame_pool, K_NO_WAIT);

	if (!frame) {
		LOG_WRN_RATELIMIT("Wi-Fi audio frame pool exhausted");
		return;
	}
	uint8_t *data = net_buf_add(frame, FRAME_SAMPLES * SAMPLE_BYTES);
	struct audio_metadata *meta = net_buf_user_data(frame);

	*meta = (struct audio_metadata){
		.data_coding = PCM,
		.data_len_us = 10000,
		.sample_rate_hz = 48000,
		.bits_per_sample = 16,
		.carried_bits_per_sample = 16,
		.bytes_per_location = FRAME_SAMPLES * sizeof(int16_t),
		.interleaved = true,
		.locations = BT_AUDIO_LOCATION_FRONT_LEFT | BT_AUDIO_LOCATION_FRONT_RIGHT,
	};

	k_mutex_lock(&audio_lock, K_FOREVER);
	if (!playing) {
		k_mutex_unlock(&audio_lock);
		net_buf_unref(frame);
		return;
	}
	if (!primed && sample_count >= PREFILL_SAMPLES) {
		primed = true;
	}

	if (!primed || sample_count < FRAME_SAMPLES + 1) {
		memset(data, 0, FRAME_SAMPLES * SAMPLE_BYTES);
		if (primed) {
			LOG_WRN_RATELIMIT("Wi-Fi audio buffer underrun; rebuffering");
			reset_buffer();
		}
	} else {
		size_t consume = FRAME_SAMPLES;

		if (sample_count > PREFILL_SAMPLES + CORRECTION_MARGIN) {
			consume++;
		} else if (sample_count < PREFILL_SAMPLES - CORRECTION_MARGIN) {
			consume--;
		}
		/* Spread one sample of clock correction across the entire frame. */
		for (size_t i = 0; i < FRAME_SAMPLES; i++) {
			size_t position = i * (consume - 1);
			size_t index = position / (FRAME_SAMPLES - 1);
			size_t fraction = position % (FRAME_SAMPLES - 1);
			size_t first = (read_pos + index) % RING_SAMPLES;
			size_t second = (read_pos + MIN(index + 1, consume - 1)) % RING_SAMPLES;

			for (size_t ch = 0; ch < 2; ch++) {
				int32_t value = ring[first][ch] * ((int32_t)FRAME_SAMPLES - 1 -
								   (int32_t)fraction) +
						ring[second][ch] * (int32_t)fraction;

				sys_put_le16((uint16_t)(int16_t)(value / (FRAME_SAMPLES - 1)),
					     data + i * 4 + ch * 2);
			}
		}
		read_pos = (read_pos + consume) % RING_SAMPLES;
		sample_count -= consume;
	}
	int ret = k_msgq_put(output_queue, &frame, K_NO_WAIT);

	k_mutex_unlock(&audio_lock);
	if (ret) {
		LOG_WRN_RATELIMIT("Wi-Fi audio encoder queue full");
		net_buf_unref(frame);
	}
}

static void playback_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	while (true) {
		k_sem_take(&playback_start, K_FOREVER);
		int64_t next = k_uptime_get();

		while (true) {
			k_mutex_lock(&audio_lock, K_FOREVER);
			bool active = playing;
			k_mutex_unlock(&audio_lock);
			if (!active) {
				break;
			}
			frame_submit();
			next += 10;
			if (k_uptime_get() >= next) {
				LOG_WRN_RATELIMIT("Wi-Fi audio playback deadline missed");
				next = k_uptime_get() + 10;
			}
			k_sleep(K_TIMEOUT_ABS_MS(next));
		}
	}
}

K_THREAD_DEFINE(wifi_receive_thread_id, 4096, receive_thread, NULL, NULL, NULL, K_PRIO_PREEMPT(8),
		0, -1);
K_THREAD_DEFINE(wifi_playback_thread_id, 3072, playback_thread, NULL, NULL, NULL, K_PRIO_PREEMPT(5),
		0, -1);

int audio_wifi_init(void)
{
	k_thread_start(wifi_receive_thread_id);
	k_thread_start(wifi_playback_thread_id);
	return 0;
}

int audio_wifi_start(struct k_msgq *queue)
{
	if (!queue) {
		return -EINVAL;
	}
	k_mutex_lock(&audio_lock, K_FOREVER);
	output_queue = queue;
	reset_buffer();
	session_valid = false;
	playing = true;
	k_mutex_unlock(&audio_lock);
	k_sem_give(&playback_start);
	return 0;
}

void audio_wifi_stop(void)
{
	k_mutex_lock(&audio_lock, K_FOREVER);
	playing = false;
	reset_buffer();
	session_valid = false;
	struct net_buf *frame;

	while (output_queue && k_msgq_get(output_queue, &frame, K_NO_WAIT) == 0) {
		net_buf_unref(frame);
	}
	k_mutex_unlock(&audio_lock);
}

/*
 * Copyright (c) 2022-2023 Nordic Semiconductor ASA
 * Copyright (c) 2024 Demant A/S
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <ctype.h>
#include <strings.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/audio/audio.h>
#include <zephyr/bluetooth/audio/bap.h>
#include <zephyr/bluetooth/audio/pacs.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/bluetooth/audio/pbp.h>

BUILD_ASSERT(IS_ENABLED(CONFIG_SCAN_SELF) || IS_ENABLED(CONFIG_SCAN_OFFLOAD),
	     "Either SCAN_SELF or SCAN_OFFLOAD must be enabled");

#define SEM_TIMEOUT		    K_SECONDS(60)
#define BROADCAST_ASSISTANT_TIMEOUT K_SECONDS(120) /* 2 minutes */

#define LOG_INTERVAL 1000U

#if defined(CONFIG_SCAN_SELF)
#define ADV_TIMEOUT K_SECONDS(CONFIG_SCAN_DELAY)
#else /* !CONFIG_SCAN_SELF */
#define ADV_TIMEOUT K_FOREVER
#endif /* CONFIG_SCAN_SELF */

#define INVALID_BROADCAST_ID		  (BT_AUDIO_BROADCAST_ID_MAX + 1)
#define PA_SYNC_INTERVAL_TO_TIMEOUT_RATIO 20 /* Set the timeout relative to interval */
#define PA_SYNC_SKIP			  5
#define NAME_LEN			  sizeof(CONFIG_TARGET_BROADCAST_NAME) + 1
#define BROADCAST_DATA_ELEMENT_SIZE	  sizeof(int16_t)

static K_SEM_DEFINE(sem_broadcast_sink_stopped, 0U, 1U);
static K_SEM_DEFINE(sem_connected, 0U, 1U);
static K_SEM_DEFINE(sem_disconnected, 0U, 1U);
static K_SEM_DEFINE(sem_broadcaster_found, 0U, 1U);
static K_SEM_DEFINE(sem_pa_synced, 0U, 1U);
static K_SEM_DEFINE(sem_base_received, 0U, 1U);
static K_SEM_DEFINE(sem_syncable, 0U, 1U);
static K_SEM_DEFINE(sem_pa_sync_lost, 0U, 1U);
static K_SEM_DEFINE(sem_broadcast_code_received, 0U, 1U);
static K_SEM_DEFINE(sem_pa_request, 0U, 1U);
static K_SEM_DEFINE(sem_past_request, 0U, 1U);
static K_SEM_DEFINE(sem_bis_sync_requested, 0U, 1U);
static K_SEM_DEFINE(sem_stream_connected, 0U, CONFIG_BT_BAP_BROADCAST_SNK_STREAM_COUNT);
static K_SEM_DEFINE(sem_stream_started, 0U, CONFIG_BT_BAP_BROADCAST_SNK_STREAM_COUNT);
static K_SEM_DEFINE(sem_big_synced, 0U, 1U);

/* Sample assumes that we only have a single Scan Delegator receive state */
static const struct bt_bap_scan_delegator_recv_state *req_recv_state;
static struct bt_bap_broadcast_sink *broadcast_sink;
static struct bt_le_scan_recv_info broadcaster_info;
static bt_addr_le_t broadcaster_addr;
static struct bt_le_per_adv_sync *pa_sync;
static uint32_t broadcaster_broadcast_id;
static struct broadcast_sink_stream {
	struct bt_bap_stream stream;
	size_t recv_cnt;
	size_t loss_cnt;
	size_t error_cnt;
	size_t valid_cnt;
} streams[CONFIG_BT_BAP_BROADCAST_SNK_STREAM_COUNT];

static struct bt_bap_stream *streams_p[ARRAY_SIZE(streams)];
static volatile bool big_synced;
static volatile bool base_received;
static struct bt_conn *broadcast_assistant_conn;
static volatile uint8_t stream_count;

static const struct bt_audio_codec_cap codec_cap = BT_AUDIO_CODEC_CAP_LC3(
	BT_AUDIO_CODEC_CAP_FREQ_16KHZ | BT_AUDIO_CODEC_CAP_FREQ_24KHZ BT_AUDIO_CODEC_CAP_FREQ_48KHZ,
	BT_AUDIO_CODEC_CAP_DURATION_10, BT_AUDIO_CODEC_CAP_CHAN_COUNT_SUPPORT(1), 40u, 120u,
	CONFIG_MAX_CODEC_FRAMES_PER_SDU, BT_AUDIO_CONTEXT_TYPE_ANY);

/* Create a mask for the maximum BIS we can sync to using the number of streams
 * we have. We add an additional 1 since the bis indexes start from 1 and not
 * 0.
 */
static const uint32_t bis_index_mask = BIT_MASK(ARRAY_SIZE(streams) + 1U);
static uint32_t requested_bis_sync;
static uint32_t bis_index_bitfield;
static uint8_t sink_broadcast_code[BT_AUDIO_BROADCAST_CODE_SIZE];

uint64_t total_rx_iso_packet_count; /* This value is exposed to test code */

static void stream_connected_cb(struct bt_bap_stream *stream)
{
	printk("Stream %p connected\n", stream);

	k_sem_give(&sem_stream_connected);
}

static void stream_disconnected_cb(struct bt_bap_stream *stream, uint8_t reason)
{
	int err;

	printk("Stream %p disconnected with reason 0x%02X\n", stream, reason);

	err = k_sem_take(&sem_stream_connected, K_NO_WAIT);
	if (err != 0) {
		printk("Failed to take sem_stream_connected: %d\n", err);
	}
}

static void stream_started_cb(struct bt_bap_stream *stream)
{
	struct broadcast_sink_stream *sink_stream =
		CONTAINER_OF(stream, struct broadcast_sink_stream, stream);

	printk("Stream %p started\n", stream);

	total_rx_iso_packet_count = 0U;
	sink_stream->recv_cnt = 0U;
	sink_stream->loss_cnt = 0U;
	sink_stream->valid_cnt = 0U;
	sink_stream->error_cnt = 0U;

	k_sem_give(&sem_stream_started);
	if (k_sem_count_get(&sem_stream_started) == stream_count) {
		big_synced = true;
		k_sem_give(&sem_big_synced);
	}
}

static void stream_stopped_cb(struct bt_bap_stream *stream, uint8_t reason)
{
	int err;

	printk("Stream %p stopped with reason 0x%02X\n", stream, reason);

	err = k_sem_take(&sem_stream_started, K_NO_WAIT);
	if (err != 0) {
		printk("Failed to take sem_stream_started: %d\n", err);
	}

	if (k_sem_count_get(&sem_stream_started) != stream_count) {
		big_synced = false;
	}
}

static void stream_recv_cb(struct bt_bap_stream *stream, const struct bt_iso_recv_info *info,
			   struct net_buf *buf)
{
	struct broadcast_sink_stream *sink_stream =
		CONTAINER_OF(stream, struct broadcast_sink_stream, stream);

	if (info->flags & BT_ISO_FLAGS_ERROR) {
		sink_stream->error_cnt++;
	}

	if (info->flags & BT_ISO_FLAGS_LOST) {
		sink_stream->loss_cnt++;
	}

	if (info->flags & BT_ISO_FLAGS_VALID) {
		sink_stream->valid_cnt++;
	}

	total_rx_iso_packet_count++;
	sink_stream->recv_cnt++;
	if ((sink_stream->recv_cnt % LOG_INTERVAL) == 0U) {
		printk("Stream %p: received %u total ISO packets: Valid %u | Error %u | Loss %u\n",
		       &sink_stream->stream, sink_stream->recv_cnt, sink_stream->valid_cnt,
		       sink_stream->error_cnt, sink_stream->loss_cnt);
	}
}

static struct bt_bap_stream_ops stream_ops = {
	.connected = stream_connected_cb,
	.disconnected = stream_disconnected_cb,
	.started = stream_started_cb,
	.stopped = stream_stopped_cb,
	.recv = stream_recv_cb,
};

static void base_recv_cb(struct bt_bap_broadcast_sink *sink, const struct bt_bap_base *base,
			 size_t base_size)
{
	uint32_t base_bis_index_bitfield = 0U;
	int err;

	if (base_received) {
		return;
	}

	printk("Received BASE with %d subgroups from broadcast sink %p\n",
	       bt_bap_base_get_subgroup_count(base), sink);

	err = bt_bap_base_get_bis_indexes(base, &base_bis_index_bitfield);
	if (err != 0) {
		printk("Failed to BIS indexes: %d\n", err);
		return;
	}

	bis_index_bitfield = base_bis_index_bitfield & bis_index_mask;

	printk("bis_index_bitfield = 0x%08x\n", bis_index_bitfield);

	if (broadcast_assistant_conn == NULL) {
		/* No broadcast assistant requesting anything */
		requested_bis_sync = BT_BAP_BIS_SYNC_NO_PREF;
		k_sem_give(&sem_bis_sync_requested);
	}

	base_received = true;
	k_sem_give(&sem_base_received);
}

static void syncable_cb(struct bt_bap_broadcast_sink *sink, const struct bt_iso_biginfo *biginfo)
{
	printk("Broadcast sink (%p) is syncable, BIG %s\n", (void *)sink,
	       biginfo->encryption ? "encrypted" : "not encrypted");

	k_sem_give(&sem_syncable);

	if (!biginfo->encryption) {
		/* Use the semaphore as a boolean */
		k_sem_reset(&sem_broadcast_code_received);
		k_sem_give(&sem_broadcast_code_received);
	}
}

static struct bt_bap_broadcast_sink_cb broadcast_sink_cbs = {
	.base_recv = base_recv_cb,
	.syncable = syncable_cb,
};

static void pa_timer_handler(struct k_work *work)
{
	if (req_recv_state != NULL) {
		enum bt_bap_pa_state pa_state;

		if (req_recv_state->pa_sync_state == BT_BAP_PA_STATE_INFO_REQ) {
			pa_state = BT_BAP_PA_STATE_NO_PAST;
		} else {
			pa_state = BT_BAP_PA_STATE_FAILED;
		}

		bt_bap_scan_delegator_set_pa_state(req_recv_state->src_id, pa_state);
	}

	printk("PA timeout\n");
}

static K_WORK_DELAYABLE_DEFINE(pa_timer, pa_timer_handler);

static uint16_t interval_to_sync_timeout(uint16_t pa_interval)
{
	uint16_t pa_timeout;

	if (pa_interval == BT_BAP_PA_INTERVAL_UNKNOWN) {
		/* Use maximum value to maximize chance of success */
		pa_timeout = BT_GAP_PER_ADV_MAX_TIMEOUT;
	} else {
		uint32_t interval_ms;
		uint32_t timeout;

		/* Add retries and convert to unit in 10's of ms */
		interval_ms = BT_GAP_PER_ADV_INTERVAL_TO_MS(pa_interval);
		timeout = (interval_ms * PA_SYNC_INTERVAL_TO_TIMEOUT_RATIO) / 10;

		/* Enforce restraints */
		pa_timeout = CLAMP(timeout, BT_GAP_PER_ADV_MIN_TIMEOUT, BT_GAP_PER_ADV_MAX_TIMEOUT);
	}

	return pa_timeout;
}

static int pa_sync_past(struct bt_conn *conn, uint16_t pa_interval)
{
	struct bt_le_per_adv_sync_transfer_param param = {0};
	int err;

	param.skip = PA_SYNC_SKIP;
	param.timeout = interval_to_sync_timeout(pa_interval);

	err = bt_le_per_adv_sync_transfer_subscribe(conn, &param);
	if (err != 0) {
		printk("Could not do PAST subscribe: %d\n", err);
	} else {
		printk("Syncing with PAST\n");
		(void)k_work_reschedule(&pa_timer, K_MSEC(param.timeout * 10));
	}

	return err;
}

static void recv_state_updated_cb(struct bt_conn *conn,
				  const struct bt_bap_scan_delegator_recv_state *recv_state)
{
	printk("Receive state updated, pa sync state: %u\n", recv_state->pa_sync_state);

	for (uint8_t i = 0; i < recv_state->num_subgroups; i++) {
		printk("subgroup %d bis_sync: 0x%08x\n", i, recv_state->subgroups[i].bis_sync);
	}

	req_recv_state = recv_state;
}

static int pa_sync_req_cb(struct bt_conn *conn,
			  const struct bt_bap_scan_delegator_recv_state *recv_state,
			  bool past_avail, uint16_t pa_interval)
{

	printk("Received request to sync to PA (PAST %savailble): %u\n", past_avail ? "" : "not ",
	       recv_state->pa_sync_state);

	req_recv_state = recv_state;

	if (recv_state->pa_sync_state == BT_BAP_PA_STATE_SYNCED ||
	    recv_state->pa_sync_state == BT_BAP_PA_STATE_INFO_REQ) {
		/* Already syncing */
		/* TODO: Terminate existing sync and then sync to new?*/
		return -1;
	}

	if (IS_ENABLED(CONFIG_BT_PER_ADV_SYNC_TRANSFER_RECEIVER) && past_avail) {
		int err;

		err = pa_sync_past(conn, pa_interval);
		if (err != 0) {
			printk("Failed to subscribe to PAST: %d\n", err);

			return err;
		}

		k_sem_give(&sem_past_request);

		err = bt_bap_scan_delegator_set_pa_state(recv_state->src_id,
							 BT_BAP_PA_STATE_INFO_REQ);
		if (err != 0) {
			printk("Failed to set PA state to BT_BAP_PA_STATE_INFO_REQ: %d\n", err);

			return err;
		}
	}

	k_sem_give(&sem_pa_request);

	return 0;
}

static int pa_sync_term_req_cb(struct bt_conn *conn,
			       const struct bt_bap_scan_delegator_recv_state *recv_state)
{
	int err;

	printk("PA sync termination req, pa sync state: %u\n", recv_state->pa_sync_state);

	for (uint8_t i = 0; i < recv_state->num_subgroups; i++) {
		printk("subgroup %d bis_sync: 0x%08x\n", i, recv_state->subgroups[i].bis_sync);
	}

	req_recv_state = recv_state;

	printk("Delete periodic advertising sync\n");
	err = bt_le_per_adv_sync_delete(pa_sync);
	if (err != 0) {
		printk("Could not delete per adv sync: %d\n", err);

		return err;
	}

	return 0;
}

static void broadcast_code_cb(struct bt_conn *conn,
			      const struct bt_bap_scan_delegator_recv_state *recv_state,
			      const uint8_t broadcast_code[BT_AUDIO_BROADCAST_CODE_SIZE])
{
	printk("Broadcast code received for %p\n", recv_state);

	req_recv_state = recv_state;

	(void)memcpy(sink_broadcast_code, broadcast_code, BT_AUDIO_BROADCAST_CODE_SIZE);

	/* Use the semaphore as a boolean */
	k_sem_reset(&sem_broadcast_code_received);
	k_sem_give(&sem_broadcast_code_received);
}

static int bis_sync_req_cb(struct bt_conn *conn,
			   const struct bt_bap_scan_delegator_recv_state *recv_state,
			   const uint32_t bis_sync_req[CONFIG_BT_BAP_BASS_MAX_SUBGROUPS])
{
	printk("BIS sync request received for %p: 0x%08x->0x%08x, broadcast id: 0x%06x, (%s)\n",
	       recv_state, requested_bis_sync, bis_sync_req[0], recv_state->broadcast_id,
	       big_synced ? "BIG synced" : "BIG not synced");

	/* We only care about a single subgroup in this sample */
	if (big_synced && requested_bis_sync != bis_sync_req[0]) {
		/* If the BIS sync request is received while we are already
		 * synced, it means that the requested BIS sync has changed.
		 */
		int err;

		/* The stream stopped callback will be called as part of this,
		 * and we do not need to wait for any events from the
		 * controller. Thus, when this returns, the `big_synced`
		 * is back to false.
		 */
		err = bt_bap_broadcast_sink_stop(broadcast_sink);
		if (err != 0) {
			printk("Failed to stop Broadcast Sink: %d\n", err);

			return err;
		}

		k_sem_give(&sem_broadcast_sink_stopped);
	}

	requested_bis_sync = bis_sync_req[0];
	broadcaster_broadcast_id = recv_state->broadcast_id;
	if (bis_sync_req[0] != 0) {
		k_sem_give(&sem_bis_sync_requested);
	}

	return 0;
}

static struct bt_bap_scan_delegator_cb scan_delegator_cbs = {
	.recv_state_updated = recv_state_updated_cb,
	.pa_sync_req = pa_sync_req_cb,
	.pa_sync_term_req = pa_sync_term_req_cb,
	.broadcast_code = broadcast_code_cb,
	.bis_sync_req = bis_sync_req_cb,
};

static struct bt_pacs_cap cap = {
	.codec_cap = &codec_cap,
};

static bool scan_check_and_sync_broadcast(struct bt_data *data, void *user_data)
{
	enum bt_pbp_announcement_feature source_features;
	const struct bt_le_scan_recv_info *info = user_data;
	struct bt_uuid_16 adv_uuid;
	uint8_t *tmp_meta = NULL;
	int ret;

	if (data->type != BT_DATA_SVC_DATA16) {
		return true;
	}

	if (!bt_uuid_create(&adv_uuid.uuid, data->data, BT_UUID_SIZE_16)) {
		return true;
	}

	if (!bt_uuid_cmp(&adv_uuid.uuid, BT_UUID_BROADCAST_AUDIO)) {
		broadcaster_broadcast_id = sys_get_le24(data->data + BT_UUID_SIZE_16);
		return true;
	}

	ret = bt_pbp_parse_announcement(data, &source_features, &tmp_meta);
	if (ret >= 0) {
		printk("PBA:\n");
		if (source_features & BT_PBP_ANNOUNCEMENT_FEATURE_ENCRYPTION) {
			printk("\tEncrypted\n");
		}

		if (source_features & BT_PBP_ANNOUNCEMENT_FEATURE_STANDARD_QUALITY) {
			printk("\tStandard quality\n");
		}

		if (source_features & BT_PBP_ANNOUNCEMENT_FEATURE_HIGH_QUALITY) {
			printk("\tHigh quality\n");
		}

		/* parse metadata */
		if (tmp_meta == NULL) {
			printk("\tNo metadata\n");
			return true;
		}

		size_t i = 0;

		while (i < ret) {
			uint8_t len = tmp_meta[i++];
			uint8_t type = tmp_meta[i];

			printk("\tLen: %d", len);
			printk("\tType: %d", type);

			if (len == 2) {
				printk("\tValue: 0x%04x\n", tmp_meta[i + 1]);
			} else {
				printk("\tValue: %d\n", tmp_meta[i + 1]);
			}
			i += len;
		}

		/* Store info for PA sync parameters */
		memcpy(&broadcaster_info, info, sizeof(broadcaster_info));
		bt_addr_le_copy(&broadcaster_addr, info->addr);
		printk("broadcaster_broadcast_id = 0x%06X\n", broadcaster_broadcast_id);
		k_sem_give(&sem_broadcaster_found);

		return false;
	}

	return true;
}

static bool is_substring(const char *substr, const char *str)
{
	const size_t str_len = strlen(str);
	const size_t sub_str_len = strlen(substr);

	if (sub_str_len > str_len) {
		return false;
	}

	for (size_t pos = 0; pos < str_len; pos++) {
		if (pos + sub_str_len > str_len) {
			return false;
		}

		if (strncasecmp(substr, &str[pos], sub_str_len) == 0) {
			return true;
		}
	}

	return false;
}

static bool data_cb(struct bt_data *data, void *user_data)
{
	char *name = user_data;

	switch (data->type) {
	case BT_DATA_NAME_SHORTENED:
	case BT_DATA_NAME_COMPLETE:
	case BT_DATA_BROADCAST_NAME:
		memcpy(name, data->data, MIN(data->data_len, NAME_LEN - 1));
		return false;
	default:
		return true;
	}
}

static void broadcast_scan_recv(const struct bt_le_scan_recv_info *info, struct net_buf_simple *ad)
{
	if (info->interval != 0U) {
		/* call to bt_data_parse consumes netbufs so shallow clone for verbose output */

		/* If req_recv_state is not NULL then we have been requested by a broadcast
		 * assistant to sync to a specific broadcast source. In that case we do not apply
		 * our own broadcast name filter.
		 */
		if (req_recv_state == NULL && strlen(CONFIG_TARGET_BROADCAST_NAME) > 0U) {
			struct net_buf_simple buf_copy;
			char name[NAME_LEN] = {0};

			net_buf_simple_clone(ad, &buf_copy);
			bt_data_parse(&buf_copy, data_cb, name);
			if (!(is_substring(CONFIG_TARGET_BROADCAST_NAME, name))) {
				return;
			}
		}
		bt_data_parse(ad, scan_check_and_sync_broadcast, (void *)info);
	}
}

static struct bt_le_scan_cb bap_scan_cb = {
	.recv = broadcast_scan_recv,
};

static void bap_pa_sync_synced_cb(struct bt_le_per_adv_sync *sync,
				  struct bt_le_per_adv_sync_synced_info *info)
{
	if (sync == pa_sync ||
	    (req_recv_state != NULL && bt_addr_le_eq(info->addr, &req_recv_state->addr) &&
	     info->sid == req_recv_state->adv_sid)) {
		printk("PA sync %p synced for broadcast sink with broadcast ID 0x%06X\n", sync,
		       broadcaster_broadcast_id);

		if (pa_sync == NULL) {
			pa_sync = sync;
		}

		k_work_cancel_delayable(&pa_timer);
		k_sem_give(&sem_pa_synced);
	}
}

static void bap_pa_sync_terminated_cb(struct bt_le_per_adv_sync *sync,
				      const struct bt_le_per_adv_sync_term_info *info)
{
	if (sync == pa_sync) {
		printk("PA sync %p lost with reason %u\n", sync, info->reason);
		pa_sync = NULL;

		k_sem_give(&sem_pa_sync_lost);
	}
}

static struct bt_le_per_adv_sync_cb bap_pa_sync_cb = {
	.synced = bap_pa_sync_synced_cb,
	.term = bap_pa_sync_terminated_cb,
};

static int init(void)
{
	int err;

	err = bt_enable(NULL);
	if (err) {
		printk("Bluetooth enable failed (err %d)\n", err);
		return err;
	}

	printk("Bluetooth initialized\n");

	err = bt_pacs_cap_register(BT_AUDIO_DIR_SINK, &cap);
	if (err) {
		printk("Capability register failed (err %d)\n", err);
		return err;
	}

	bt_bap_broadcast_sink_register_cb(&broadcast_sink_cbs);
	bt_bap_scan_delegator_register_cb(&scan_delegator_cbs);
	bt_le_per_adv_sync_cb_register(&bap_pa_sync_cb);
	bt_le_scan_cb_register(&bap_scan_cb);

	for (size_t i = 0U; i < ARRAY_SIZE(streams); i++) {
		streams[i].stream.ops = &stream_ops;
	}

	return 0;
}

static int reset(void)
{
	int err;

	printk("Reset\n");

	bis_index_bitfield = 0U;
	requested_bis_sync = 0U;
	req_recv_state = NULL;
	big_synced = false;
	base_received = false;
	stream_count = 0U;
	(void)memset(sink_broadcast_code, 0, sizeof(sink_broadcast_code));
	(void)memset(&broadcaster_info, 0, sizeof(broadcaster_info));
	(void)memset(&broadcaster_addr, 0, sizeof(broadcaster_addr));
	broadcaster_broadcast_id = INVALID_BROADCAST_ID;

	if (broadcast_sink != NULL) {
		err = bt_bap_broadcast_sink_delete(broadcast_sink);
		if (err) {
			printk("Deleting broadcast sink failed (err %d)\n", err);

			return err;
		}

		broadcast_sink = NULL;
	}

	if (pa_sync != NULL) {
		bt_le_per_adv_sync_delete(pa_sync);
		if (err) {
			printk("Deleting PA sync failed (err %d)\n", err);

			return err;
		}

		pa_sync = NULL;
	}

	k_sem_reset(&sem_broadcaster_found);
	k_sem_reset(&sem_pa_synced);
	k_sem_reset(&sem_base_received);
	k_sem_reset(&sem_syncable);
	k_sem_reset(&sem_pa_sync_lost);
	k_sem_reset(&sem_broadcast_code_received);
	k_sem_reset(&sem_bis_sync_requested);
	k_sem_reset(&sem_stream_connected);
	k_sem_reset(&sem_stream_started);
	k_sem_reset(&sem_broadcast_sink_stopped);

	return 0;
}

static int pa_sync_create(void)
{
	struct bt_le_per_adv_sync_param create_params = {0};

	bt_addr_le_copy(&create_params.addr, &broadcaster_addr);
	create_params.options = BT_LE_PER_ADV_SYNC_OPT_FILTER_DUPLICATE;
	create_params.sid = broadcaster_info.sid;
	create_params.skip = PA_SYNC_SKIP;
	create_params.timeout = interval_to_sync_timeout(broadcaster_info.interval);

	return bt_le_per_adv_sync_create(&create_params, &pa_sync);
}

int main(void)
{
	int err;

	err = init();
	if (err) {
		printk("Init failed (err %d)\n", err);
		return 0;
	}

	for (size_t i = 0U; i < ARRAY_SIZE(streams_p); i++) {
		streams_p[i] = &streams[i].stream;
	}

	while (true) {
		uint32_t sync_bitfield;

		err = reset();
		if (err != 0) {
			printk("Resetting failed: %d - Aborting\n", err);

			return 0;
		}

		if (strlen(CONFIG_TARGET_BROADCAST_NAME) > 0U) {
			printk("Scanning for broadcast sources containing "
			       "`" CONFIG_TARGET_BROADCAST_NAME "`\n");
		} else {
			printk("Scanning for broadcast sources\n");
		}

		err = bt_le_scan_start(BT_LE_SCAN_ACTIVE, NULL);
		if (err != 0 && err != -EALREADY) {
			printk("Unable to start scan for broadcast sources: %d\n", err);
			return 0;
		}

		printk("Waiting for Broadcaster\n");
		err = k_sem_take(&sem_broadcaster_found, SEM_TIMEOUT);
		if (err != 0) {
			printk("sem_broadcaster_found timed out, resetting\n");
			continue;
		}

		err = bt_le_scan_stop();
		if (err != 0) {
			printk("bt_le_scan_stop failed with %d, resetting\n", err);
			continue;
		}

		printk("Attempting to PA sync to the broadcaster with id 0x%06X\n",
		       broadcaster_broadcast_id);
		err = pa_sync_create();
		if (err != 0) {
			printk("Could not create Broadcast PA sync: %d, resetting\n", err);
			continue;
		}

		printk("Waiting for PA synced\n");
		err = k_sem_take(&sem_pa_synced, SEM_TIMEOUT);
		if (err != 0) {
			printk("sem_pa_synced timed out, resetting\n");
			continue;
		}

		printk("Broadcast source PA synced, creating Broadcast Sink\n");
		err = bt_bap_broadcast_sink_create(pa_sync, broadcaster_broadcast_id,
						   &broadcast_sink);
		if (err != 0) {
			printk("Failed to create broadcast sink: %d\n", err);
			continue;
		}

		printk("Broadcast Sink created, waiting for BASE\n");
		err = k_sem_take(&sem_base_received, SEM_TIMEOUT);
		if (err != 0) {
			printk("sem_base_received timed out, resetting\n");
			continue;
		}

		printk("BASE received, waiting for syncable\n");
		err = k_sem_take(&sem_syncable, SEM_TIMEOUT);
		if (err != 0) {
			printk("sem_syncable timed out, resetting\n");
			continue;
		}

		/* sem_broadcast_code_received is also given if the
		 * broadcast is not encrypted
		 */
		printk("Waiting for broadcast code\n");
		err = k_sem_take(&sem_broadcast_code_received, SEM_TIMEOUT);
		if (err != 0) {
			printk("sem_broadcast_code_received timed out, resetting\n");
			continue;
		}

		printk("Waiting for BIS sync request\n");
		err = k_sem_take(&sem_bis_sync_requested, SEM_TIMEOUT);
		if (err != 0) {
			printk("sem_bis_sync_requested timed out, resetting\n");
			continue;
		}

		sync_bitfield = bis_index_bitfield & requested_bis_sync;
		stream_count = 0;
		for (int i = 1; i < BT_ISO_MAX_GROUP_ISO_COUNT; i++) {
			if ((sync_bitfield & BIT(i)) != 0) {
				stream_count++;
			}
		}

		printk("Syncing to broadcast with bitfield: 0x%08x = 0x%08x (bis_index) & 0x%08x "
		       "(req_bis_sync), stream_count = %u\n",
		       sync_bitfield, bis_index_bitfield, requested_bis_sync, stream_count);

		err = bt_bap_broadcast_sink_sync(broadcast_sink, sync_bitfield, streams_p,
						 sink_broadcast_code);
		if (err != 0) {
			printk("Unable to sync to broadcast source: %d\n", err);
			return 0;
		}

		printk("Waiting for stream(s) started\n");
		err = k_sem_take(&sem_big_synced, SEM_TIMEOUT);
		if (err != 0) {
			printk("sem_big_synced timed out, resetting\n");
			continue;
		}

		printk("Waiting for PA disconnected\n");
		k_sem_take(&sem_pa_sync_lost, K_FOREVER);

		printk("Waiting for sink to stop\n");
		err = k_sem_take(&sem_broadcast_sink_stopped, SEM_TIMEOUT);
		if (err != 0) {
			printk("sem_broadcast_sink_stopped timed out, resetting\n");
			continue;
		}
	}

	return 0;
}

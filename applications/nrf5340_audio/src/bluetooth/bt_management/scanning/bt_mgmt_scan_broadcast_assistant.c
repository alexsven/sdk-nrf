/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/audio/bap.h>
#include <zephyr/sys/util.h>

#include "bt_mgmt.h"
#include "macros_common.h"
#include "nrf5340_audio_common.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(bro_assistant, 3);

#define INVALID_BROADCAST_ID 0xFFFFFFFFU

static struct bt_bap_base received_base;
static struct bt_conn *default_conn;
struct bt_le_per_adv_sync *default_pa_sync;

static bool pa_decode_base(struct bt_data *data, void *user_data)
{
	struct bt_bap_base base = {0};
	int err;

	if (data->type != BT_DATA_SVC_DATA16) {
		return true;
	}

	if (data->data_len < BT_BAP_BASE_MIN_SIZE) {
		return true;
	}

	err = bt_bap_decode_base(data, &base);
	if (err != 0 && err != -ENOMSG) {
		LOG_ERR("Failed to decode BASE: %d", err);

		return false;
	}

	/* Compare BASE and print if different */
	if (memcmp(&base, &received_base, sizeof(base)) != 0) {
		(void)memcpy(&received_base, &base, sizeof(base));
	}

	return false;
}

static void pa_recv(struct bt_le_per_adv_sync *sync,
		    const struct bt_le_per_adv_sync_recv_info *info, struct net_buf_simple *buf)
{
	bt_data_parse(buf, pa_decode_base, NULL);
}

static void bap_broadcast_assistant_discover_cb(struct bt_conn *conn, int err,
						uint8_t recv_state_count)
{
	if (err != 0) {
		LOG_ERR("BASS discover failed (%d)", err);
	} else {
		LOG_DBG("BASS discover done with %u recv states", recv_state_count);
	}
}

static void bap_broadcast_assistant_scan_cb(const struct bt_le_scan_recv_info *info,
					    uint32_t broadcast_id)
{
	char le_addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(info->addr, le_addr, sizeof(le_addr));
	LOG_DBG("[DEVICE]: %s, broadcast_id 0x%06X, interval (ms) %u), SID 0x%x, RSSI %i", le_addr,
		broadcast_id, BT_GAP_PER_ADV_INTERVAL_TO_MS(info->interval), info->sid, info->rssi);
}

static bool metadata_entry(struct bt_data *data, void *user_data)
{
	char metadata[512];

	bin2hex(data->data, data->data_len, metadata, sizeof(metadata));

	LOG_DBG("\t\tMetadata length %u, type %u, data: %s", data->data_len, data->type, metadata);

	return true;
}

static void
bap_broadcast_assistant_recv_state_cb(struct bt_conn *conn, int err,
				      const struct bt_bap_scan_delegator_recv_state *state)
{
	char le_addr[BT_ADDR_LE_STR_LEN];
	char bad_code[33];
	bool is_bad_code;

	if (err != 0) {
		LOG_ERR("BASS recv state read failed (%d)", err);
		return;
	}

	bt_addr_le_to_str(&state->addr, le_addr, sizeof(le_addr));
	bin2hex(state->bad_code, BT_AUDIO_BROADCAST_CODE_SIZE, bad_code, sizeof(bad_code));

	is_bad_code = state->encrypt_state == BT_BAP_BIG_ENC_STATE_BAD_CODE;
	LOG_DBG("BASS recv state: src_id %u, addr %s, "
		"sid %u, sync_state %u, encrypt_state %u%s%s",
		state->src_id, le_addr, state->adv_sid, state->pa_sync_state, state->encrypt_state,
		is_bad_code ? ", bad code" : "", bad_code);

	for (int i = 0; i < state->num_subgroups; i++) {
		const struct bt_bap_scan_delegator_subgroup *subgroup = &state->subgroups[i];
		struct net_buf_simple buf;

		LOG_DBG("\t[%d]: BIS sync 0x%04X, metadata_len %u", i, subgroup->bis_sync,
			subgroup->metadata_len);

		net_buf_simple_init_with_data(&buf, (void *)subgroup->metadata,
					      subgroup->metadata_len);
		bt_data_parse(&buf, metadata_entry, NULL);
	}

	if (state->pa_sync_state == BT_BAP_PA_STATE_INFO_REQ ||
	    state->pa_sync_state == BT_BAP_PA_STATE_NOT_SYNCED) {
		if (default_pa_sync) {
			LOG_DBG("Sending PAST");

			err = bt_le_per_adv_sync_transfer(default_pa_sync, conn, BT_UUID_BASS_VAL);

			if (err != 0) {
				LOG_ERR("Could not transfer periodic adv sync: %d", err);
			}

			return;
		}
	}
}

static void bap_broadcast_assistant_recv_state_removed_cb(struct bt_conn *conn, int err,
							  uint8_t src_id)
{
	if (err != 0) {
		LOG_ERR("BASS recv state removed failed (%d)", err);
	} else {
		LOG_DBG("BASS recv state %u removed", src_id);
	}
}

static void bap_broadcast_assistant_scan_start_cb(struct bt_conn *conn, int err)
{
	if (err != 0) {
		LOG_ERR("BASS scan start failed (%d)", err);
	} else {
		LOG_DBG("BASS scan start successful");
	}
}

static void bap_broadcast_assistant_scan_stop_cb(struct bt_conn *conn, int err)
{
	if (err != 0) {
		LOG_ERR("BASS scan stop failed (%d)", err);
	} else {
		LOG_DBG("BASS scan stop successful");
	}
}

static void bap_broadcast_assistant_add_src_cb(struct bt_conn *conn, int err)
{
	if (err != 0) {
		LOG_ERR("BASS add source failed (%d)", err);
	} else {
		LOG_DBG("BASS add source successful");
	}
}

static void bap_broadcast_assistant_mod_src_cb(struct bt_conn *conn, int err)
{
	if (err != 0) {
		LOG_ERR("BASS modify source failed (%d)", err);
	} else {
		LOG_DBG("BASS modify source successful");
	}
}

static void bap_broadcast_assistant_broadcast_code_cb(struct bt_conn *conn, int err)
{
	if (err != 0) {
		LOG_ERR("BASS broadcast code failed (%d)", err);
	} else {
		LOG_DBG("BASS broadcast code successful");
	}
}

static void bap_broadcast_assistant_rem_src_cb(struct bt_conn *conn, int err)
{
	if (err != 0) {
		LOG_ERR("BASS remove source failed (%d)", err);
	} else {
		LOG_DBG("BASS remove source successful");
	}
}

static struct bt_bap_broadcast_assistant_cb cbs = {
	.discover = bap_broadcast_assistant_discover_cb,
	.scan = bap_broadcast_assistant_scan_cb,
	.recv_state = bap_broadcast_assistant_recv_state_cb,
	.recv_state_removed = bap_broadcast_assistant_recv_state_removed_cb,
	.scan_start = bap_broadcast_assistant_scan_start_cb,
	.scan_stop = bap_broadcast_assistant_scan_stop_cb,
	.add_src = bap_broadcast_assistant_add_src_cb,
	.mod_src = bap_broadcast_assistant_mod_src_cb,
	.broadcast_code = bap_broadcast_assistant_broadcast_code_cb,
	.rem_src = bap_broadcast_assistant_rem_src_cb,
};

int bt_mgmt_scan_broadcast_assistant_add_src(struct bt_le_per_adv_sync *pa_sync,
					     uint32_t broadcast_id)
{
	struct bt_bap_scan_delegator_subgroup subgroup_params[BT_ISO_MAX_GROUP_ISO_COUNT] = {0};
	struct bt_bap_broadcast_assistant_add_src_param param = {0};
	/* TODO: Add support to select which PA sync to BIG sync to */
	struct bt_le_per_adv_sync_info pa_info;
	uint32_t bis_bitfield_req;
	int err;

	if (pa_sync == NULL) {
		LOG_ERR("PA not synced");

		return -ENOEXEC;
	}

	default_pa_sync = pa_sync;

	err = bt_le_per_adv_sync_get_info(pa_sync, &pa_info);
	if (err != 0) {
		LOG_ERR("Could not get PA sync info: %d", err);

		return -ENOEXEC;
	}

	bt_addr_le_copy(&param.addr, &pa_info.addr);
	param.adv_sid = pa_info.sid;
	param.pa_interval = pa_info.interval;

	param.pa_sync = true;
	param.broadcast_id = broadcast_id;

	bis_bitfield_req = 2U;

	/* The MIN is used to handle `array-bounds` error on some compilers */
	param.num_subgroups = MIN(received_base.subgroup_count, BROADCAST_SNK_SUBGROUP_CNT);
	param.subgroups = subgroup_params;

	for (size_t i = 0; i < param.num_subgroups; i++) {
		struct bt_bap_scan_delegator_subgroup *subgroup_param = &subgroup_params[i];
		const struct bt_bap_base_subgroup *subgroup = &received_base.subgroups[i];
		uint32_t subgroup_bis_indexes = 0U;
		ssize_t metadata_len;

		for (size_t j = 0U; j < MIN(subgroup->bis_count, ARRAY_SIZE(subgroup->bis_data));
		     j++) {
			const struct bt_bap_base_bis_data *bis_data = &subgroup->bis_data[j];

			subgroup_bis_indexes |= BIT(bis_data->index);
		}

		subgroup_param->bis_sync = subgroup_bis_indexes & bis_bitfield_req;

#if CONFIG_BT_CODEC_MAX_METADATA_COUNT > 0
		metadata_len = bt_audio_codec_data_to_buf(
			subgroup->codec.meta, subgroup->codec.meta_count, subgroup_param->metadata,
			sizeof(subgroup_param->metadata));
		if (metadata_len < 0) {
			return -ENOMEM;
		}
#else
		metadata_len = 0U;
#endif /* CONFIG_BT_CODEC_MAX_METADATA_COUNT > 0 */
		subgroup_param->metadata_len = metadata_len;
	}

	err = bt_bap_broadcast_assistant_add_src(default_conn, &param);
	if (err != 0) {
		LOG_ERR("Fail: %d", err);

		return -ENOEXEC;
	}

	return 0;
}

int bt_mgmt_scan_broadcast_assistant_discover(struct bt_conn *conn)
{
	static bool registered;
	int result;

	if (!registered) {
		default_conn = conn;
		static struct bt_le_per_adv_sync_cb cb = {
			.recv = pa_recv,
		};

		bt_le_per_adv_sync_cb_register(&cb);

		bt_bap_broadcast_assistant_register_cb(&cbs);

		registered = true;
	}

	result = bt_bap_broadcast_assistant_discover(default_conn);

	return result;
}

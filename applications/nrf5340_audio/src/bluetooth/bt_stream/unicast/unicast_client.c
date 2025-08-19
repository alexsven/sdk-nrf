/*
 * Copyright (c) 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include "unicast_client.h"

#include <zephyr/zbus/zbus.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/audio/audio.h>
#include <zephyr/bluetooth/audio/csip.h>
#include <zephyr/bluetooth/audio/cap.h>
#include <zephyr/bluetooth/audio/bap.h>
#include <zephyr/bluetooth/audio/bap_lc3_preset.h>
#include <../subsys/bluetooth/audio/bap_iso.h>

/* TODO: Remove when a qos_pref_get function has been added in host */
/* https://github.com/zephyrproject-rtos/zephyr/issues/72359 */
#include <../subsys/bluetooth/audio/bap_endpoint.h>

#include "macros_common.h"
#include "zbus_common.h"
#include "bt_le_audio_tx.h"
#include "le_audio.h"
#include "server_store.h"

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(unicast_client, 4);

ZBUS_CHAN_DEFINE(le_audio_chan, struct le_audio_msg, NULL, NULL, ZBUS_OBSERVERS_EMPTY,
		 ZBUS_MSG_INIT(0));

#define CAP_PROCED_MUTEX_WAIT_TIME_MS K_MSEC(500)

/* For unicast (as opposed to broadcast) level 2/subgroup is not defined in the specification */
#define LVL2 0

struct discover_dir {
	struct bt_conn *conn;
	bool sink;
	bool source;
};

K_MSGQ_DEFINE(cap_start_q, sizeof(struct bt_conn **), CONFIG_BT_ISO_MAX_CHAN, sizeof(void *));

BUILD_ASSERT(CONFIG_BT_ISO_MAX_CIG == 1, "Only one CIG is supported");

static le_audio_receive_cb receive_cb;

static struct bt_bap_unicast_group *unicast_group;
static bool unicast_group_created;

static bool playing_state = true;

static void le_audio_event_publish(enum le_audio_evt_type event, struct bt_conn *conn,
				   enum bt_audio_dir dir)
{
	int ret;
	struct le_audio_msg msg;

	msg.event = event;
	msg.conn = conn;
	msg.dir = dir;

	ret = zbus_chan_pub(&le_audio_chan, &msg, LE_AUDIO_ZBUS_EVENT_WAIT_TIME);
	ERR_CHK(ret);
}

K_MUTEX_DEFINE(mtx_cap_procedure_proceed);

static void create_group(void)
{
	int ret;
	struct bt_bap_unicast_group_stream_pair_param
		pair_params[CONFIG_BT_BAP_UNICAST_CLIENT_GROUP_STREAM_COUNT / 2];
	struct bt_bap_unicast_group_stream_param
		group_sink_stream_params[CONFIG_BT_BAP_UNICAST_CLIENT_GROUP_STREAM_COUNT];
	struct bt_bap_unicast_group_stream_param
		group_source_stream_params[CONFIG_BT_BAP_UNICAST_CLIENT_GROUP_STREAM_COUNT];
	struct bt_bap_unicast_group_param group_param;

	/* Find out how many servers we have connected */
	uint8_t num_servers = srv_store_num_get();
	if (num_servers == 0) {
		LOG_ERR("No servers found, cannot create unicast group");
		return;
	}

	/* Remove */
	/* Find out how many valid sink EPs we have */
	uint8_t num_valid_sink_eps = 0;
	uint8_t num_valid_source_eps = 0;
	for (int i = 0; i < num_servers; i++) {
		struct server_store *tmp_server = NULL;
		ret = srv_store_server_get(&tmp_server, i);
		if (ret < 0) {
			LOG_ERR("Failed to get server store from index %d: %d", i, ret);
			return;
		}

		num_valid_sink_eps += tmp_server->snk.num_eps;
		num_valid_source_eps += tmp_server->src.num_eps;
	}

	LOG_INF("We have %d servers, with a total of %d valid sink EPs and %d valid source "
		"EPs",
		num_servers, num_valid_sink_eps, num_valid_source_eps);

	if (num_valid_sink_eps == 0 && num_valid_source_eps == 0) {
		LOG_ERR("No valid sink or source EPs found, cannot create unicast group");
		return;
	}

	uint8_t group_sink_iterator = 0;
	uint8_t group_source_iterator = 0;

	for (int i = 0; i < num_servers; i++) {
		struct server_store *tmp_server = NULL;
		ret = srv_store_server_get(&tmp_server, i);
		if (ret < 0) {
			LOG_ERR("Failed to get server store from index %d: %d", i, ret);
			return;
		}
		if (tmp_server->snk.num_eps == 0 && tmp_server->src.num_eps == 0) {
			LOG_WRN("Server %d has no valid sink or source EPs, skipping", i);
			continue;
		}

		/* Add only the streams that has a valid preset set */
		for (int j = 0; j < tmp_server->snk.num_eps; j++) {
			if (tmp_server->snk.lc3_preset[j].qos.pd == 0) {
				LOG_WRN("Sink EP %d has no valid preset, skipping", j);
				continue;
			}
			group_sink_stream_params[group_sink_iterator].qos =
				&tmp_server->snk.lc3_preset[0].qos;
			group_sink_stream_params[group_sink_iterator].stream =
				&tmp_server->snk.cap_streams[j].bap_stream;
			group_sink_iterator++;
		}

		/* Add only the streams that has a valid preset set */
		for (int j = 0; j < tmp_server->src.num_eps; j++) {
			if (tmp_server->src.lc3_preset[j].qos.pd == 0) {
				LOG_WRN("Source EP %d has no valid preset, skipping", j);
				continue;
			}
			group_source_stream_params[group_source_iterator].qos =
				&tmp_server->src.lc3_preset[0].qos;
			group_source_stream_params[group_source_iterator].stream =
				&tmp_server->src.cap_streams[j].bap_stream;
			group_source_iterator++;
		}
	}

	int stream_iterator = 0;

	/* Pair TX and RX from same server */
	/* We pair in the order of sink to source because the sink stream will always be created
	 * before the source stream
	 */
	for (int i = 0; i < group_sink_iterator; i++) {
		pair_params[i].tx_param = &group_sink_stream_params[i];
		/* Search streams for a matching source */
		for (int j = stream_iterator; j < group_source_iterator; j++) {
			/* Check if the source stream belongs to the same connection */
			if (group_sink_stream_params[i].stream->conn ==
			    group_source_stream_params[j].stream->conn) {
				pair_params[i].rx_param = &group_source_stream_params[j];
				stream_iterator++;
				break;
			}
		}
		pair_params[i].rx_param = NULL;
		stream_iterator++;
	}

	group_param.params = pair_params;
	group_param.params_count = stream_iterator;

	if (IS_ENABLED(CONFIG_BT_AUDIO_PACKING_INTERLEAVED)) {
		group_param.packing = BT_ISO_PACKING_INTERLEAVED;
	} else {
		group_param.packing = BT_ISO_PACKING_SEQUENTIAL;
	}

	ret = bt_bap_unicast_group_create(&group_param, &unicast_group);
	if (ret) {
		LOG_ERR("Failed to create unicast group: %d", ret);
	} else {
		unicast_group_created = true;
	}
}

static void cap_start_worker(struct k_work *work)
{
	int ret;
	struct bt_conn *conn;

	/* Check msgq for a pending start procedure */
	ret = k_msgq_get(&cap_start_q, &conn, K_NO_WAIT);
	if (ret) {
		LOG_ERR("Failed to get device index for pending cap start procedure: %d", ret);
		return;
	}

	/* Create unicast group if it doesn't exist */
	if (unicast_group_created == false) {
		create_group();
	} else {
		/* Length of current unicast group */
		uint8_t group_length = sys_slist_len(&unicast_group->streams);
		if (group_length >= CONFIG_BT_BAP_UNICAST_CLIENT_GROUP_STREAM_COUNT) {
			LOG_ERR("Unicast group is full, cannot add more streams");
			return;
		}

		if (group_length < srv_store_num_get()) {
			LOG_WRN("Unicast group has less streams than servers, creating new "
				"group");
			unicast_client_stop(0);
			unicast_group_created = false;

			/* A new group will be created after the released_cb has been called
			 */
			return;
		}
	}

	ret = unicast_client_start(0);
	if (ret < 0) {
		LOG_ERR("Failed to start unicast client: %d", ret);
		return;
	}
}
K_WORK_DEFINE(cap_start_work, cap_start_worker);

// static int update_cap_sink_stream_qos(struct server_store *server, uint32_t pres_delay_us)
// {
// 	int ret;

// 	if (server->snk.cap_streams.bap_stream.ep == NULL) {
// 		return -ESRCH;
// 	}

// 	if (unicast_server->cap_sink_stream.bap_stream.qos == NULL) {
// 		LOG_WRN("No QoS found for %p", (void *)&unicast_server->cap_sink_stream.bap_stream);
// 		return -ENXIO;
// 	}

// 	if (unicast_server->cap_sink_stream.bap_stream.qos->pd != pres_delay_us) {
// 		struct bt_cap_unicast_audio_stop_param param;
// 		struct bt_cap_stream *streams[2];

// 		LOG_DBG("Current preset PD = %d us, target PD = %d us",
// 			unicast_server->cap_sink_stream.bap_stream.qos->pd, pres_delay_us);

// 		param.streams = streams;
// 		param.count = 0;
// 		param.type = BT_CAP_SET_TYPE_AD_HOC;
// 		param.release = true;

// 		if (playing_state &&
// 		    le_audio_ep_state_check(unicast_server->cap_sink_stream.bap_stream.ep,
// 					    BT_BAP_EP_STATE_STREAMING)) {
// 			LOG_DBG("Update streaming %s unicast_server, connection %p, stream %p",
// 				unicast_server->name, (void *)&unicast_server->device_conn,
// 				(void *)&unicast_server->cap_sink_stream.bap_stream);

// 			unicast_server->qos_reconfigure = true;
// 			unicast_server->reconfigure_pd = pres_delay_us;

// 			streams[param.count] = &unicast_server->cap_sink_stream;
// 			param.count++;
// 		} else {
// 			LOG_DBG("Reset %s unicast_server, connection %p, stream %p",
// 				unicast_server->name, (void *)&unicast_server->device_conn,
// 				(void *)&unicast_server->cap_sink_stream.bap_stream);
// 			unicast_server->cap_sink_stream.bap_stream.qos->pd = pres_delay_us;
// 		}

// 		if (playing_state &&
// 		    le_audio_ep_state_check(unicast_server->cap_source_stream.bap_stream.ep,
// 					    BT_BAP_EP_STATE_STREAMING)) {
// 			unicast_server->qos_reconfigure = true;
// 			unicast_server->reconfigure_pd = pres_delay_us;

// 			streams[param.count] = &unicast_server->cap_source_stream;
// 			param.count++;
// 		}

// 		if (param.count > 0) {
// 			ret = bt_cap_initiator_unicast_audio_stop(&param);
// 			if (ret) {
// 				LOG_WRN("Failed to stop streams: %d, use default PD in preset",
// 					ret);
// 				return ret;
// 			}
// 		}
// 	}

// 	return 0;
// }

static void unicast_client_location_cb(struct bt_conn *conn, enum bt_audio_dir dir,
				       enum bt_audio_location loc)
{
	int ret;
	struct server_store *server = NULL;

	ret = srv_store_from_conn_get(conn, &server);
	if (ret) {
		LOG_ERR("Unknown connection, should not reach here");
		return;
	}

	if (loc & BT_AUDIO_LOCATION_FRONT_LEFT && loc & BT_AUDIO_LOCATION_FRONT_RIGHT) {
		LOG_INF("Both front left and right channel locations are set, stereo device found");
		srv_store_location_set(
			conn, dir, BT_AUDIO_LOCATION_FRONT_LEFT | BT_AUDIO_LOCATION_FRONT_RIGHT);
		server->name = "STEREO";
	} else if ((loc & BT_AUDIO_LOCATION_FRONT_LEFT) || (loc & BT_AUDIO_LOCATION_BACK_LEFT) ||
		   (loc & BT_AUDIO_LOCATION_FRONT_LEFT_OF_CENTER) ||
		   (loc & BT_AUDIO_LOCATION_SIDE_LEFT) ||
		   (loc & BT_AUDIO_LOCATION_TOP_FRONT_LEFT) ||
		   (loc & BT_AUDIO_LOCATION_TOP_BACK_LEFT) ||
		   (loc & BT_AUDIO_LOCATION_TOP_SIDE_LEFT) ||
		   (loc & BT_AUDIO_LOCATION_BOTTOM_FRONT_LEFT) ||
		   (loc & BT_AUDIO_LOCATION_FRONT_LEFT_WIDE) ||
		   (loc & BT_AUDIO_LOCATION_LEFT_SURROUND) ||
		   (loc == BT_AUDIO_LOCATION_MONO_AUDIO)) {
		ret = srv_store_location_set(conn, dir, BT_AUDIO_LOCATION_FRONT_LEFT);
		if (ret) {
			LOG_ERR("Failed to set location for conn %p, dir %d, loc %d: %d",
				(void *)conn, dir, loc, ret);
			return;
		}
		server->name = "LEFT";

	} else if ((loc & BT_AUDIO_LOCATION_FRONT_RIGHT) || (loc & BT_AUDIO_LOCATION_BACK_RIGHT) ||
		   (loc & BT_AUDIO_LOCATION_FRONT_RIGHT_OF_CENTER) ||
		   (loc & BT_AUDIO_LOCATION_SIDE_RIGHT) ||
		   (loc & BT_AUDIO_LOCATION_TOP_FRONT_RIGHT) ||
		   (loc & BT_AUDIO_LOCATION_TOP_BACK_RIGHT) ||
		   (loc & BT_AUDIO_LOCATION_TOP_SIDE_RIGHT) ||
		   (loc & BT_AUDIO_LOCATION_BOTTOM_FRONT_RIGHT) ||
		   (loc & BT_AUDIO_LOCATION_FRONT_RIGHT_WIDE) ||
		   (loc & BT_AUDIO_LOCATION_RIGHT_SURROUND)) {
		ret = srv_store_location_set(conn, dir, BT_AUDIO_LOCATION_FRONT_RIGHT);
		if (ret) {
			LOG_ERR("Failed to set location for conn %p, dir %d, loc %d: %d",
				(void *)conn, dir, loc, ret);
			return;
		}
		server->name = "RIGHT";
	} else {
		LOG_WRN("Channel location not supported: %d", loc);
		le_audio_event_publish(LE_AUDIO_EVT_NO_VALID_CFG, conn, dir);
	}
}

static void available_contexts_cb(struct bt_conn *conn, enum bt_audio_context snk_ctx,
				  enum bt_audio_context src_ctx)
{
	int ret;

	LOG_DBG("conn: %p, snk ctx %d src ctx %d", (void *)conn, snk_ctx, src_ctx);

	ret = srv_store_avail_context_set(conn, snk_ctx, src_ctx);
	if (ret) {
		LOG_ERR("Failed to set available contexts for conn %p, snk ctx %d src ctx %d: %d",
			(void *)conn, snk_ctx, src_ctx, ret);
		return;
	}
}

static void pac_record_cb(struct bt_conn *conn, enum bt_audio_dir dir,
			  const struct bt_audio_codec_cap *codec)
{
	int ret;

	if (codec->id != BT_HCI_CODING_FORMAT_LC3) {
		LOG_DBG("Only the LC3 codec is supported");
		return;
	}

	ret = srv_store_codec_cap_set(conn, dir, codec);
	if (ret) {
		LOG_ERR("Failed to set codec capability: %d", ret);
		return;
	}
}

static void endpoint_cb(struct bt_conn *conn, enum bt_audio_dir dir, struct bt_bap_ep *ep)
{
	int ret;

	struct server_store *server = NULL;
	ret = srv_store_from_conn_get(conn, &server);
	if (ret) {
		LOG_ERR("Unknown connection, should not reach here");
		return;
	}

	if (dir == BT_AUDIO_DIR_SINK) {
		if (ep != NULL) {
			if (server->snk.num_eps >= ARRAY_SIZE(server->snk.eps)) {
				LOG_WRN("No more space (%d) for sink endpoints, increase "
					"CONFIG_SNK_EP_COUNT_MAX (%d)",
					server->snk.num_eps, ARRAY_SIZE(server->snk.eps));
				return;
			}

			server->snk.eps[server->snk.num_eps] = ep;
			server->snk.num_eps++;
		}

		if (server->snk.eps[0] == NULL) {
			LOG_WRN("No sink endpoints found");
		}
	} else if (dir == BT_AUDIO_DIR_SOURCE) {
		if (ep != NULL) {
			if (server->src.num_eps >= ARRAY_SIZE(server->src.eps)) {
				LOG_WRN("No more space for source endpoints, increase "
					"CONFIG_SRC_EP_COUNT_MAX");
				return;
			}

			server->src.eps[server->src.num_eps] = ep;
			server->src.num_eps++;
		}

		if (server->src.eps[0] == NULL) {
			LOG_WRN("No source endpoints found");
		}
	} else {
		LOG_WRN("Endpoint direction not recognized: %d", dir);
	}
}

static void discover_cb(struct bt_conn *conn, int err, enum bt_audio_dir dir)
{
	int ret;

	struct server_store *server = NULL;
	ret = srv_store_from_conn_get(conn, &server);
	if (ret) {
		LOG_ERR("Unknown connection, should not reach here");
		return;
	}

	if (err == BT_ATT_ERR_ATTRIBUTE_NOT_FOUND) {
		if (dir == BT_AUDIO_DIR_SINK) {
			LOG_WRN("No sinks found");
			server->snk.waiting_for_disc = false;
		} else if (dir == BT_AUDIO_DIR_SOURCE) {
			LOG_WRN("No sources found");
			server->src.waiting_for_disc = false;
		}
	} else if (err) {
		LOG_ERR("Discovery failed: %d", err);
		return;
	}

	if (dir == BT_AUDIO_DIR_SINK && !err) {
		uint32_t valid_sink_caps = 0;

		ret = srv_store_valid_codec_cap_check(conn, dir, &valid_sink_caps);
		if (valid_sink_caps) {

			/* Get the valid configuration to set for this stream and put that in the
			 * corresponding preset */

			LOG_INF("Found %d valid PAC record(s), %d location(s), %d snk ep(s) %d src "
				"ep(s)",
				POPCOUNT(valid_sink_caps), POPCOUNT(server->snk.locations),
				server->snk.num_eps, server->src.num_eps);

			if (POPCOUNT(server->snk.locations) == 1 &&
			    POPCOUNT(valid_sink_caps) >= 1 && server->snk.num_eps >= 1) {

				ret = bt_audio_codec_cfg_set_val(
					&server->snk.lc3_preset[0].codec_cfg,
					BT_AUDIO_CODEC_CFG_CHAN_ALLOC,
					(uint8_t *)&server->snk.locations,
					sizeof(server->snk.locations));
				if (ret < 0) {
					LOG_ERR("Failed to set codec channel allocation: %d", ret);
					return;
				}

			} else if (POPCOUNT(server->snk.locations) == 2 &&
				   POPCOUNT(valid_sink_caps) >= 1 && server->snk.num_eps >= 2) {

				uint32_t left = BT_AUDIO_LOCATION_FRONT_LEFT;
				uint32_t right = BT_AUDIO_LOCATION_FRONT_RIGHT;

				ret = bt_audio_codec_cfg_set_val(
					&server->snk.lc3_preset[0].codec_cfg,
					BT_AUDIO_CODEC_CFG_CHAN_ALLOC, (uint8_t *)&left,
					sizeof(server->snk.locations));
				if (ret < 0) {
					LOG_ERR("Failed to set codec channel allocation: %d", ret);
					return;
				}

				memcpy(&server->snk.lc3_preset[1].codec_cfg,
				       &server->snk.lc3_preset[0].codec_cfg,
				       sizeof(struct bt_audio_codec_cfg));

				ret = bt_audio_codec_cfg_set_val(
					&server->snk.lc3_preset[1].codec_cfg,
					BT_AUDIO_CODEC_CFG_CHAN_ALLOC, (uint8_t *)&right,
					sizeof(server->snk.locations));
				if (ret < 0) {
					LOG_ERR("Failed to set codec channel allocation: %d", ret);
					return;
				}
			} else {
				LOG_WRN("Unsupported unicast server/headset configuration");
				return;
			}

		} else {
			/* NOTE: The string below is used by the Nordic CI system */
			LOG_WRN("No valid codec capability found for %s sink", server->name);
		}
	} else if (dir == BT_AUDIO_DIR_SOURCE && !err) {
		uint32_t valid_source_caps = 0;

		ret = srv_store_valid_codec_cap_check(conn, dir, &valid_source_caps);
		if (valid_source_caps) {
			ret = bt_audio_codec_cfg_set_val(
				&server->src.lc3_preset[0].codec_cfg, BT_AUDIO_CODEC_CFG_CHAN_ALLOC,
				(uint8_t *)&server->src.locations, sizeof(server->src.locations));
			if (ret < 0) {
				LOG_ERR("Failed to set codec channel allocation: %d", ret);
				return;
			}
		} else {
			LOG_WRN("No valid codec capability found for %s source", server->name);
		}
	}

	if (dir == BT_AUDIO_DIR_SINK) {
		server->snk.waiting_for_disc = false;

		if (server->src.waiting_for_disc) {
			ret = bt_bap_unicast_client_discover(conn, BT_AUDIO_DIR_SOURCE);
			if (ret) {
				LOG_WRN("Failed to discover source: %d", ret);
			}

			return;
		}
	} else if (dir == BT_AUDIO_DIR_SOURCE) {
		server->src.waiting_for_disc = false;
	}

	if (!playing_state) {
		/* Since we are not in a playing state we return before starting the new
		 * streams
		 */
		return;
	}

	ret = k_msgq_put(&cap_start_q, &conn, K_NO_WAIT);
	if (ret) {
		LOG_ERR("Failed to put device_index on the queue: %d", ret);
		return;
	}

	// /* If we have a valid stereo idx, put it on the queue */
	// if (memcmp(&stereo_idx, &idx, sizeof(struct stream_index)) && stereo_idx.lvl3 != 0) {
	// 	ret = k_msgq_put(&cap_start_q, &stereo_idx, K_NO_WAIT);
	// 	if (ret) {
	// 		LOG_ERR("Failed to put stereo device_index on the queue: %d", ret);
	// 		return;
	// 	}
	// }

	k_work_submit(&cap_start_work);
}

#if (CONFIG_BT_AUDIO_TX)
static void stream_sent_cb(struct bt_bap_stream *stream)
{
	int ret;
	struct stream_index idx;
	uint8_t state;

	ret = le_audio_ep_state_get(stream->ep, &state);
	if (ret) {
		return;
	}

	if (state == BT_BAP_EP_STATE_STREAMING) {

		const uint8_t *loc;

		bt_audio_codec_cfg_get_val(stream->codec_cfg, BT_AUDIO_CODEC_CFG_CHAN_ALLOC, &loc);

		/* Get the stream index for the stream */
		idx.lvl1 = 0;
		idx.lvl2 = LVL2;
		if (*loc & BT_AUDIO_LOCATION_FRONT_LEFT) {
			idx.lvl3 = 0;
		} else if (*loc & BT_AUDIO_LOCATION_FRONT_RIGHT) {
			idx.lvl3 = 1;
		} else {
			LOG_ERR("Unknown channel location: %d", *loc);
			return;
		}

		ERR_CHK(bt_le_audio_tx_stream_sent(idx));
	} else {
		LOG_WRN("Not in streaming state: %d", state);
	}
}
#endif /* CONFIG_BT_AUDIO_TX */

static void stream_configured_cb(struct bt_bap_stream *stream,
				 const struct bt_bap_qos_cfg_pref *pref)
{
	int ret;
	uint32_t new_pres_dly_us = 0;
	enum bt_audio_dir dir;

	struct server_store *server = NULL;
	ret = srv_store_from_stream_get(stream, &server);

	dir = le_audio_stream_dir_get(stream);
	if (dir <= 0) {
		LOG_ERR("Failed to get dir of stream %p", (void *)stream);
		return;
	}

	if (dir == BT_AUDIO_DIR_SINK) {
		/* NOTE: The string below is used by the Nordic CI system */
		LOG_INF("%s sink stream configured", server->name);
		le_audio_print_codec(stream->codec_cfg, dir);
	} else if (dir == BT_AUDIO_DIR_SOURCE) {
		LOG_INF("%s source stream configured", server->name);
		le_audio_print_codec(stream->codec_cfg, dir);
	} else {
		LOG_ERR("Endpoint direction not recognized: %d", dir);
		return;
	}
	LOG_DBG("Configured Stream info: %s, %p, dir %d", server->name, (void *)stream, dir);

	bool group_reconfigure_needed = false;

	ret = srv_store_pres_dly_find(stream, &new_pres_dly_us, pref, &group_reconfigure_needed);
	if (ret) {
		LOG_ERR("Cannot get a valid presentation delay");
		return;
	}

	if (server->src.waiting_for_disc) {
		return;
	}

	if (le_audio_ep_state_check(stream->ep, BT_BAP_EP_STATE_CODEC_CONFIGURED) &&
	    new_pres_dly_us != stream->qos->pd) {
		// check_and_update_pd_in_group(idx, new_pres_dly_us);
		stream->qos->pd = new_pres_dly_us;
	}

	le_audio_event_publish(LE_AUDIO_EVT_CONFIG_RECEIVED, stream->conn, dir);

	/* Make sure both sink and source ep (if both are discovered) are configured before
	 * QoS
	 */
	if ((server->snk.eps[0] != NULL &&
	     !le_audio_ep_state_check(stream->ep, BT_BAP_EP_STATE_CODEC_CONFIGURED)) ||
	    (server->src.eps[0] != NULL &&
	     !le_audio_ep_state_check(stream->ep, BT_BAP_EP_STATE_CODEC_CONFIGURED))) {
		return;
	}
}

static void stream_qos_set_cb(struct bt_bap_stream *stream)
{
	LOG_DBG("QoS set cb");
}

static void stream_enabled_cb(struct bt_bap_stream *stream)
{
	LOG_DBG("Stream enabled: %p", (void *)stream);
}

static void stream_started_cb(struct bt_bap_stream *stream)
{
	int ret;
	enum bt_audio_dir dir;

	dir = le_audio_stream_dir_get(stream);
	if (dir <= 0) {
		LOG_ERR("Failed to get dir of stream %p", (void *)stream);
		return;
	}

	struct stream_index idx = {0};
	if (IS_ENABLED(CONFIG_BT_AUDIO_TX)) {

		const uint8_t *loc;

		bt_audio_codec_cfg_get_val(stream->codec_cfg, BT_AUDIO_CODEC_CFG_CHAN_ALLOC, &loc);

		/* Get the stream index for the stream */
		idx.lvl1 = 0;
		idx.lvl2 = LVL2;
		if (*loc & BT_AUDIO_LOCATION_FRONT_LEFT) {
			idx.lvl3 = 0;
		} else if (*loc & BT_AUDIO_LOCATION_FRONT_RIGHT) {
			idx.lvl3 = 1;
		} else {
			LOG_ERR("Unknown channel location: %d", *loc);
			return;
		}

		ERR_CHK(bt_le_audio_tx_stream_started(idx));
	}

	/* NOTE: The string below is used by the Nordic CI system */
	LOG_INF("Stream %p started, idx: %d %d %d", (void *)stream, idx.lvl1, idx.lvl2, idx.lvl3);

	le_audio_event_publish(LE_AUDIO_EVT_STREAMING, stream->conn, dir);
}

static void stream_metadata_updated_cb(struct bt_bap_stream *stream)
{
	LOG_DBG("Audio Stream %p metadata updated", (void *)stream);
}

static void stream_disabled_cb(struct bt_bap_stream *stream)
{
	LOG_DBG("Audio Stream %p disabled", (void *)stream);
}

static void stream_stopped_cb(struct bt_bap_stream *stream, uint8_t reason)
{
	/* NOTE: The string below is used by the Nordic CI system */
	LOG_INF("Stream %p stopped. Reason %d", (void *)stream, reason);

	/* Check if the other streams are streaming, send event if not */
	if (srv_store_all_ep_state_count(BT_BAP_EP_STATE_STREAMING, BT_AUDIO_DIR_SINK) ||
	    srv_store_all_ep_state_count(BT_BAP_EP_STATE_STREAMING, BT_AUDIO_DIR_SOURCE)) {
		LOG_DBG("Other streams are still streaming, not publishing NOT_STREAMING event");
		return;
	}

	enum bt_audio_dir dir = 0;

	dir = le_audio_stream_dir_get(stream);
	if (dir <= 0) {
		LOG_ERR("Failed to get dir of stream %p", (void *)stream);
	}

	le_audio_event_publish(LE_AUDIO_EVT_NOT_STREAMING, stream->conn, dir);
}

static void stream_released_cb(struct bt_bap_stream *stream)
{
	int ret;

	LOG_DBG("Audio Stream %p released", (void *)stream);

	if (unicast_group_created == false) {
		ret = bt_bap_unicast_group_delete(unicast_group);
		if (ret) {
			LOG_ERR("Failed to delete unicast group: %d", ret);
		}

		/* Create a new unicast group */
		ret = k_msgq_put(&cap_start_q, NULL, K_NO_WAIT);
		if (ret) {
			LOG_ERR("Failed to put device_index on the queue: %d", ret);
			return;
		}
		k_work_submit(&cap_start_work);
		return;
	}

	/* Check if the other streams are streaming, send event if not */
	if (srv_store_all_ep_state_count(BT_BAP_EP_STATE_STREAMING, BT_AUDIO_DIR_SINK) ||
	    srv_store_all_ep_state_count(BT_BAP_EP_STATE_STREAMING, BT_AUDIO_DIR_SOURCE)) {
		LOG_DBG("Other streams are still streaming, not publishing NOT_STREAMING event");
		return;
	}

	enum bt_audio_dir dir = 0;

	dir = le_audio_stream_dir_get(stream);
	if (dir <= 0) {
		LOG_ERR("Failed to get dir of stream %p", (void *)stream);
	}

	le_audio_event_publish(LE_AUDIO_EVT_NOT_STREAMING, stream->conn, dir);
}

#if (CONFIG_BT_AUDIO_RX)
static void stream_recv_cb(struct bt_bap_stream *stream, const struct bt_iso_recv_info *info,
			   struct net_buf *audio_frame)
{
	int ret;
	struct audio_metadata meta;

	if (receive_cb == NULL) {
		LOG_ERR("The RX callback has not been set");
		return;
	}

	ret = le_audio_metadata_populate(&meta, stream, info, audio_frame);
	if (ret) {
		LOG_ERR("Failed to populate meta data: %d", ret);
		return;
	}

	struct stream_index idx;

	ret = srv_store_stream_idx_get(stream, &idx.lvl1, &idx.lvl3);
	if (ret) {
		LOG_ERR("Device index not found");
		return;
	}

	receive_cb(audio_frame, &meta, idx.lvl3);
}
#endif /* (CONFIG_BT_AUDIO_RX) */

static struct bt_bap_stream_ops stream_ops = {
	.configured = stream_configured_cb,
	.qos_set = stream_qos_set_cb,
	.enabled = stream_enabled_cb,
	.started = stream_started_cb,
	.metadata_updated = stream_metadata_updated_cb,
	.disabled = stream_disabled_cb,
	.stopped = stream_stopped_cb,
	.released = stream_released_cb,
#if (CONFIG_BT_AUDIO_RX)
	.recv = stream_recv_cb,
#endif /* (CONFIG_BT_AUDIO_RX) */
#if (CONFIG_BT_AUDIO_TX)
	.sent = stream_sent_cb,
#endif /* (CONFIG_BT_AUDIO_TX) */
};

static struct bt_bap_unicast_client_cb unicast_client_cbs = {
	.location = unicast_client_location_cb,
	.available_contexts = available_contexts_cb,
	.pac_record = pac_record_cb,
	.endpoint = endpoint_cb,
	.discover = discover_cb,
};

static void unicast_discovery_complete_cb(struct bt_conn *conn, int err,
					  const struct bt_csip_set_coordinator_set_member *member,
					  const struct bt_csip_set_coordinator_csis_inst *csis_inst)
{
	int ret;
	struct le_audio_msg msg;
	struct server_store *server = NULL;

	ret = srv_store_from_conn_get(conn, &server);
	if (ret) {
		LOG_ERR("Unknown connection, should not reach here");
		return;
	}

	if (err || csis_inst == NULL) {
		LOG_WRN("Got err: %d from conn: %p", err, (void *)conn);
		msg.set_size = 0;
		msg.sirk = NULL;
	} else {
		LOG_DBG("\tErr: %d, set_size: %d", err, csis_inst->info.set_size);
		LOG_HEXDUMP_DBG(csis_inst->info.sirk, BT_CSIP_SIRK_SIZE, "\tSIRK:");

		server->member = member;
		msg.set_size = csis_inst->info.set_size;
		msg.sirk = csis_inst->info.sirk;
	}

	LOG_DBG("Unicast discovery complete cb");

	msg.event = LE_AUDIO_EVT_COORD_SET_DISCOVERED;
	msg.conn = conn;

	ret = zbus_chan_pub(&le_audio_chan, &msg, LE_AUDIO_ZBUS_EVENT_WAIT_TIME);
	ERR_CHK(ret);
}

static void unicast_start_complete_cb(int err, struct bt_conn *conn)
{
	int ret;

	k_mutex_unlock(&mtx_cap_procedure_proceed);

	if (err) {
		LOG_WRN("Failed start_complete for conn: %p, err: %d", (void *)conn, err);
	}

	LOG_DBG("Unicast start complete cb");
	ret = k_msgq_peek(&cap_start_q, &conn);
	if (ret == 0) {
		/* Pending start procedure found, call k_work */
		k_work_submit(&cap_start_work);
	} else {
		LOG_DBG("No outstanding work");
	}
}

static void unicast_update_complete_cb(int err, struct bt_conn *conn)
{
	if (err) {
		LOG_WRN("Failed update_complete for conn: %p, err: %d", (void *)conn, err);
	}

	LOG_DBG("Unicast update complete cb");
}

static void unicast_stop_complete_cb(int err, struct bt_conn *conn)
{
	// int ret;

	if (err) {
		LOG_WRN("Failed stop_complete for conn: %p, err: %d", (void *)conn, err);
	}

	LOG_DBG("Unicast stop complete cb");

	/* Check for reconfigurable sink streams */
	// for (int i = 0; i < CONFIG_BT_ISO_MAX_CIG; i++) {
	// 	for (int j = 0; j < ARRAY_SIZE(unicast_servers[i][LVL2]); j++) {
	// 		if (unicast_servers[i][LVL2][j].qos_reconfigure && playing_state) {
	// 			struct stream_index idx = {
	// 				.lvl1 = i,
	// 				.lvl2 = 0,
	// 				.lvl3 = j,
	// 			};
	// 			ret = k_msgq_put(&cap_start_q, &idx, K_NO_WAIT);
	// 			if (ret) {
	// 				LOG_ERR("Failed to put device_index %d on the "
	// 					"queue: %d",
	// 					j, ret);
	// 			}
	// 			k_work_submit(&cap_start_work);
	// 		}
	// 	}
	// }
}

static struct bt_cap_initiator_cb cap_cbs = {
	.unicast_discovery_complete = unicast_discovery_complete_cb,
	.unicast_start_complete = unicast_start_complete_cb,
	.unicast_update_complete = unicast_update_complete_cb,
	.unicast_stop_complete = unicast_stop_complete_cb,
};

int le_audio_concurrent_sync_num_get(void)
{
	return 1; /* Only one stream supported at the moment */
}

int unicast_client_config_get(struct bt_conn *conn, enum bt_audio_dir dir, uint32_t *bitrate,
			      uint32_t *sampling_rate_hz)
{
	int ret;
	// struct stream_index idx;

	if (conn == NULL) {
		LOG_ERR("No valid connection pointer received");
		return -EINVAL;
	}

	if (bitrate == NULL && sampling_rate_hz == NULL) {
		LOG_ERR("No valid pointers received");
		return -ENXIO;
	}

	struct server_store *server = NULL;
	ret = srv_store_from_conn_get(conn, &server);
	if (ret) {
		LOG_ERR("Unknown connection, should not reach here");
		return ret;
	}

	if (dir == BT_AUDIO_DIR_SINK) {
		if (server->snk.cap_streams[0].bap_stream.codec_cfg == NULL) {
			LOG_ERR("No codec found for the stream");

			return -ENXIO;
		}

		if (sampling_rate_hz != NULL) {
			ret = le_audio_freq_hz_get(server->snk.cap_streams[0].bap_stream.codec_cfg,
						   sampling_rate_hz);
			if (ret) {
				LOG_ERR("Invalid sampling frequency: %d", ret);
				return -ENXIO;
			}
		}

		if (bitrate != NULL) {
			ret = le_audio_bitrate_get(server->snk.cap_streams[0].bap_stream.codec_cfg,
						   bitrate);
			if (ret) {
				LOG_ERR("Unable to calculate bitrate: %d", ret);
				return -ENXIO;
			}
		}
	} else if (dir == BT_AUDIO_DIR_SOURCE) {
		if (server->src.cap_streams[0].bap_stream.codec_cfg == NULL) {
			LOG_ERR("No codec found for the stream");
			return -ENXIO;
		}

		if (sampling_rate_hz != NULL) {
			ret = le_audio_freq_hz_get(server->src.cap_streams[0].bap_stream.codec_cfg,
						   sampling_rate_hz);
			if (ret) {
				LOG_ERR("Invalid sampling frequency: %d", ret);
				return -ENXIO;
			}
		}

		if (bitrate != NULL) {
			ret = le_audio_bitrate_get(server->src.cap_streams[0].bap_stream.codec_cfg,
						   bitrate);
			if (ret) {
				LOG_ERR("Unable to calculate bitrate: %d", ret);
				return -ENXIO;
			}
		}
	}

	return 0;
}

void unicast_client_conn_disconnected(struct bt_conn *conn)
{
	/* Do nothing for now */
}

int unicast_client_discover(struct bt_conn *conn, enum unicast_discover_dir dir)
{
	int ret;

	struct server_store *server = NULL;

	ret = srv_store_add(conn);
	if (ret == -EALREADY) {
		LOG_INF("Server store already exists for conn: %p", (void *)conn);
		ret = srv_store_from_conn_get(conn, &server);
		if (ret) {
			LOG_ERR("Unknown connection, should not reach here");
			return ret;
		}

		server->snk.num_eps = 0;
		server->src.num_eps = 0;
		memset(&server->snk.lc3_preset, 0, sizeof(server->snk.lc3_preset));
		memset(&server->src.lc3_preset, 0, sizeof(server->src.lc3_preset));
		server->snk.eps[0] = NULL;
		server->src.eps[0] = NULL;
		server->snk.waiting_for_disc = false;
		server->src.waiting_for_disc = false;
		server->snk.locations = 0;
		server->src.locations = 0;
		server->snk.num_codec_caps = 0;
		server->src.num_codec_caps = 0;

	} else if (ret) {
		LOG_ERR("Failed to add server store for conn: %p, err: %d", (void *)conn, ret);
		return ret;
	} else {

		ret = srv_store_from_conn_get(conn, &server);
		if (ret) {
			LOG_ERR("Unknown connection, should not reach here");
			return ret;
		}

		/* Register ops */
		for (int i = 0; i < CONFIG_BT_BAP_UNICAST_CLIENT_ASE_SNK_COUNT; i++) {
			bt_cap_stream_ops_register(&server->snk.cap_streams[i], &stream_ops);
		}

		for (int i = 0; i < CONFIG_BT_BAP_UNICAST_CLIENT_ASE_SRC_COUNT; i++) {
			bt_cap_stream_ops_register(&server->src.cap_streams[i], &stream_ops);
		}
	}

	ret = bt_cap_initiator_unicast_discover(conn);
	if (ret) {
		LOG_WRN("Failed to start cap discover: %d", ret);
		return ret;
	}

	if (dir & BT_AUDIO_DIR_SOURCE) {
		server->src.waiting_for_disc = true;
	}

	if (dir & BT_AUDIO_DIR_SINK) {
		server->snk.waiting_for_disc = true;
	}

	if (dir == UNICAST_SERVER_BIDIR) {
		/* If we need to discover both source and sink, do sink first */
		ret = bt_bap_unicast_client_discover(conn, BT_AUDIO_DIR_SINK);
		return ret;
	}

	ret = bt_bap_unicast_client_discover(conn, dir);
	return ret;
}

int unicast_client_start(uint8_t cig_index)
{
	int ret;

	ret = k_mutex_lock(&mtx_cap_procedure_proceed, CAP_PROCED_MUTEX_WAIT_TIME_MS);
	if (ret == -EAGAIN) {
		LOG_ERR("CAP procedure lock timeout");
	}

	/* Start all unicast_servers with valid endpoints */
	struct bt_cap_unicast_audio_start_stream_param
		cap_stream_params[CONFIG_BT_BAP_UNICAST_CLIENT_ASE_SNK_COUNT +
				  CONFIG_BT_BAP_UNICAST_CLIENT_ASE_SRC_COUNT];

	struct bt_cap_unicast_audio_start_param param;

	param.stream_params = cap_stream_params;
	param.count = 0;
	param.type = BT_CAP_SET_TYPE_AD_HOC;

	uint8_t num_servers = srv_store_num_get();

	for (int i = 0; i < num_servers; i++) {
		uint8_t state;
		struct server_store *server = NULL;

		ret = srv_store_server_get(&server, i);

		if (server->snk.num_eps > 0) {
			for (int j = 0; j < server->snk.num_eps; j++) {
				ret = le_audio_ep_state_get(server->snk.eps[j], &state);
				if (state == BT_BAP_EP_STATE_STREAMING) {
					LOG_DBG("Sink endpoint is already streaming, "
						"skipping "
						"start");
					continue;
				}

				LOG_WRN("Putting stream %d (%p) from i: %d in the queue", j,
					(void *)&server->snk.cap_streams[j], i);

				cap_stream_params[param.count].member.member = server->conn;
				cap_stream_params[param.count].stream = &server->snk.cap_streams[j];
				cap_stream_params[param.count].ep = server->snk.eps[j];
				cap_stream_params[param.count].codec_cfg =
					&server->snk.lc3_preset[j].codec_cfg;
				param.count++;
			}
		}

		if (server->src.num_eps > 0) {
			for (int j = 0; j < server->src.num_eps; j++) {
				ret = le_audio_ep_state_get(server->src.eps[j], &state);
				if (state == BT_BAP_EP_STATE_STREAMING) {
					LOG_DBG("Source endpoint is already streaming, "
						"skipping "
						"start");
					continue;
				}

				cap_stream_params[param.count].member.member = server->conn;
				cap_stream_params[param.count].stream = &server->src.cap_streams[j];
				cap_stream_params[param.count].ep = server->src.eps[j];
				cap_stream_params[param.count].codec_cfg =
					&server->src.lc3_preset[0].codec_cfg;
				param.count++;
			}
		}
	}

	if (param.count == 0) {
		LOG_DBG("No streams to start");
		k_mutex_unlock(&mtx_cap_procedure_proceed);
		return -EIO;
	}

	ret = bt_cap_initiator_unicast_audio_start(&param);
	if (ret == -EBUSY) {
		/* Try again once the ongoing start procedure is completed */
		ret = k_msgq_put(&cap_start_q, NULL, K_NO_WAIT);
		if (ret) {
			LOG_ERR("Failed to put device_index on the queue: %d", ret);
		}
	} else if (ret) {
		LOG_ERR("Failed to start unicast sink audio: %d", ret);
	}

	playing_state = true;

	return 0;
}

int unicast_client_stop(uint8_t cig_index)
{
	int ret;
	struct bt_cap_stream *streams[(CONFIG_BT_BAP_UNICAST_CLIENT_ASE_SRC_COUNT +
				       CONFIG_BT_BAP_UNICAST_CLIENT_ASE_SNK_COUNT) *
				      CONFIG_BT_MAX_CONN];
	static struct bt_cap_unicast_audio_stop_param param;

	if (cig_index >= CONFIG_BT_ISO_MAX_CIG) {
		LOG_ERR("Trying to stop CIG %d out of %d", cig_index, CONFIG_BT_ISO_MAX_CIG);
		return -EINVAL;
	}

	param.streams = streams;
	param.count = 0;
	param.type = BT_CAP_SET_TYPE_AD_HOC;
	param.release = true;

	le_audio_event_publish(LE_AUDIO_EVT_NOT_STREAMING, NULL, 0);

	/* TODO: Find all server_store server sink and source endpoints for a given cig and check
	 * state. If the endpoint is in a streaming state, add it to the stop list
	 */

	uint8_t num_servers = srv_store_num_get();

	for (int i = 0; i < num_servers; i++) {
		uint8_t state;
		struct server_store *server = NULL;

		ret = srv_store_server_get(&server, i);

		if (server->snk.num_eps > 0) {
			for (int j = 0; j < server->snk.num_eps; j++) {
				ret = le_audio_ep_state_get(server->snk.eps[j], &state);
				if (state != BT_BAP_EP_STATE_STREAMING) {
					LOG_DBG("Sink endpoint is not streaming, skipping stop");
					continue;
				}

				streams[param.count++] = &server->snk.cap_streams[j];
			}
		}

		if (server->src.num_eps > 0) {
			for (int j = 0; j < server->src.num_eps; j++) {
				ret = le_audio_ep_state_get(server->src.eps[j], &state);
				if (state != BT_BAP_EP_STATE_STREAMING) {
					LOG_DBG("Source endpoint is not streaming, skipping stop");
					continue;
				}

				streams[param.count++] = &server->src.cap_streams[j];
			}
		}
	}

	if (param.count > 0) {
		ret = bt_cap_initiator_unicast_audio_stop(&param);
		if (ret) {
			LOG_ERR("Failed to stop unicast audio: %d", ret);
			return ret;
		}

		playing_state = false;
	} else {
		return -EIO;
	}

	return 0;
}

int unicast_client_send(struct net_buf const *const audio_frame, uint8_t cig_index)
{
#if (CONFIG_BT_AUDIO_TX)
	int ret;
	uint8_t num_active_streams = 0;

	if (cig_index >= CONFIG_BT_ISO_MAX_CIG) {
		LOG_ERR("Trying to send to CIG %d out of %d", cig_index, CONFIG_BT_ISO_MAX_CIG);
		return -EINVAL;
	}

	uint8_t num_streaming =
		srv_store_all_ep_state_count(BT_BAP_EP_STATE_STREAMING, BT_AUDIO_DIR_SINK);

	struct le_audio_tx_info tx[num_streaming];

	for (int i = 0; i < srv_store_num_get(); i++) {
		/* Skip unicast_servers not in a streaming state */
		struct server_store *server = NULL;
		ret = srv_store_server_get(&server, i);
		for (int j = 0; j < server->snk.num_eps; j++) {
			if (!le_audio_ep_state_check(server->snk.cap_streams[j].bap_stream.ep,
						     BT_BAP_EP_STATE_STREAMING)) {
				continue;
			}

			/* Set cap stream pointer */
			tx[num_active_streams].cap_stream = &server->snk.cap_streams[j];

			/* Set index */
			tx[num_active_streams].idx.lvl1 = cig_index;
			tx[num_active_streams].idx.lvl2 = LVL2;

			const uint8_t *loc;

			bt_audio_codec_cfg_get_val(server->snk.cap_streams[j].bap_stream.codec_cfg,
						   BT_AUDIO_CODEC_CFG_CHAN_ALLOC, &loc);

			if (*loc & BT_AUDIO_LOCATION_FRONT_LEFT) {
				tx[num_active_streams].idx.lvl3 = 0;
			} else if (*loc & BT_AUDIO_LOCATION_FRONT_RIGHT) {
				tx[num_active_streams].idx.lvl3 = 1;
			} else {
				LOG_ERR("Unknown channel location: %d", *loc);
				return -EIO;
			}

			/* Set channel location */
			/* Both mono and left unicast_servers will receive left channel */

			tx[num_active_streams].audio_channel =
				*loc == BT_AUDIO_LOCATION_FRONT_RIGHT ? AUDIO_CH_R : AUDIO_CH_L;

			num_active_streams++;
		}
	}

	if (num_active_streams == 0) {
		LOG_WRN("No active streams");
		return -ECANCELED;
	}

	ret = bt_le_audio_tx_send(audio_frame, tx, num_active_streams);
	if (ret) {
		return ret;
	}
#endif /* (CONFIG_BT_AUDIO_TX) */

	return 0;
}

int unicast_client_disable(uint8_t cig_index)
{
	ARG_UNUSED(cig_index);

	return -ENOTSUP;
}

int unicast_client_enable(uint8_t cig_index, le_audio_receive_cb recv_cb)
{
	int ret;
	static bool initialized;

	if (initialized) {
		LOG_WRN("Already initialized");
		return -EALREADY;
	}

	ret = srv_store_init();
	if (ret) {
		return ret;
	}

	if (recv_cb == NULL) {
		LOG_ERR("Receive callback is NULL");
		return -EINVAL;
	}

	receive_cb = recv_cb;

	ret = bt_bap_unicast_client_register_cb(&unicast_client_cbs);
	if (ret) {
		LOG_ERR("Failed to register client callbacks: %d", ret);
		return ret;
	}

	ret = bt_cap_initiator_register_cb(&cap_cbs);
	if (ret) {
		LOG_ERR("Failed to register cap callbacks: %d", ret);
		return ret;
	}

	if (IS_ENABLED(CONFIG_BT_AUDIO_TX)) {
		bt_le_audio_tx_init();
	}

	initialized = true;

	return 0;
}

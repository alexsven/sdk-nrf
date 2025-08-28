/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/ztest.h>
#include <zephyr/ztest_error_hook.h>
#include <zephyr/tc_util.h>

#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/audio/audio.h>
#include <zephyr/bluetooth/audio/bap.h>
#include <../subsys/bluetooth/audio/bap_endpoint.h>
#include <../subsys/bluetooth/audio/bap_iso.h>
#include <../subsys/bluetooth/host/conn_internal.h>
#include "server_store.h"

#define TEST_CAP_STREAM(name)                                                                      \
	struct bt_cap_stream test_##name##_cap_stream;                                             \
	struct bt_bap_ep test_##name##_ep_var = {0};                                               \
	struct bt_iso_chan test_##name##_bap_iso = {0};                                            \
	struct bt_bap_qos_cfg test_##name##_qos = {0};                                             \
	test_##name##_qos.pd = UINT32_MAX;                                                         \
	test_##name##_ep_var.dir = 1;                                                              \
	test_##name##_cap_stream.bap_stream.ep = &test_##name##_ep_var;                            \
	test_##name##_cap_stream.bap_stream.iso = &test_##name##_bap_iso;                          \
	test_##name##_cap_stream.bap_stream.group = (void *)0;                                     \
	test_##name##_cap_stream.bap_stream.qos = &test_##name##_qos;

#define TEST_CONN(val)                                                                             \
	struct bt_conn test_##val##_conn = {                                                       \
		.handle = val,                                                                     \
		.type = BT_CONN_TYPE_LE,                                                           \
		.id = val,                                                                         \
		.state = BT_CONN_STATE_CONNECTED,                                                  \
		.le = {.dst = {.type = BT_ADDR_LE_PUBLIC, .a = {{val, 0, 0, 0, 0, 0}}}}};

ZTEST(suite_server_store, test_srv_store_init)
{
	int ret;

	ret = srv_store_init();
	zassert_equal(ret, 0, "Init did not return zero");

	ret = srv_store_num_get(true);
	zassert_equal(ret, 0, "Number of servers should be zero after init");

	TEST_CONN(1);

	ret = srv_store_add(&test_1_conn);
	zassert_equal(ret, 0, "Adding server did not return zero");

	ret = srv_store_num_get(true);
	zassert_equal(ret, 1, "Number of servers should be one after adding a server");

	ret = srv_store_remove_all();
	zassert_equal(ret, 0, "Clearing all servers did not return zero");

	ret = srv_store_num_get(true);
	zassert_equal(ret, 0, "Number of servers should be zero after clearing");

	srv_store_unlock();
}

ZTEST(suite_server_store, test_srv_store_multiple)
{
	int ret;

	ret = srv_store_init();
	zassert_equal(ret, 0, "Init did not return zero");

	TEST_CONN(101);
	TEST_CONN(102);
	TEST_CONN(103);
	TEST_CONN(104);

	ret = srv_store_add(&test_101_conn);
	zassert_equal(ret, 0, "Adding server did not return zero");

	ret = srv_store_add(&test_102_conn);
	zassert_equal(ret, 0, "Adding server did not return zero");

	ret = srv_store_add(&test_103_conn);
	zassert_equal(ret, 0, "Adding server did not return zero");

	ret = srv_store_num_get(true);
	zassert_equal(ret, 3, "Number of servers should be three after adding three servers");

	struct server_store *retr_server = NULL;

	ret = srv_store_from_conn_get(&test_102_conn, &retr_server);
	zassert_equal(ret, 0, "Retrieving server by connection did not return zero");
	zassert_not_null(retr_server, "Retrieved server should not be NULL");
	zassert_equal(retr_server->conn, &test_102_conn,
		      "Retrieved server connection does not match expected");

	ret = srv_store_from_conn_get(&test_104_conn, &retr_server);
	zassert_equal(ret, -ENOENT, "Retrieving non-existing server should return -ENOENT");
	zassert_is_null(retr_server, "Retrieved server should be NULL for non-existing entry");

	srv_store_unlock();
}

ZTEST(suite_server_store, test_srv_store_pointer_check)
{
	int ret;

	Z_TEST_SKIP_IFNDEF(true);

	ret = srv_store_init();
	zassert_equal(ret, 0, "Init did not return zero");

	TEST_CONN(101);
	TEST_CONN(102);
	TEST_CONN(103);

	struct server_store *retr_server1 = NULL;
	struct server_store *retr_server2 = NULL;
	struct server_store *retr_server3 = NULL;

	ret = srv_store_add(&test_102_conn);
	zassert_equal(ret, 0);
	ret = srv_store_from_conn_get(&test_102_conn, &retr_server2);
	zassert_equal(ret, 0);
	retr_server2->snk.num_codec_caps = 2;
	TC_PRINT("value %d stored_ at %p (%p)\n", retr_server2->snk.num_codec_caps,
		 &retr_server2->snk.num_codec_caps, retr_server2);

	ret = srv_store_add(&test_101_conn);
	zassert_equal(ret, 0);
	ret = srv_store_from_conn_get(&test_102_conn, &retr_server2);
	zassert_equal(ret, 0);
	TC_PRINT("value %d fetched at %p (%p)\n", retr_server2->snk.num_codec_caps,
		 &retr_server2->snk.num_codec_caps, retr_server2);
	ret = srv_store_from_conn_get(&test_101_conn, &retr_server1);
	zassert_equal(ret, 0);
	retr_server1->snk.num_codec_caps = 1;
	TC_PRINT("value %d stored_ at %p (%p)\n", retr_server1->snk.num_codec_caps,
		 &retr_server1->snk.num_codec_caps, retr_server1);

	ret = srv_store_add(&test_103_conn);
	zassert_equal(ret, 0);
	ret = srv_store_from_conn_get(&test_103_conn, &retr_server3);
	zassert_equal(ret, 0);
	retr_server3->snk.num_codec_caps = 3;
	TC_PRINT("value %d stored_ at %p (%p)\n", retr_server3->snk.num_codec_caps,
		 &retr_server3->snk.num_codec_caps, retr_server3);

	ret = srv_store_from_conn_get(&test_101_conn, &retr_server1);
	zassert_equal(ret, 0);
	TC_PRINT("value %d fetched at %p (%p)\n", retr_server1->snk.num_codec_caps,
		 &retr_server1->snk.num_codec_caps, retr_server1);
	ret = srv_store_from_conn_get(&test_103_conn, &retr_server3);
	zassert_equal(ret, 0);
	TC_PRINT("value %d stored_ at %p (%p)\n", retr_server3->snk.num_codec_caps,
		 &retr_server3->snk.num_codec_caps, retr_server3);

	ret = srv_store_from_conn_get(&test_101_conn, &retr_server1);
	TC_PRINT("value %d fetched at %p (%p)\n", retr_server1->snk.num_codec_caps,
		 &retr_server1->snk.num_codec_caps, retr_server1);

	ret = srv_store_from_conn_get(&test_102_conn, &retr_server2);
	TC_PRINT("value %d fetched at %p (%p)\n", retr_server2->snk.num_codec_caps,
		 &retr_server2->snk.num_codec_caps, retr_server2);

	ret = srv_store_from_conn_get(&test_103_conn, &retr_server3);
	TC_PRINT("value %d fetched at %p (%p)\n", retr_server3->snk.num_codec_caps,
		 &retr_server3->snk.num_codec_caps, retr_server3);

	srv_store_unlock();
}

ZTEST(suite_server_store, test_srv_remove)
{
	int ret;

	ret = srv_store_init();
	zassert_equal(ret, 0, "Init did not return zero");

	TEST_CONN(100);
	TEST_CONN(101);
	TEST_CONN(102);

	ret = srv_store_add(&test_100_conn);
	zassert_equal(ret, 0, "Adding server did not return zero");

	ret = srv_store_add(&test_101_conn);
	zassert_equal(ret, 0, "Adding server did not return zero");

	ret = srv_store_add(&test_102_conn);
	zassert_equal(ret, 0, "Adding server did not return zero");

	ret = srv_store_num_get(true);
	zassert_equal(ret, 3, "Number of servers should be three after adding three servers");

	ret = srv_store_remove(&test_102_conn);
	zassert_equal(ret, 0, "Removing server by connection did not return zero");

	ret = srv_store_num_get(true);
	zassert_equal(ret, 2, "Number of servers should be two after removing one");

	ret = srv_store_remove(&test_100_conn);
	zassert_equal(ret, 0, "Removing server by connection did not return zero");

	/* Test with creating a gap in server store. */
	ret = srv_store_num_get(true);
	zassert_equal(ret, -EINVAL, "Number of servers should be two after removing one");

	ret = srv_store_num_get(false);
	zassert_equal(ret, 1, "Number of servers should be two after removing one");

	srv_store_unlock();
}

ZTEST(suite_server_store, test_find_srv_from_stream)
{
	int ret;

	ret = srv_store_init();
	zassert_equal(ret, 0, "Init did not return zero");

	TEST_CONN(100);
	TEST_CONN(101);

	ret = srv_store_add(&test_100_conn);
	zassert_equal(ret, 0, "Adding server did not return zero");

	ret = srv_store_add(&test_101_conn);
	zassert_equal(ret, 0, "Adding server did not return zero");

	struct server_store *retr_server = NULL;

	ret = srv_store_from_conn_get(&test_100_conn, &retr_server);
	zassert_equal(ret, 0, "Retrieving OK server by connection did not return zero");

	retr_server->name = "Test Server 100";
	TEST_CAP_STREAM(one);
	memcpy(&retr_server->snk.cap_streams[0], &test_one_cap_stream, sizeof(test_one_cap_stream));
	TEST_CAP_STREAM(two);
	memcpy(&retr_server->snk.cap_streams[1], &test_two_cap_stream, sizeof(test_two_cap_stream));
	TEST_CAP_STREAM(three);
	memcpy(&retr_server->snk.cap_streams[2], &test_three_cap_stream,
	       sizeof(test_three_cap_stream));

	ret = srv_store_from_conn_get(&test_101_conn, &retr_server);
	zassert_equal(ret, 0, "Retrieving OK server by connection did not return zero");

	retr_server->name = "Test Server 101";
	TEST_CAP_STREAM(four);
	memcpy(&retr_server->snk.cap_streams[0], &test_four_cap_stream,
	       sizeof(test_four_cap_stream));
	TEST_CAP_STREAM(five);
	memcpy(&retr_server->snk.cap_streams[1], &test_five_cap_stream,
	       sizeof(test_five_cap_stream));
	TEST_CAP_STREAM(six);
	memcpy(&retr_server->snk.cap_streams[2], &test_six_cap_stream, sizeof(test_six_cap_stream));

	TEST_CAP_STREAM(seven);

	ret = srv_store_from_stream_get(&test_seven_cap_stream.bap_stream, &retr_server);
	zassert_equal(ret, -ENOENT, "Retrieving from non existing stream should return -ENOENT");
	zassert_is_null(retr_server, "Retrieved server should be NULL");

	TC_PRINT("test bap ptr %p\n", &test_two_cap_stream.bap_stream);

	ret = srv_store_from_stream_get(&test_five_cap_stream.bap_stream, &retr_server);
	zassert_equal(ret, 0, "Retrieving from stream should return zero %d", ret);
	zassert_not_null(retr_server, "Retrieved server should not be NULL");
	zassert_equal(retr_server->name, "Test Server 101");
	zassert_equal_ptr(retr_server->conn, &test_101_conn,
			  "Retrieved server connection does not match expected");
	/* Testing illegal operation with idential cap_stream pointers */

	memcpy(&retr_server->snk.cap_streams[3], &test_two_cap_stream, sizeof(test_two_cap_stream));

	ret = srv_store_from_stream_get(&test_two_cap_stream.bap_stream, &retr_server);
	zassert_equal(ret, -ESPIPE, "Retrieving from stream should return -ESPIPE %d", ret);

	srv_store_unlock();
}

ZTEST(suite_server_store, test_pres_dly_simple)
{
	int ret;

	ret = srv_store_init();
	zassert_equal(ret, 0, "Init did not return zero");

	TEST_CAP_STREAM(one);
	test_one_cap_stream.bap_stream.group = (void *)0xaaaa;

	struct bt_bap_qos_cfg_pref qos_cfg_pref_in;
	qos_cfg_pref_in.pd_min = 1000;
	qos_cfg_pref_in.pd_max = 4000;
	qos_cfg_pref_in.pref_pd_min = 2000;
	qos_cfg_pref_in.pref_pd_max = 3000;

	/* For this test, we have no other endpoints or streams stored
	 * This simulates getting the first call to find the presentation delay
	 */

	uint32_t computed_pres_dly_us = 0;
	uint32_t existing_pres_dly_us = 0;
	bool group_reconfig_needed = false;

	ret = srv_store_pres_dly_find(&test_one_cap_stream.bap_stream, &computed_pres_dly_us,
				      &existing_pres_dly_us, &qos_cfg_pref_in,
				      &group_reconfig_needed);

	zassert_equal(ret, 0, "Finding presentation delay did not return zero");
	zassert_equal(computed_pres_dly_us, 2000,
		      "Computed presentation delay should be equal to preferred min");

	/* Removing preferred min. Result should return min */
	qos_cfg_pref_in.pref_pd_min = 0;

	ret = srv_store_pres_dly_find(&test_one_cap_stream.bap_stream, &computed_pres_dly_us,
				      &existing_pres_dly_us, &qos_cfg_pref_in,
				      &group_reconfig_needed);
	zassert_equal(ret, 0, "Finding presentation delay did not return zero");
	zassert_equal(computed_pres_dly_us, 1000,
		      "Computed presentation delay should be equal to preferred min");

	/* Removing min, should return error*/
	qos_cfg_pref_in.pd_min = 0;
	ret = srv_store_pres_dly_find(&test_one_cap_stream.bap_stream, &computed_pres_dly_us,
				      &existing_pres_dly_us, &qos_cfg_pref_in,
				      &group_reconfig_needed);
	zassert_equal(ret, -EINVAL, "Finding presentation delay should return -EINVAL %d ", ret);

	/*TODO: Add test for existing_pres_dly_us*/
	srv_store_unlock();
}

ZTEST(suite_server_store, test_pres_delay_advanced)
{
	int ret;

	TEST_CONN(100);

	ret = srv_store_init();
	zassert_equal(ret, 0, "Init did not return zero");

	ret = srv_store_add(&test_100_conn);
	zassert_equal(ret, 0, "Adding server did not return zero");

	struct server_store *retr_server = NULL;

	ret = srv_store_from_conn_get(&test_100_conn, &retr_server);
	zassert_equal(ret, 0, "Retrieving server by connection did not return zero");
	zassert_not_null(retr_server, "Retrieved server should not be NULL");
	zassert_equal(retr_server->conn, &test_100_conn,
		      "Retrieved server connection does not match expected");

	TEST_CAP_STREAM(one);
	test_one_cap_stream.bap_stream.ep->qos_pref.pd_min = 1000;
	test_one_cap_stream.bap_stream.ep->qos_pref.pd_max = 4000;
	test_one_cap_stream.bap_stream.ep->qos_pref.pref_pd_min = 2000;
	test_one_cap_stream.bap_stream.ep->qos_pref.pref_pd_max = 3000;
	test_one_cap_stream.bap_stream.group = (void *)0xaaaa;
	test_one_cap_stream.bap_stream.qos->pd = 2500;

	memcpy(&retr_server->snk.cap_streams[0], &test_one_cap_stream, sizeof(test_one_cap_stream));

	TEST_CAP_STREAM(two);

	struct bt_bap_qos_cfg_pref qos_cfg_pref_in;

	qos_cfg_pref_in.pd_min = 1100;
	qos_cfg_pref_in.pd_max = 4000;
	qos_cfg_pref_in.pref_pd_min = 2100;
	qos_cfg_pref_in.pref_pd_max = 3000;

	uint32_t computed_pres_dly_us = 0;
	uint32_t existing_pres_dly_us = 0;
	bool group_reconfig_needed = false;

	ret = srv_store_pres_dly_find(&test_one_cap_stream.bap_stream, &computed_pres_dly_us,
				      &existing_pres_dly_us, &qos_cfg_pref_in,
				      &group_reconfig_needed);
	zassert_equal(ret, 0, "Finding presentation delay did not return zero %d", ret);
	zassert_equal(computed_pres_dly_us, 2500, "Presentation delay should be unchanged %d",
		      computed_pres_dly_us);
	zassert_equal(group_reconfig_needed, false, "Group reconfiguration should not be needed");

	/* Testing with pref outside existing PD. Should not change existing streams*/

	qos_cfg_pref_in.pref_pd_min = 2600;
	ret = srv_store_pres_dly_find(&test_one_cap_stream.bap_stream, &computed_pres_dly_us,
				      &existing_pres_dly_us, &qos_cfg_pref_in,
				      &group_reconfig_needed);
	zassert_equal(ret, 0, "Finding presentation delay did not return zero %d", ret);
	zassert_equal(computed_pres_dly_us, 2500, "Presentation delay should be unchanged %d",
		      computed_pres_dly_us);
	zassert_equal(group_reconfig_needed, false, "Group reconfiguration should not be needed");

	/* Now check with min outside range. Shall trigger a reconfig*/
	qos_cfg_pref_in.pd_min = 2600;
	ret = srv_store_pres_dly_find(&test_one_cap_stream.bap_stream, &computed_pres_dly_us,
				      &existing_pres_dly_us, &qos_cfg_pref_in,
				      &group_reconfig_needed);
	zassert_equal(ret, 0, "Finding presentation delay did not return zero %d", ret);
	zassert_equal(computed_pres_dly_us, 2600, "Presentation delay should be unchanged %d",
		      computed_pres_dly_us);
	zassert_equal(group_reconfig_needed, true, "Group reconfiguration should not be needed");
	srv_store_unlock();
}

ZTEST(suite_server_store, test_pres_delay_multi_group)
{
	int ret;

	TEST_CONN(100);
	TEST_CONN(101);

	ret = srv_store_init();
	zassert_equal(ret, 0, "Init did not return zero");

	ret = srv_store_add(&test_100_conn);
	zassert_equal(ret, 0, "Adding server did not return zero");

	ret = srv_store_add(&test_101_conn);
	zassert_equal(ret, 0, "Adding server did not return zero");

	struct server_store *retr_server = NULL;

	ret = srv_store_from_conn_get(&test_100_conn, &retr_server);
	zassert_equal(ret, 0, "Retrieving server by connection did not return zero");
	zassert_not_null(retr_server, "Retrieved server should not be NULL");
	zassert_equal(retr_server->conn, &test_100_conn,
		      "Retrieved server connection does not match expected");

	TEST_CAP_STREAM(one);
	test_one_cap_stream.bap_stream.group = (void *)0xaaaa;
	test_one_cap_stream.bap_stream.qos->pd = 2000;

	memcpy(&retr_server->snk.cap_streams[0], &test_one_cap_stream, sizeof(test_one_cap_stream));

	/* Add stream in another group. Should be ignored */
	TEST_CAP_STREAM(two);
	test_two_cap_stream.bap_stream.group = (void *)0xbbbb;
	test_two_cap_stream.bap_stream.qos->pd = 500;

	memcpy(&retr_server->snk.cap_streams[1], &test_two_cap_stream, sizeof(test_two_cap_stream));

	struct bt_bap_qos_cfg_pref qos_cfg_pref_in;

	qos_cfg_pref_in.pd_min = 1100;
	qos_cfg_pref_in.pd_max = 4000;
	qos_cfg_pref_in.pref_pd_min = 2100;
	qos_cfg_pref_in.pref_pd_max = 3000;

	uint32_t computed_pres_dly_us = 0;
	uint32_t existing_pres_dly_us = 0;
	bool group_reconfig_needed = false;

	ret = srv_store_pres_dly_find(&test_one_cap_stream.bap_stream, &computed_pres_dly_us,
				      &existing_pres_dly_us, &qos_cfg_pref_in,
				      &group_reconfig_needed);
	zassert_equal(ret, 0, "Finding presentation delay did not return zero %d", ret);
	zassert_equal(computed_pres_dly_us, 2000, "Presentation delay should be unchanged %d",
		      computed_pres_dly_us);
	zassert_equal(group_reconfig_needed, false, "Group reconfiguration should not be needed");
	srv_store_unlock();
}

ZTEST(suite_server_store, test_cap_set)
{
	int ret;

	TEST_CONN(1);

	ret = srv_store_init();
	zassert_equal(ret, 0, "Init did not return zero");

	ret = srv_store_add(&test_1_conn);
	zassert_equal(ret, 0, "Adding server did not return zero");

	struct bt_audio_codec_cap codec;

	codec.id = 0xaa;
	codec.data_len = 10;

	ret = srv_store_codec_cap_set(&test_1_conn, BT_AUDIO_DIR_SINK, &codec);
	zassert_equal(ret, 0, "Setting codec capabilities did not return zero %d", ret);

	srv_store_unlock();
}

ZTEST(suite_server_store, test_srv_get)
{
	int ret;

	TEST_CONN(100);
	TEST_CONN(101);

	ret = srv_store_add(&test_100_conn);
	zassert_equal(ret, 0, "Adding server did not return zero");

	ret = srv_store_add(&test_101_conn);
	zassert_equal(ret, 0, "Adding server did not return zero");

	struct server_store *server;

	ret = srv_store_server_get(&server, 0);
	zassert_equal(ret, 0, "Adding server did not return zero");
	zassert_not_null(server, "Retrieved server should not be NULL");
	zassert_equal_ptr(server->conn, &test_100_conn,
			  "Retrieved server connection does not match expected");

	ret = srv_store_server_get(&server, 1);
	zassert_equal(ret, 0, "Adding server did not return zero");
	zassert_not_null(server, "Retrieved server should not be NULL");
	zassert_equal_ptr(server->conn, &test_101_conn,
			  "Retrieved server connection does not match expected");

	ret = srv_store_server_get(&server, 2);
	zassert_equal(ret, -ENOENT, "Adding server did not return zero %d", ret);

	srv_store_unlock();
}

void before_fn(void *dummy)
{
	int ret;

	ret = srv_store_lock(K_NO_WAIT);
	zassert_equal(ret, 0);

	ret = srv_store_init();
	zassert_equal(ret, 0, "Init did not return zero");
}

/* Test that calling functions without lock triggers assertions */
ZTEST(suite_server_store, test_assert_no_lock)
{
	/* First unlock to simulate not having the lock */
	srv_store_unlock();

	/* Enable assert catching to prevent actual crash */
	ztest_set_assert_valid(true);

	/* This should trigger the assertion in valid_entry_check() */
	int ret = srv_store_num_get(true);

	/* Disable assert catching */
	ztest_set_assert_valid(false);

	/* Re-acquire lock for cleanup */
	ret = srv_store_lock(K_NO_WAIT);
	zassert_equal(ret, 0, "Should be able to re-acquire lock");
}

ZTEST_SUITE(suite_server_store, NULL, NULL, before_fn, NULL, NULL);

#!/usr/bin/env bash
# Copyright 2023 Nordic Semiconductor ASA
# SPDX-License-Identifier: Apache-2.0

# Compile all the applications needed by the bsim tests in these subfolders

#set -x #uncomment this line for debugging
BOARD=nrf5340bsim/nrf5340/cpuapp
set -ue

: "${ZEPHYR_BASE:?ZEPHYR_BASE must be set to point to the zephyr root directory}"

source ${ZEPHYR_BASE}/tests/bsim/compile.source

app=${ZEPHYR_NRF_MODULE_DIR}samples/bluetooth/broadcast_config_tool conf_overlay=overlay-nrf5340_bsim.conf sysbuild=1 compile 
app=${ZEPHYR_NRF_MODULE_DIR}tests/bluetooth/bsim/broadcast_config_tool sysbuild=1 compile

wait_for_background_jobs

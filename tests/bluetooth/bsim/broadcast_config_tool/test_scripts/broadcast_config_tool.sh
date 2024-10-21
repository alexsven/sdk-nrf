#!/usr/bin/env bash
# Copyright 2023 Nordic Semiconductor ASA
# SPDX-License-Identifier: Apache-2.0

simulation_id="broadcast_config_tool_samples_test"
verbosity_level=2

source ${ZEPHYR_BASE}/tests/bsim/sh_common.source

EXECUTE_TIMEOUT=200

cd ${BSIM_OUT_PATH}/bin

Execute ./bs_nrf5340bsim_nrf5340_cpuapp____nrf_samples_bluetooth_broadcast_config_tool_prj_conf_overlay-nrf5340_bsim_conf \
  -v=${verbosity_level} -s=${simulation_id} -d=0 -RealEncryption=1

Execute ./bs_nrf5340bsim_nrf5340_cpuapp____nrf_tests_bluetooth_bsim_broadcast_config_tool_prj_conf \
  -v=${verbosity_level} -s=${simulation_id} -d=1 -RealEncryption=1 \
  -testid=broadcast_config_tool

Execute ./bs_2G4_phy_v1 -v=${verbosity_level} -s=${simulation_id} \
  -D=2 -sim_length=10e6 $@

wait_for_background_jobs #Wait for all programs in background and return != 0 if any fails

#
# Copyright (c) 2026 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_broadcast_sink)

set(audio_app ${ZEPHYR_NRF_MODULE_DIR}/applications/nrf_audio)
target_sources(app PRIVATE
  src/main.c
  ${audio_app}/src/bluetooth/bt_stream/le_audio.c
  ${ZEPHYR_BASE}/subsys/bluetooth/audio/bap_base.c
  ${ZEPHYR_BASE}/subsys/bluetooth/audio/codec.c
  ${ZEPHYR_BASE}/subsys/bluetooth/audio/audio.c
  ${ZEPHYR_BASE}/subsys/bluetooth/host/uuid.c
  ${ZEPHYR_NRF_MODULE_DIR}/tests/nrf_audio/fakes/zbus_fakes/zbus_fakes.c
)
target_include_directories(app PRIVATE
  ${audio_app}/include
  ${audio_app}/src/bluetooth/bt_management
  ${audio_app}/src/bluetooth/bt_stream
  ${audio_app}/src/bluetooth/bt_stream/broadcast
  ${audio_app}/src/bluetooth/bt_stream/bt_le_audio_tx
  ${audio_app}/src/modules
  ${audio_app}/src/utils
  ${audio_app}/src/utils/macros
  ${ZEPHYR_NRF_MODULE_DIR}/tests/nrf_audio/fakes
  ${ZEPHYR_NRF_MODULE_DIR}/include
)

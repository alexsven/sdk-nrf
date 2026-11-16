#
# Copyright (c) 2026 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(test_multichannel_auracast)

set(audio_app ${ZEPHYR_NRF_MODULE_DIR}/applications/nrf_audio)
target_sources(app PRIVATE
  ${audio_app}/multichannel_auracast/programmes.c
  ${audio_app}/src/audio/sw_codec_select.c
  src/codec_test.c
  src/programmes_test.c
  src/usb_test.c
)
target_include_directories(app PRIVATE
  src
  ${audio_app}/multichannel_auracast
  ${audio_app}/src/modules
  ${audio_app}/src/audio
  ${audio_app}/src/utils
  ${audio_app}/src/utils/macros
  ${ZEPHYR_NRF_MODULE_DIR}/include
  ${ZEPHYR_NRFXLIB_MODULE_DIR}/lc3/include
)

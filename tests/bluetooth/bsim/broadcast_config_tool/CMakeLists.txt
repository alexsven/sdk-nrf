# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(broadcast_config_tool_test)

target_sources(app PRIVATE
  src/broadcast_config_tool_test.c
  src/test_main.c
)

zephyr_library_include_directories(app PRIVATE
    ${ZEPHYR_NRF_MODULE_DIR}/tests/bluetooth/bsim/broadcast_config_tool/include)

zephyr_include_directories(
  ${BSIM_COMPONENTS_PATH}/libUtilv1/src/
  ${BSIM_COMPONENTS_PATH}/libPhyComv1/src/
  )

#
# Copyright (c) 2023 Nordic Semiconductor
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

zephyr_library_include_directories(
    broadcast
    unicast_server
)

if (CONFIG_BT_BAP_BROADCAST_SOURCE)
	target_sources(app PRIVATE
			${CMAKE_CURRENT_SOURCE_DIR}/broadcast/broadcast_source.c)
endif()

if (CONFIG_TRANSPORT_CIS AND (CONFIG_AUDIO_DEV MATCHES HEADSET))
	target_sources(app PRIVATE
			${CMAKE_CURRENT_SOURCE_DIR}/unicast_server/unicast_server.c)
endif()


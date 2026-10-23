#
# Copyright (c) 2026 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

if(CONFIG_MODEM_NRF91SM)
	zephyr_library()
	zephyr_library_sources(cellular_nordic_nrf91_sm_v2.c)
endif()

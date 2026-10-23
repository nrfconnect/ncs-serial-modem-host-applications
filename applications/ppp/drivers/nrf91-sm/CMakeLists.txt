#
# Copyright (c) 2026 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

if(CONFIG_MODEM_NRF91SM)
	zephyr_library()
	zephyr_library_sources(nordic_nrf91sm.c)
endif()

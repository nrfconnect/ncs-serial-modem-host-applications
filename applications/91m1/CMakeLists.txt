#
# Copyright (c) 2026 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

cmake_minimum_required(VERSION 3.20.0)

# Bundled nrf91-sm driver (see drivers/nrf91-sm/).
set(NRF91SM_DRIVER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/drivers/nrf91-sm)
list(APPEND EXTRA_ZEPHYR_MODULES ${NRF91SM_DRIVER_DIR})

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project("Serial Modem Host - 91m1")

# Memfault CoAP includes unused nRF91 modem headers. Provide stubs on host MCU builds.
if(CONFIG_MEMFAULT_USE_NRF_CLOUD_COAP AND NOT CONFIG_NRF_MODEM_LIB)
	set(MEMFAULT_MODEM_SHIM ${CMAKE_CURRENT_SOURCE_DIR}/cmake/memfault-modem-shim)
	set(MEMFAULT_LIB_TARGET ..__modules__lib__memfault-firmware-sdk__ports__zephyr)
	if(TARGET ${MEMFAULT_LIB_TARGET})
		target_include_directories(${MEMFAULT_LIB_TARGET} BEFORE PRIVATE ${MEMFAULT_MODEM_SHIM})
	endif()
endif()

set(SMHA_LIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../lib)

zephyr_include_directories(
	${SMHA_LIB_DIR}/include
	${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_sources(app PRIVATE
	src/main.c
	src/modem_reset.c
	src/modules/network/network.c
	src/modules/cloud/cloud.c
)

target_sources_ifdef(CONFIG_APP_FOTA app PRIVATE src/modules/fota/fota.c)
target_sources_ifdef(CONFIG_APP_FOTA_SHELL app PRIVATE src/modules/fota/fota_shell.c)

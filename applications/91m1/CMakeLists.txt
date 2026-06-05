#
# Copyright (c) 2026 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project("Serial Modem Host - 91m1")

set(SMHA_LIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../lib)

zephyr_include_directories(${SMHA_LIB_DIR}/include)

target_sources(app PRIVATE src/main.c)

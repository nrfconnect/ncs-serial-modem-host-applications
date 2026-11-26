#
# Copyright (c) 2026 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

# Generates ${CMAKE_BINARY_DIR}/merged.hex from all images.
# Set MERGED_HEX_APP_IMAGE to the application's sysbuild image name before
# including this file. Requires SB_CONFIG_MERGED_HEX_FILES=y.

if(NOT DEFINED MERGED_HEX_APP_IMAGE)
  message(FATAL_ERROR
    "Set MERGED_HEX_APP_IMAGE to the sysbuild image name before "
    "including merged_hex.cmake")
endif()

if(TARGET b0)
  sysbuild_get(bl_board_target IMAGE b0 VAR CONFIG_BOARD_TARGET KCONFIG)
endif()
if(NOT bl_board_target AND TARGET mcuboot)
  sysbuild_get(bl_board_target IMAGE mcuboot VAR CONFIG_BOARD_TARGET KCONFIG)
endif()

sysbuild_get(app_board_target IMAGE ${MERGED_HEX_APP_IMAGE} VAR CONFIG_BOARD_TARGET KCONFIG)

if(NOT app_board_target)
  message(WARNING
    "merged_hex.cmake: no board target for image '${MERGED_HEX_APP_IMAGE}', "
    "skipping merged.hex generation")
  return()
endif()

set(merged_out ${CMAKE_BINARY_DIR}/merged.hex)

foreach(var bl_board_target app_board_target)
  if(${var})
    string(REPLACE "/" "_" ${var} "${${var}}")
    string(REPLACE "." "_" ${var} "${${var}}")
    string(REPLACE "@" "_" ${var} "${${var}}")
  endif()
endforeach()

if(bl_board_target)
  # b0 and mcuboot share the same board target on some boards, so
  # merged_<bl_board_target>.hex may already contain both b0 and mcuboot.
  set(merged_bl_hex  ${CMAKE_BINARY_DIR}/merged_${bl_board_target}.hex)
  set(merged_app_hex ${CMAKE_BINARY_DIR}/merged_${app_board_target}.hex)

  set(merged_inputs  ${merged_bl_hex} ${merged_app_hex})
  set(merged_depends merged_${bl_board_target}_target
                     merged_${app_board_target}_target
                     ${merged_inputs})

  # app_provision.hex is only present when b0/NSIB is enabled
  if(TARGET app_provision_target)
    list(APPEND merged_inputs  ${CMAKE_BINARY_DIR}/app_provision.hex)
    list(APPEND merged_depends app_provision_target
                               ${CMAKE_BINARY_DIR}/app_provision.hex)
  endif()

  add_custom_command(
    OUTPUT ${merged_out}
    COMMAND ${PYTHON_EXECUTABLE}
      ${ZEPHYR_BASE}/scripts/build/mergehex.py
      --overlap replace
      -o ${merged_out}
      ${merged_inputs}
    DEPENDS ${merged_depends}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Generating merged.hex (bootloader + app)"
  )
else()
  set(merged_app_hex ${CMAKE_BINARY_DIR}/merged_${app_board_target}.hex)

  add_custom_command(
    OUTPUT ${merged_out}
    COMMAND ${CMAKE_COMMAND} -E copy ${merged_app_hex} ${merged_out}
    DEPENDS merged_${app_board_target}_target ${merged_app_hex}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Generating merged.hex (single image)"
  )
endif()

add_custom_target(merged_hex_target ALL DEPENDS ${merged_out})

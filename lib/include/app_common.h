/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef APP_COMMON_H_
#define APP_COMMON_H_

#include <zephyr/kernel.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/sys/util.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Handle fatal error.
 *  @param is_watchdog_timeout Boolean indicating if the macro was called upon a watchdog timeout.
 */
#define FATAL_ERROR_HANDLE(is_watchdog_timeout) do { \
	LOG_PANIC(); \
	ARG_UNUSED(is_watchdog_timeout); \
	k_sleep(K_SECONDS(10)); \
	__ASSERT(false, "FATAL_ERROR() macro called"); \
} while (0)

/** @brief Macro used to handle fatal errors. */
#define FATAL_ERROR() FATAL_ERROR_HANDLE(0)

/** @brief Macro used to handle watchdog timeouts. */
#define FATAL_ERROR_WATCHDOG_TIMEOUT() FATAL_ERROR_HANDLE(1)

/* Helper macro to create union member from channel and type */
#define UNION_MEMBER(_chan, _type) _type _chan##_data_type;

/**
 * @brief Macro to compute the maximum message size from a list of channel types.
 *
 * @param _CHAN_LIST List of channels to compute the maximum message size from.
 *		     The list should be in the format: (CHANNEL_NAME, type)
 *
 * @return Maximum message size from the list of channels
 */
#define MAX_MSG_SIZE_FROM_LIST(_CHAN_LIST) \
	sizeof(union { _CHAN_LIST(UNION_MEMBER) })

/* Timeout for zbus channel publishing */
#define PUB_TIMEOUT K_MSEC(100)

#ifdef __cplusplus
}
#endif

#endif /* APP_COMMON_H_ */

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

#if defined(CONFIG_MEMFAULT)
#include <memfault/panics/assert.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Flush the logs and assert.
 *
 * @param is_watchdog_timeout Nonzero skips the 10 second delay before the assert.
 */
#define FATAL_ERROR_HANDLE(is_watchdog_timeout) do { \
	LOG_PANIC(); \
	if (!(is_watchdog_timeout)) { \
		k_sleep(K_SECONDS(10)); \
	} \
	__ASSERT(false, "FATAL_ERROR() macro called"); \
} while (0)

/** @brief Handle a fatal error. */
#define FATAL_ERROR() FATAL_ERROR_HANDLE(0)

/**
 * @brief Handle a watchdog timeout.
 *
 * With Memfault, captures a coredump with the "Software Watchdog" reason and does not
 * return. Without Memfault, same as FATAL_ERROR() without the delay.
 */
#if defined(CONFIG_MEMFAULT)
#define FATAL_ERROR_WATCHDOG_TIMEOUT() do { \
	LOG_PANIC(); \
	MEMFAULT_SOFTWARE_WATCHDOG(); \
} while (0)
#else
#define FATAL_ERROR_WATCHDOG_TIMEOUT() FATAL_ERROR_HANDLE(1)
#endif

#define UNION_MEMBER(_chan, _type) _type _chan##_data_type;

/**
 * @brief Size of the largest message type in a channel list.
 *
 * @param _CHAN_LIST X-macro list whose entries are X(channel, type).
 *
 * @return Size in bytes of the largest type in the list.
 */
#define MAX_MSG_SIZE_FROM_LIST(_CHAN_LIST) \
	sizeof(union { _CHAN_LIST(UNION_MEMBER) })

#define PUB_TIMEOUT K_MSEC(100)

#ifdef __cplusplus
}
#endif

#endif /* APP_COMMON_H_ */

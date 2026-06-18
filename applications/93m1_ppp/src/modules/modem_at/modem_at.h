/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef MODEM_AT_H_
#define MODEM_AT_H_

#include <zephyr/kernel.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Run an AT command on the shared modem AT pipe (DLCI 3).
 *
 * Blocks until the modem replies OK/ERROR or the timeout elapses. Intermediate
 * response lines (everything before the final OK) are concatenated, newline
 * separated, into @p resp. Serialised internally, so the modem-at shell and
 * application modules can share the single user pipe.
 *
 * @param req       AT command string (without trailing CR).
 * @param resp      Buffer for the collected response, or NULL to ignore it.
 * @param resp_size Size of @p resp.
 * @param timeout_s Max seconds to wait for the command to complete.
 *
 * @retval 0        Modem replied OK.
 * @retval -EPERM   Pipe not ready (modem not attached yet).
 * @retval -EBUSY   Another command is already running.
 * @retval -EIO     Modem replied ERROR or the script timed out.
 * @retval -EINVAL  Invalid request.
 */
int modem_at_run(const char *req, char *resp, size_t resp_size, uint32_t timeout_s);

#ifdef __cplusplus
}
#endif

#endif /* MODEM_AT_H_ */

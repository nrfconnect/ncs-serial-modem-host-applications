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
 * Blocks until the modem replies OK or ERROR, or the timeout elapses. Response lines
 * before the final OK are collected into @p resp, newline separated. Calls are
 * serialised, so the `at` shell and application modules can share the pipe.
 *
 * @param req       AT command string, without trailing CR.
 * @param resp      Buffer for the collected response, or NULL to ignore it.
 * @param resp_size Size of @p resp.
 * @param timeout_s Max seconds to wait for the command to complete.
 *
 * @retval 0        Modem replied OK.
 * @retval -EINVAL  @p req is NULL or does not fit CONFIG_APP_MODEM_AT_COMMAND_MAX_SIZE.
 * @retval -EPERM   AT pipe not open (modem not attached yet).
 * @retval -EBUSY   AT pipe not available within CONFIG_APP_MODEM_AT_PIPE_WAIT_TIMEOUT_SECONDS.
 * @retval -EAGAIN  Modem replied ERROR, the command timed out, or another modem_at_run()
 *                  call held the pipe for CONFIG_APP_MODEM_AT_PIPE_WAIT_TIMEOUT_SECONDS.
 */
int modem_at_run(const char *req, char *resp, size_t resp_size, uint32_t timeout_s);

#ifdef __cplusplus
}
#endif

#endif /* MODEM_AT_H_ */

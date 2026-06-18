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
 * @brief Power-cycle the modem and bring up the AT transport.
 *
 * @retval 0 on success, negative errno otherwise.
 */
int modem_at_setup(void);

/**
 * @brief Whether modem_at_setup() has completed.
 */
bool modem_at_is_ready(void);

/**
 * @brief Run an AT command synchronously.
 *
 * Blocks until OK/ERROR or timeout. Response lines are newline-joined into
 * @p resp. URC-matched lines are routed to subscribers, not collected here.
 *
 * @param req       AT command string (without trailing CR).
 * @param resp      Buffer for the response, or NULL to discard.
 * @param resp_size Size of @p resp.
 * @param timeout_s Seconds to wait before aborting.
 *
 * @retval 0        Modem replied OK.
 * @retval -EPERM   Transport not ready.
 * @retval -EIO     Modem replied ERROR or timed out.
 * @retval -EINVAL  Invalid request.
 */
int modem_at_run(const char *req, char *resp, size_t resp_size, uint32_t timeout_s);

/**
 * @brief URC callback. argv[0] is the matched prefix, argv[1..] the arguments.
 */
typedef void (*modem_at_urc_cb)(char **argv, uint16_t argc, void *user_data);

/**
 * @brief Subscribe to a URC prefix (e.g. "+CEREG: ").
 *
 * @param prefix    Must match a prefix registered in unsol_matches.
 * @param cb        Invoked from the modem_chat receive context.
 * @param user_data Passed back to @p cb.
 *
 * @retval 0        Subscribed.
 * @retval -ENOMEM  Subscriber table full.
 * @retval -EINVAL  Invalid arguments.
 */
int modem_at_urc_subscribe(const char *prefix, modem_at_urc_cb cb, void *user_data);

#ifdef __cplusplus
}
#endif

#endif /* MODEM_AT_H_ */

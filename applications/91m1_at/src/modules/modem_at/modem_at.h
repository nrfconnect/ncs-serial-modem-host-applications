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
 * @brief Run an AT command synchronously on the Serial Modem UART.
 *
 * Blocks until OK/ERROR or timeout. Response lines are newline-joined into
 * @p resp. Lines handled by a URC waiter or subscriber are not collected.
 * Serialised internally, so the `at` shell and application modules can share
 * the UART.
 *
 * @param req       AT command string (without trailing CR).
 * @param resp      Buffer for the response, or NULL to discard.
 * @param resp_size Size of @p resp.
 * @param timeout_s Seconds to wait before aborting.
 *
 * @retval 0        Modem replied OK.
 * @retval -EAGAIN  Timed out waiting for another command to finish.
 * @retval -EIO     Modem replied ERROR or timed out.
 * @retval -EINVAL  Invalid request.
 */
int modem_at_run(const char *req, char *resp, size_t resp_size, uint32_t timeout_s);

/**
 * @brief Run an AT command and wait for the URC that reports its result.
 *
 * The wait is armed before the command is sent, so a URC that arrives before
 * OK is not missed. The UART is free for other commands while waiting.
 *
 * @param req           AT command string (without trailing CR).
 * @param urc_prefix    URC prefix to wait for, e.g. "#XNRFPROV: ". Must be one of
 *                      the prefixes registered in the module.
 * @param urc           Buffer for the URC arguments, comma-joined.
 * @param urc_size      Size of @p urc.
 * @param timeout_s     Seconds to wait for OK.
 * @param urc_timeout_s Seconds to wait for the URC after OK.
 *
 * @retval -ETIMEDOUT   The URC did not arrive in time.
 * @retval -ENOMEM      No free URC waiter.
 *
 * Other return values are the same as for modem_at_run().
 */
int modem_at_run_wait_urc(const char *req, const char *urc_prefix, char *urc, size_t urc_size,
			  uint32_t timeout_s, uint32_t urc_timeout_s);

/**
 * @brief URC callback. argv[0] is the matched prefix, argv[1..] the arguments.
 */
typedef void (*modem_at_urc_cb)(char **argv, uint16_t argc, void *user_data);

/**
 * @brief Subscribe to a URC prefix (e.g. "+CEREG: ").
 *
 * @param prefix    Must match a prefix registered in the module.
 * @param cb        Invoked from the modem receive context. Must not block or run
 *                  AT commands.
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

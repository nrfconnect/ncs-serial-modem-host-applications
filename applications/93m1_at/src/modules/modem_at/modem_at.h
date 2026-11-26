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
 * @brief Run an AT command synchronously on the shared user pipe (DLCI 3).
 *
 * Blocks until OK/ERROR or timeout. Response lines are newline-joined into
 * @p resp. Lines handled by a URC subscriber are not collected.
 * Serialised internally, so the `at` shell and application modules can share
 * the user pipe.
 *
 * @param req       AT command string (without trailing CR).
 * @param resp      Buffer for the response, or NULL to discard.
 * @param resp_size Size of @p resp.
 * @param timeout_s Seconds to wait before aborting.
 *
 * @retval 0        Modem replied OK.
 * @retval -EPERM   Pipe not ready (modem not attached yet).
 * @retval -EBUSY   Timed out waiting for another user of the pipe to release it.
 * @retval -EAGAIN  Modem replied ERROR, the command timed out, or timed out waiting
 *                  for another command to finish.
 * @retval -EINVAL  Invalid request.
 */
int modem_at_run(const char *req, char *resp, size_t resp_size, uint32_t timeout_s);

/** Build a CoAP code as reported by modem_at_run_coap(), e.g. COAP_CODE(4, 1) for 4.01. */
#define COAP_CODE(class, detail) ((uint16_t)((class) * 100 + (detail)))

/** CoAP 4.01 Unauthorized, e.g. device not onboarded to nRF Cloud. */
#define MODEM_AT_COAP_UNAUTHORIZED COAP_CODE(4, 1)

/**
 * @brief Like modem_at_run(), but also reports the CoAP response code the modem
 *        received while the command ran.
 *
 * @param coap_code Set to the CoAP code (see COAP_CODE()), or 0 if no CoAP
 *                  response was received. May be NULL.
 *
 * Return values are the same as for modem_at_run().
 */
int modem_at_run_coap(const char *req, char *resp, size_t resp_size, uint32_t timeout_s,
		      uint16_t *coap_code);

/**
 * @brief URC callback. argv[0] is the matched prefix, argv[1..] the arguments.
 */
typedef void (*modem_at_urc_cb)(char **argv, uint16_t argc, void *user_data);

/**
 * @brief Subscribe to a URC prefix (e.g. "%NRFCLOUDLOCATION: ").
 *
 * Only the first subscriber of a prefix receives its URCs.
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

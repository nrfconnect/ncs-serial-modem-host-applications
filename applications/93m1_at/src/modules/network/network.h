/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef NETWORK_H_
#define NETWORK_H_

#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Network module channel.
 *
 * Carries @ref NETWORK_CONNECT and @ref NETWORK_DISCONNECT to the module, and
 * @ref NETWORK_CONNECTED and @ref NETWORK_DISCONNECTED back to its observers.
 */
ZBUS_CHAN_DECLARE(network_chan);

/** @brief Message types on @ref network_chan. */
enum network_msg_type {
	/** Output: L4 connectivity on the PPP link is lost. */
	NETWORK_DISCONNECTED = 0x1,
	/** Output: L4 connectivity on the PPP link is established. */
	NETWORK_CONNECTED,
	/** Input: bring all connectivity interfaces up and connect. Ignored while connected. */
	NETWORK_CONNECT,
	/** Input: take all connectivity interfaces down. Ignored while disconnected. */
	NETWORK_DISCONNECT,
};

/** @brief Message published on @ref network_chan. */
struct network_msg {
	enum network_msg_type type;
};

#ifdef __cplusplus
}
#endif

#endif /* NETWORK_H_ */

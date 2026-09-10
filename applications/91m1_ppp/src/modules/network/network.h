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

/** @brief Channel carrying @ref network_msg messages to and from the network module. */
ZBUS_CHAN_DECLARE(network_chan);

/** @brief Message types published on and handled by @ref network_chan. */
enum network_msg_type {
	/* Output message types (published by the network module). */

	/** Network connectivity has been lost. */
	NETWORK_DISCONNECTED = 0x1,

	/** Network connectivity has been established. */
	NETWORK_CONNECTED,

	/* Input message types (handled by the network module). */

	/** Request to bring up the network interfaces and connect. */
	NETWORK_CONNECT,

	/** Request to disconnect and bring down the network interfaces. */
	NETWORK_DISCONNECT,
};

/** @brief Message carried on @ref network_chan. */
struct network_msg {
	/** Message type. */
	enum network_msg_type type;
};

#ifdef __cplusplus
}
#endif

#endif /* NETWORK_H_ */

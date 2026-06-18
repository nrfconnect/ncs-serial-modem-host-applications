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

ZBUS_CHAN_DECLARE(network_chan);

enum network_msg_type {
	NETWORK_DISCONNECTED = 0x1,
	NETWORK_CONNECTED,
};

struct network_msg {
	enum network_msg_type type;
};

#ifdef __cplusplus
}
#endif

#endif /* NETWORK_H_ */

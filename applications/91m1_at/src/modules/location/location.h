/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef LOCATION_H_
#define LOCATION_H_

#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>

#ifdef __cplusplus
extern "C" {
#endif

ZBUS_CHAN_DECLARE(location_chan);

enum location_msg_type {
	/* Requested of the module: get a location fix through the Serial Modem. */
	LOCATION_REQUEST = 0x1,
	/* Internal: the cellular request failed, try GNSS. */
	LOCATION_GNSS_FALLBACK,
};

struct location_msg {
	enum location_msg_type type;
};

#ifdef __cplusplus
}
#endif

#endif /* LOCATION_H_ */

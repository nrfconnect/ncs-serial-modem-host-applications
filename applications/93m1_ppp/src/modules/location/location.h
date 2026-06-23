/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef LOCATION_H_
#define LOCATION_H_

#include <zephyr/zbus/zbus.h>

#ifdef __cplusplus
extern "C" {
#endif

enum location_mode {
	LOCATION_MODE_ALL,  /* cell + Wi-Fi */
	LOCATION_MODE_CELL, /* cell only    */
	LOCATION_MODE_WIFI, /* Wi-Fi only   */
};

enum location_msg_type {
	LOCATION_FIX_REQUEST,
};

struct location_msg {
	enum location_msg_type type;
	enum location_mode mode;
};

ZBUS_CHAN_DECLARE(location_chan);

#ifdef __cplusplus
}
#endif

#endif /* LOCATION_H_ */

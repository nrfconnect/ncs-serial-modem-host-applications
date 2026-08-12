/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef APP_CLOUD_H_
#define APP_CLOUD_H_

#include <zephyr/zbus/zbus.h>

#ifdef __cplusplus
extern "C" {
#endif

enum cloud_msg_type {
	/* Input: upload pending diagnostics. */
	CLOUD_SYNC_REQUEST,

	/* Output: the sync finished, successfully or not. */
	CLOUD_SYNC_DONE,
};

struct cloud_msg {
	enum cloud_msg_type type;
};

ZBUS_CHAN_DECLARE(cloud_chan);

#ifdef __cplusplus
}
#endif

#endif /* APP_CLOUD_H_ */

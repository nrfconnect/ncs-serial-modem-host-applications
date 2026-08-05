/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef CLOUD_H_
#define CLOUD_H_

#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>

#ifdef __cplusplus
extern "C" {
#endif

ZBUS_CHAN_DECLARE(cloud_chan);

enum cloud_msg_type {
	CLOUD_DISCONNECTED = 0x1,
	CLOUD_CONNECTED,
	CLOUD_SEND_MESSAGE,
	CLOUD_MESSAGE_SENT,
};

struct cloud_msg {
	enum cloud_msg_type type;
	char payload[CONFIG_APP_CLOUD_MSG_MAX_LEN];
	size_t payload_len;
};

#ifdef __cplusplus
}
#endif

#endif /* CLOUD_H_ */

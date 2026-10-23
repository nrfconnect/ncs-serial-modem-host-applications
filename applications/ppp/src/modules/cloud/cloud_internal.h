/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef CLOUD_INTERNAL_H_
#define CLOUD_INTERNAL_H_

#include <zephyr/zbus/zbus.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Private cloud channel message types, used only within the cloud module. */
enum priv_cloud_msg_type {
	/* Make one attempt at establishing the cloud connection. */
	CLOUD_PRIV_CONNECT_ATTEMPT,
	/* The cloud session is no longer usable and must be re-established. */
	CLOUD_PRIV_REQUEST_FAILED,
};

struct priv_cloud_msg {
	enum priv_cloud_msg_type type;
};

/* Private cloud channel, defined in cloud.c. */
ZBUS_CHAN_DECLARE(priv_cloud_chan);

#ifdef __cplusplus
}
#endif

#endif /* CLOUD_INTERNAL_H_ */

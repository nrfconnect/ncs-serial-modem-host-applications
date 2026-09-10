/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef CLOUD_H_
#define CLOUD_H_

#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>

#if defined(CONFIG_APP_LOCATION)
#include "modules/location/location.h"
#endif /* CONFIG_APP_LOCATION */

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Channel carrying @ref cloud_msg messages to and from the cloud module. */
ZBUS_CHAN_DECLARE(cloud_chan);

/** @brief Message types published on and handled by @ref cloud_chan. */
enum cloud_msg_type {
	/* Output message types (published by the cloud module). */

	/** The cloud connection is down. */
	CLOUD_DISCONNECTED = 0x1,

	/** The cloud connection is established. */
	CLOUD_CONNECTED,

	/** The device shadow has been polled. */
	CLOUD_SHADOW_POLLED,

	/* Input message types (handled by the cloud module). */

	/** Request to establish the cloud connection. */
	CLOUD_CONNECT,

	/** Request to tear down the cloud connection. */
	CLOUD_DISCONNECT,

	/** Request to send the device message in @ref cloud_msg::payload. */
	CLOUD_MESSAGE_SEND,

	/** Request to poll the device shadow. */
	CLOUD_SHADOW_POLL_REQUEST,

	/** Request to post any pending Memfault data. */
	CLOUD_MEMFAULT_POST_REQUEST,
#if defined(CONFIG_APP_LOCATION)
	/** Request to resolve the Wi-Fi based location in
	 *  @ref cloud_msg::location_request using the nRF Cloud location service.
	 */
	CLOUD_LOCATION_REQUEST,
#endif /* CONFIG_APP_LOCATION */
};

/** @brief Message carried on @ref cloud_chan. */
struct cloud_msg {
	/** Message type, determines which of the remaining fields are valid. */
	enum cloud_msg_type type;

	/** Device message payload. Valid for @ref CLOUD_MESSAGE_SEND messages. */
	char payload[CONFIG_APP_CLOUD_MSG_MAX_LEN];

	/** Length of @ref cloud_msg::payload in bytes, excluding the terminating NUL. */
	size_t payload_len;
#if defined(CONFIG_APP_LOCATION)
	/** Wi-Fi access point data to resolve into a location.
	 *  Valid for @ref CLOUD_LOCATION_REQUEST messages.
	 */
	struct location_cloud_request_data location_request;
#endif /* CONFIG_APP_LOCATION */
};

#ifdef __cplusplus
}
#endif

#endif /* CLOUD_H_ */

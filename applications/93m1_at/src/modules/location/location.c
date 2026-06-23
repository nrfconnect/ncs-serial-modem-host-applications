/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <stdlib.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>

#include "app_common.h"
#include "modules/modem_at/modem_at.h"
#include "modules/network/network.h"
#include "location.h"

LOG_MODULE_REGISTER(location, CONFIG_APP_LOCATION_LOG_LEVEL);

ZBUS_CHAN_DEFINE(location_chan,
		 struct location_msg,
		 NULL,
		 NULL,
		 ZBUS_OBSERVERS_EMPTY,
		 ZBUS_MSG_INIT(0));

ZBUS_MSG_SUBSCRIBER_DEFINE(location);

#define CHANNEL_LIST(X)				\
	X(network_chan, struct network_msg)	\
	X(location_chan, struct location_msg)

#define MAX_MSG_SIZE MAX_MSG_SIZE_FROM_LIST(CHANNEL_LIST)

#define ADD_OBSERVERS(_chan, _type) ZBUS_CHAN_ADD_OBS(_chan, location, 0);

CHANNEL_LIST(ADD_OBSERVERS)

/* Forward declarations */
static bool is_coordinate(const char *s);
static const char *location_method_str(const char *method_num);
static void on_location(char **argv, uint16_t argc, void *user_data);
static void location_request(void);
static void location_thread(void);

/* A fix line starts with a numeric latitude; anything else is a status URC. */
static bool is_coordinate(const char *s)
{
	return s != NULL && (*s == '-' || *s == '+' || (*s >= '0' && *s <= '9'));
}

static const char *location_method_str(const char *method_num)
{
	switch (atoi(method_num)) {
	case 1: return "Single-cell";
	case 2: return "Multicell";
	case 4: return "Wi-Fi";
	default: return method_num;
	}
}

/* %NRFCLOUDLOCATION: <lat>,<lon>,<unc>,<method> — async result URC. */
static void on_location(char **argv, uint16_t argc, void *user_data)
{
	ARG_UNUSED(user_data);

	if (argc >= 5 && is_coordinate(argv[1])) {
		LOG_INF("Location: %s,%s Uncertainty: %sm Method: %s",
			argv[1], argv[2], argv[3], location_method_str(argv[4]));
	} else if (argc >= 2) {
		LOG_DBG("NRFCLOUDLOCATION status: %s", argv[1]);
	}
}

static void location_request(void)
{
	char cmd[40];
	int err;

	(void)snprintk(cmd, sizeof(cmd), "AT%%NRFCLOUDLOCATION=%d,1",
		       CONFIG_APP_LOCATION_METHOD);

	err = modem_at_run(cmd, NULL, 0, CONFIG_APP_LOCATION_AT_TIMEOUT_SECONDS);
	if (err) {
		LOG_WRN("%s failed: %d", cmd, err);
	}
}

static void location_thread(void)
{
	int err;
	const struct zbus_channel *chan;
	uint8_t msg_buf[MAX_MSG_SIZE];
	bool connected = false;

	err = modem_at_urc_subscribe("%NRFCLOUDLOCATION: ", on_location, NULL);
	if (err) {
		LOG_ERR("Failed to subscribe to %%NRFCLOUDLOCATION URC: %d", err);
	}

	while (true) {
		err = zbus_sub_wait_msg(&location, &chan, msg_buf, K_FOREVER);
		if (err) {
			LOG_ERR("zbus_sub_wait_msg, error: %d", err);
			return;
		}

		if (chan == &network_chan) {
			const struct network_msg *msg = (const struct network_msg *)msg_buf;

			connected = (msg->type == NETWORK_CONNECTED);
		} else if (chan == &location_chan && connected) {
			location_request();
		}
	}
}

K_THREAD_DEFINE(location_tid, CONFIG_APP_LOCATION_THREAD_STACK_SIZE, location_thread,
		NULL, NULL, NULL, K_LOWEST_APPLICATION_THREAD_PRIO, 0, 0);

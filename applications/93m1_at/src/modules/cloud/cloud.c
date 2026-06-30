/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>

#include "app_common.h"
#include "modules/modem_at/modem_at.h"
#include "modules/network/network.h"
#include "cloud.h"

LOG_MODULE_REGISTER(cloud, CONFIG_APP_CLOUD_LOG_LEVEL);

ZBUS_CHAN_DEFINE(cloud_chan,
		 struct cloud_msg,
		 NULL,
		 NULL,
		 ZBUS_OBSERVERS_EMPTY,
		 ZBUS_MSG_INIT(0));

ZBUS_MSG_SUBSCRIBER_DEFINE(cloud);

#define CHANNEL_LIST(X)				\
	X(network_chan, struct network_msg)	\
	X(cloud_chan, struct cloud_msg)

#define MAX_MSG_SIZE MAX_MSG_SIZE_FROM_LIST(CHANNEL_LIST)

#define ADD_OBSERVERS(_chan, _type) ZBUS_CHAN_ADD_OBS(_chan, cloud, 0);

CHANNEL_LIST(ADD_OBSERVERS)

/* Helper functions */

static int cloud_send_battery(int percent)
{
	char cmd[CONFIG_APP_CLOUD_PAYLOAD_BUFFER];
	int len = sizeof(cmd);
	int err;

	err = snprintk(cmd, len,
		       "AT%%NRFCLOUDMESSAGE="
		       "{\"appId\":\"BATTERY\",\"messageType\":\"DATA\",\"data\":\"%d\"}", percent);
	if ((err < 0) || (err >= len)) {
		LOG_ERR("snprintk, error: %d", err);
		return -EINVAL;
	}

	err = modem_at_run(cmd, NULL, 0, CONFIG_APP_CLOUD_AT_TIMEOUT_SECONDS);
	if (err) {
		LOG_ERR("modem_at_run, error: %d", err);
		return -ENETUNREACH;
	}

	LOG_DBG("Battery %d%% sent to nRF Cloud", percent);

	return 0;
}

static void cloud_thread(void)
{
	int err;
	const struct zbus_channel *chan;
	uint8_t msg_buf[MAX_MSG_SIZE];
	bool connected = false;

	while (true) {
		err = zbus_sub_wait_msg(&cloud, &chan, msg_buf, K_FOREVER);
		if (err) {
			LOG_ERR("zbus_sub_wait_msg, error: %d", err);
			FATAL_ERROR();
		}

		if (chan == &network_chan) {
			const struct network_msg *msg = (const struct network_msg *)msg_buf;

			connected = (msg->type == NETWORK_CONNECTED);
		} else if (chan == &cloud_chan && connected) {
			const struct cloud_msg *msg = (const struct cloud_msg *)msg_buf;

			if (msg->type == CLOUD_BATTERY_SAMPLE) {
				err = cloud_send_battery(msg->battery_percent);
				if (err == -ENETUNREACH) {
					LOG_WRN("Failed to sent battery data, network is down?");
				} else if (err) {
					LOG_ERR("cloud_send_battery, error: %d", err);
					FATAL_ERROR();
				} else {
				}
			}
		} else {
			LOG_WRN("Unhandled message in cloud thread");
		}
	}
}

K_THREAD_DEFINE(cloud_tid, CONFIG_APP_CLOUD_THREAD_STACK_SIZE, cloud_thread,
		NULL, NULL, NULL, K_LOWEST_APPLICATION_THREAD_PRIO, 0, 0);

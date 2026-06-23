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

static void cloud_send_battery(int percent);
static void cloud_thread(void);

static void cloud_send_battery(int percent)
{
	char cmd[64];
	int err;

	(void)snprintk(cmd, sizeof(cmd),
		       "AT%%NRFCLOUDMESSAGE={\"appId\":\"BATTERY\",\"data\":\"%d\"}", percent);

	err = modem_at_run(cmd, NULL, 0, CONFIG_APP_CLOUD_AT_TIMEOUT_SECONDS);
	if (err) {
		LOG_WRN("Battery cloud message failed: %d", err);
		return;
	}

	LOG_DBG("Battery %d%% sent to nRF Cloud", percent);
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
			return;
		}

		if (chan == &network_chan) {
			const struct network_msg *msg = (const struct network_msg *)msg_buf;

			connected = (msg->type == NETWORK_CONNECTED);
		} else if (chan == &cloud_chan && connected) {
			const struct cloud_msg *msg = (const struct cloud_msg *)msg_buf;

			switch (msg->type) {
			case CLOUD_BATTERY_SAMPLE:
				cloud_send_battery(msg->battery_percent);
				break;
			default:
				break;
			}
		}
	}
}

K_THREAD_DEFINE(cloud_tid, CONFIG_APP_CLOUD_THREAD_STACK_SIZE, cloud_thread,
		NULL, NULL, NULL, K_LOWEST_APPLICATION_THREAD_PRIO, 0, 0);

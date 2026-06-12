/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/task_wdt/task_wdt.h>
#include <zephyr/smf.h>
#include <string.h>

#include "app_common.h"
#include "modules/network/network.h"
#include "modules/cloud/cloud.h"

LOG_MODULE_REGISTER(main, CONFIG_APP_MAIN_LOG_LEVEL);

BUILD_ASSERT(CONFIG_APP_MAIN_WATCHDOG_TIMEOUT_SECONDS >
	     CONFIG_APP_MAIN_MSG_PROCESSING_TIMEOUT_SECONDS,
	     "Watchdog timeout must be greater than maximum message processing time");

ZBUS_MSG_SUBSCRIBER_DEFINE(main_subscriber);

#define CHANNEL_LIST(X) \
	X(network_chan, struct network_msg) \
	X(cloud_chan, struct cloud_msg)

#define MAX_MSG_SIZE MAX_MSG_SIZE_FROM_LIST(CHANNEL_LIST)

#define ADD_OBSERVERS(_chan, _type) ZBUS_CHAN_ADD_OBS(_chan, main_subscriber, 0);

CHANNEL_LIST(ADD_OBSERVERS)

enum main_app_state {
	STATE_RUNNING,
};

struct main_state {
	struct smf_ctx ctx;
	const struct zbus_channel *chan;
	uint8_t msg_buf[MAX_MSG_SIZE];
};

static struct main_state main_state;
static const struct smf_state states[];

#define DEMO_CLOUD_PAYLOAD \
	"{\"appId\":\"SMHA\",\"messageType\":\"DATA\",\"data\":\"hello\"}"

static void send_demo_cloud_message(void)
{
	struct cloud_msg msg = {
		.type = CLOUD_SEND_MESSAGE,
		.payload = DEMO_CLOUD_PAYLOAD,
	};
	int err;

	msg.payload_len = strlen(msg.payload);
	err = zbus_chan_pub(&cloud_chan, &msg, PUB_TIMEOUT);
	if (err) {
		LOG_ERR("zbus_chan_pub demo message, error: %d", err);
	}
}

static void running_entry(void *obj)
{
	ARG_UNUSED(obj);

	LOG_INF("running_entry");
}

static enum smf_state_result running_run(void *obj)
{
	struct main_state *state_object = obj;

	if (state_object->chan == &network_chan) {
		const struct network_msg *msg =
			(const struct network_msg *)state_object->msg_buf;

		switch (msg->type) {
		case NETWORK_CONNECTED:
			LOG_INF("Network connected");
			break;
		case NETWORK_DISCONNECTED:
			LOG_INF("Network disconnected");
			break;
		default:
			break;
		}

		return SMF_EVENT_HANDLED;
	}

	if (state_object->chan == &cloud_chan) {
		const struct cloud_msg *msg =
			(const struct cloud_msg *)state_object->msg_buf;

		switch (msg->type) {
		case CLOUD_CONNECTED:
			LOG_INF("Cloud connected");
			send_demo_cloud_message();
			break;
		case CLOUD_DISCONNECTED:
			LOG_INF("Cloud disconnected");
			break;
		case CLOUD_MESSAGE_SENT:
			LOG_INF("Cloud message sent");
			break;
		default:
			break;
		}
	}

	return SMF_EVENT_HANDLED;
}

static const struct smf_state states[] = {
	[STATE_RUNNING] = SMF_CREATE_STATE(running_entry, running_run, NULL, NULL, NULL),
};

static void main_wdt_callback(int channel_id, void *user_data)
{
	LOG_ERR("Main watchdog expired, channel: %d, thread: %s",
		channel_id, k_thread_name_get((k_tid_t)user_data));

	SEND_FATAL_ERROR_WATCHDOG_TIMEOUT();
}

int main(void)
{
	int err;
	int task_wdt_id;
	const uint32_t wdt_timeout_ms =
		(CONFIG_APP_MAIN_WATCHDOG_TIMEOUT_SECONDS * MSEC_PER_SEC);
	const uint32_t execution_time_ms =
		(CONFIG_APP_MAIN_MSG_PROCESSING_TIMEOUT_SECONDS * MSEC_PER_SEC);
	const k_timeout_t zbus_wait = K_MSEC(wdt_timeout_ms - execution_time_ms);

	task_wdt_id = task_wdt_add(wdt_timeout_ms, main_wdt_callback, (void *)k_current_get());
	if (task_wdt_id < 0) {
		LOG_ERR("Failed to add task to watchdog: %d", task_wdt_id);
		SEND_FATAL_ERROR();
		return -EFAULT;
	}

	smf_set_initial(SMF_CTX(&main_state), &states[STATE_RUNNING]);

	while (true) {
		err = task_wdt_feed(task_wdt_id);
		if (err) {
			LOG_ERR("task_wdt_feed, error: %d", err);
			SEND_FATAL_ERROR();
			return -EFAULT;
		}

		err = zbus_sub_wait_msg(&main_subscriber, &main_state.chan,
					main_state.msg_buf, zbus_wait);
		if (err == -ENOMSG) {
			continue;
		} else if (err) {
			LOG_ERR("zbus_sub_wait_msg, error: %d", err);
			SEND_FATAL_ERROR();
			return -EFAULT;
		}

		err = smf_run_state(SMF_CTX(&main_state));
		if (err) {
			LOG_ERR("smf_run_state(), error: %d", err);
			SEND_FATAL_ERROR();
			return -EFAULT;
		}
	}

	return 0;
}

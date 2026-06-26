/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/smf.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/task_wdt/task_wdt.h>
#include <zephyr/zbus/zbus.h>
#include <date_time.h>
#include <memfault/metrics/metrics.h>
#include <memfault/ports/zephyr/http.h>
#include <net/nrf_cloud.h>
#include <net/nrf_cloud_coap.h>

#include "app_common.h"
#include "modules/network/network.h"
#include "cloud.h"

LOG_MODULE_REGISTER(cloud, CONFIG_APP_CLOUD_LOG_LEVEL);

#define TIME_WAIT_TIMEOUT_S  120
#define CREDENTIAL_RETRY	K_SECONDS(CONFIG_APP_CLOUD_CREDENTIAL_RETRY_SECONDS)

BUILD_ASSERT(CONFIG_APP_CLOUD_WATCHDOG_TIMEOUT_SECONDS >
	     CONFIG_APP_CLOUD_MSG_PROCESSING_TIMEOUT_SECONDS,
	     "Watchdog timeout must be greater than maximum message processing time");

ZBUS_CHAN_DEFINE(cloud_chan,
		 struct cloud_msg,
		 NULL,
		 NULL,
		 ZBUS_OBSERVERS_EMPTY,
		 ZBUS_MSG_INIT(.type = CLOUD_DISCONNECTED));

ZBUS_MSG_SUBSCRIBER_DEFINE(cloud);

ZBUS_CHAN_ADD_OBS(network_chan, cloud, 0);
ZBUS_CHAN_ADD_OBS(cloud_chan, cloud, 0);

enum cloud_state {
	STATE_DISCONNECTED,
	STATE_CONNECTING,
	STATE_CONNECTED,
};

struct cloud_state_object {
	struct smf_ctx ctx;
	const struct zbus_channel *chan;
	uint8_t msg_buf[MAX(MAX(sizeof(struct network_msg),
				sizeof(struct cloud_msg)), 1)];
};

static struct cloud_state_object cloud_state;
static const struct smf_state states[];
static atomic_t connect_abort;

static K_SEM_DEFINE(date_time_sem, 0, 1);

static void publish_cloud_msg(enum cloud_msg_type type, const char *payload,
			      size_t payload_len)
{
	struct cloud_msg msg = { .type = type };
	int err;

	if (payload != NULL && payload_len > 0) {
		if (payload_len >= sizeof(msg.payload)) {
			payload_len = sizeof(msg.payload) - 1;
		}

		memcpy(msg.payload, payload, payload_len);
		msg.payload[payload_len] = '\0';
		msg.payload_len = payload_len;
	}

	err = zbus_chan_pub(&cloud_chan, &msg, PUB_TIMEOUT);
	if (err) {
		LOG_ERR("zbus_chan_pub, error: %d", err);
		FATAL_ERROR();
	}
}

static void date_time_event_handler(const struct date_time_evt *evt)
{
	switch (evt->type) {
	case DATE_TIME_OBTAINED_MODEM:
	case DATE_TIME_OBTAINED_NTP:
	case DATE_TIME_OBTAINED_EXT:
		k_sem_give(&date_time_sem);
		break;
	default:
		break;
	}
}

static bool credentials_ready(void)
{
	struct nrf_cloud_credentials_status cs;
	int err;

	err = nrf_cloud_credentials_check(&cs);
	if (err) {
		LOG_ERR("nrf_cloud_credentials_check, error: %d", err);
		return false;
	}

	if (cs.ca && cs.prv_key) {
		return true;
	}

	LOG_WRN("Missing nRF Cloud credentials (see doc/README.md)");
	if (!cs.ca) {
		LOG_WRN("  - CA cert (run device_credentials_installer --coap)");
	}
	if (!cs.prv_key) {
		LOG_WRN("  - Private key (JWT signing key)");
	}

	return false;
}

static bool wait_for_valid_time(void)
{
	int64_t deadline = k_uptime_get() +
			   (TIME_WAIT_TIMEOUT_S * MSEC_PER_SEC);

	if (date_time_is_valid()) {
		return true;
	}

	date_time_update_async(date_time_event_handler);

	while (!atomic_get(&connect_abort) && k_uptime_get() < deadline) {
		if (k_sem_take(&date_time_sem, K_SECONDS(1)) == 0 &&
		    date_time_is_valid()) {
			return true;
		}
	}

	LOG_WRN("Valid date/time not available within %d seconds",
		TIME_WAIT_TIMEOUT_S);
	return false;
}

static void post_memfault_data(void)
{
	int err;

	memfault_metrics_heartbeat_debug_trigger();

	err = memfault_zephyr_port_post_data();
	if (err) {
		LOG_WRN("memfault_zephyr_port_post_data, error: %d", err);
	} else {
		LOG_INF("Memfault data posted");
	}
}

static void cloud_connect(void)
{
	char device_id[NRF_CLOUD_CLIENT_ID_MAX_LEN + 1];
	int err;

	while (!atomic_get(&connect_abort)) {
		if (!credentials_ready()) {
			k_sleep(CREDENTIAL_RETRY);
			continue;
		}

		if (!wait_for_valid_time()) {
			k_sleep(CREDENTIAL_RETRY);
			continue;
		}

		err = nrf_cloud_client_id_get(device_id, sizeof(device_id));
		if (err) {
			LOG_ERR("nrf_cloud_client_id_get, error: %d", err);
			k_sleep(CREDENTIAL_RETRY);
			continue;
		}

		LOG_INF("nRF Cloud client ID: %s", device_id);

		err = nrf_cloud_coap_connect(NULL);
		if (err) {
			LOG_ERR("nrf_cloud_coap_connect, error: %d", err);
			k_sleep(CREDENTIAL_RETRY);
			continue;
		}

		LOG_INF("Connected to nRF Cloud");

		if (!atomic_get(&connect_abort)) {
			publish_cloud_msg(CLOUD_CONNECTED, NULL, 0);
		}

		return;
	}
}

static void disconnected_entry(void *obj)
{
	ARG_UNUSED(obj);

	LOG_DBG("Cloud module disconnected");
}

static enum smf_state_result disconnected_run(void *obj)
{
	struct cloud_state_object *state_object = obj;

	if (state_object->chan != &network_chan) {
		return SMF_EVENT_HANDLED;
	}

	const struct network_msg *msg =
		(const struct network_msg *)state_object->msg_buf;

	if (msg->type == NETWORK_CONNECTED) {
		smf_set_state(SMF_CTX(state_object), &states[STATE_CONNECTING]);
	}

	return SMF_EVENT_HANDLED;
}

static void connecting_entry(void *obj)
{
	ARG_UNUSED(obj);

	LOG_DBG("Cloud module connecting");

	atomic_set(&connect_abort, 0);
	cloud_connect();
}

static enum smf_state_result connecting_run(void *obj)
{
	struct cloud_state_object *state_object = obj;

	if (state_object->chan == &network_chan) {
		const struct network_msg *msg =
			(const struct network_msg *)state_object->msg_buf;

		if (msg->type == NETWORK_DISCONNECTED) {
			atomic_set(&connect_abort, 1);
			(void)nrf_cloud_coap_disconnect();
			publish_cloud_msg(CLOUD_DISCONNECTED, NULL, 0);
			smf_set_state(SMF_CTX(state_object),
				      &states[STATE_DISCONNECTED]);
		}

		return SMF_EVENT_HANDLED;
	}

	if (state_object->chan == &cloud_chan) {
		const struct cloud_msg *msg = (const struct cloud_msg *)state_object->msg_buf;

		if (msg->type == CLOUD_CONNECTED) {
			smf_set_state(SMF_CTX(state_object), &states[STATE_CONNECTED]);
		}
	}

	return SMF_EVENT_HANDLED;
}

static void connected_entry(void *obj)
{
	ARG_UNUSED(obj);

	LOG_DBG("Cloud module connected");

	/* Do an initial update on connect */
	post_memfault_data();
}

static enum smf_state_result connected_run(void *obj)
{
	struct cloud_state_object *state_object = obj;

	if (state_object->chan == &network_chan) {
		const struct network_msg *msg =
			(const struct network_msg *)state_object->msg_buf;

		if (msg->type == NETWORK_DISCONNECTED) {
			publish_cloud_msg(CLOUD_DISCONNECTED, NULL, 0);
			smf_set_state(SMF_CTX(state_object),
				      &states[STATE_DISCONNECTED]);
		}

		return SMF_EVENT_HANDLED;
	}

	if (state_object->chan == &cloud_chan) {
		const struct cloud_msg *msg = (const struct cloud_msg *)state_object->msg_buf;
		int err;

		if (msg->type != CLOUD_SEND_MESSAGE) {
			return SMF_EVENT_HANDLED;
		}

		err = nrf_cloud_coap_json_message_send(msg->payload, false, true);
		if (err) {
			LOG_ERR("nrf_cloud_coap_json_message_send, error: %d", err);
			return SMF_EVENT_HANDLED;
		}

		publish_cloud_msg(CLOUD_MESSAGE_SENT, msg->payload, msg->payload_len);
	}

	return SMF_EVENT_HANDLED;
}

static void connected_exit(void *obj)
{
	ARG_UNUSED(obj);

	(void)nrf_cloud_coap_disconnect();
}

static const struct smf_state states[] = {
	[STATE_DISCONNECTED] = SMF_CREATE_STATE(disconnected_entry,
						disconnected_run, NULL, NULL, NULL),
	[STATE_CONNECTING] = SMF_CREATE_STATE(connecting_entry,
					      connecting_run, NULL, NULL, NULL),
	[STATE_CONNECTED] = SMF_CREATE_STATE(connected_entry,
					     connected_run, connected_exit, NULL, NULL),
};

static void cloud_wdt_callback(int channel_id, void *user_data)
{
	LOG_ERR("Cloud watchdog expired, channel: %d, thread: %s",
		channel_id, k_thread_name_get((k_tid_t)user_data));

	FATAL_ERROR_WATCHDOG_TIMEOUT();
}

static void cloud_module(void)
{
	int err;
	int task_wdt_id;
	const uint32_t wdt_timeout_ms =
		(CONFIG_APP_CLOUD_WATCHDOG_TIMEOUT_SECONDS * MSEC_PER_SEC);
	const uint32_t execution_time_ms =
		(CONFIG_APP_CLOUD_MSG_PROCESSING_TIMEOUT_SECONDS * MSEC_PER_SEC);
	const k_timeout_t zbus_wait = K_MSEC(wdt_timeout_ms - execution_time_ms);

	err = settings_subsys_init();
	if (err) {
		LOG_ERR("settings_subsys_init, error: %d", err);
		FATAL_ERROR();
	}

	err = nrf_cloud_coap_init();
	if (err) {
		LOG_ERR("nrf_cloud_coap_init, error: %d", err);
		FATAL_ERROR();
	}

	task_wdt_id = task_wdt_add(wdt_timeout_ms, cloud_wdt_callback,
				   (void *)k_current_get());
	if (task_wdt_id < 0) {
		LOG_ERR("Failed to add task to watchdog: %d", task_wdt_id);
		FATAL_ERROR();
	}

	smf_set_initial(SMF_CTX(&cloud_state), &states[STATE_DISCONNECTED]);

	while (true) {
		err = task_wdt_feed(task_wdt_id);
		if (err) {
			LOG_ERR("task_wdt_feed, error: %d", err);
			FATAL_ERROR();
		}

		err = zbus_sub_wait_msg(&cloud, &cloud_state.chan,
					cloud_state.msg_buf, zbus_wait);
		if (err == -ENOMSG) {
			continue;
		} else if (err) {
			LOG_ERR("zbus_sub_wait_msg, error: %d", err);
			FATAL_ERROR();
		}

		err = smf_run_state(SMF_CTX(&cloud_state));
		if (err) {
			LOG_ERR("smf_run_state(), error: %d", err);
			FATAL_ERROR();
		}
	}
}

K_THREAD_DEFINE(cloud_module_thread, CONFIG_APP_CLOUD_THREAD_STACK_SIZE,
		cloud_module, NULL, NULL, NULL, 3, 0, 0);

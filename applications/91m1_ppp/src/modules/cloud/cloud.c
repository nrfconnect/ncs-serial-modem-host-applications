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
#include <zephyr/task_wdt/task_wdt.h>
#include <zephyr/zbus/zbus.h>
#include <date_time.h>
#include <zephyr/net/coap.h>
#include <net/nrf_cloud.h>
#include <net/nrf_cloud_coap.h>
#include <memfault/metrics/metrics.h>
#include <memfault/ports/zephyr/http.h>

#include "app_common.h"
#include "cloud.h"
#if defined(CONFIG_APP_LOCATION)
#include "cloud_location.h"
#endif /* CONFIG_APP_LOCATION */

LOG_MODULE_REGISTER(cloud, CONFIG_APP_CLOUD_LOG_LEVEL);

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

enum priv_cloud_msg_type {
	/* Make one attempt at establishing the cloud connection. */
	CLOUD_PRIV_CONNECT_ATTEMPT,
};

struct priv_cloud_msg {
	enum priv_cloud_msg_type type;
};

ZBUS_CHAN_DEFINE(priv_cloud_chan,
		 struct priv_cloud_msg,
		 NULL,
		 NULL,
		 ZBUS_OBSERVERS_EMPTY,
		 ZBUS_MSG_INIT(0)
);

#define CHANNEL_LIST(X)							\
	X(cloud_chan,		struct cloud_msg)			\
	X(priv_cloud_chan,	struct priv_cloud_msg)

#define MAX_MSG_SIZE			MAX_MSG_SIZE_FROM_LIST(CHANNEL_LIST)

#define ADD_OBSERVERS(_chan, _type)	ZBUS_CHAN_ADD_OBS(_chan, cloud, 0);

CHANNEL_LIST(ADD_OBSERVERS)

enum cloud_state {
	STATE_DISCONNECTED,
	STATE_CONNECTING,
	STATE_CONNECTED,
};

struct cloud_state_object {
	struct smf_ctx ctx;
	const struct zbus_channel *chan;
	uint8_t msg_buf[MAX_MSG_SIZE];
};

static struct cloud_state_object cloud_state;
static const struct smf_state states[];

static void cloud_msg_publish(enum cloud_msg_type type, const char *payload,
			      size_t payload_len)
{
	int err;
	struct cloud_msg msg = {
		.type = type
	};

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

static void priv_cloud_msg_publish(enum priv_cloud_msg_type type)
{
	int err;
	struct priv_cloud_msg msg = {
		.type = type
	};

	err = zbus_chan_pub(&priv_cloud_chan, &msg, PUB_TIMEOUT);
	if (err) {
		LOG_ERR("zbus_chan_pub priv_cloud_chan, error: %d", err);
		FATAL_ERROR();
	}
}

static void connect_retry_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	priv_cloud_msg_publish(CLOUD_PRIV_CONNECT_ATTEMPT);
}

static K_WORK_DELAYABLE_DEFINE(connect_retry_work, connect_retry_work_handler);

static void connect_retry_schedule(void)
{
	int err;

	err = k_work_schedule(&connect_retry_work, CREDENTIAL_RETRY);
	if (err < 0) {
		LOG_ERR("k_work_schedule, error: %d", err);
		FATAL_ERROR();
	}
}

static void date_time_event_handler(const struct date_time_evt *evt)
{
	switch (evt->type) {
	case DATE_TIME_OBTAINED_MODEM:
	case DATE_TIME_OBTAINED_NTP:
	case DATE_TIME_OBTAINED_EXT:
		priv_cloud_msg_publish(CLOUD_PRIV_CONNECT_ATTEMPT);
		break;
	default:
		break;
	}
}

static bool credentials_ready(void)
{
	int err;
	struct nrf_cloud_credentials_status cs;

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

static void cloud_shadow_poll(void)
{
	int err;
	char buf[CONFIG_APP_CLOUD_SHADOW_MAX_LEN];
	size_t buf_len = sizeof(buf);

	err = nrf_cloud_coap_shadow_get(buf, &buf_len, false, COAP_CONTENT_FORMAT_APP_JSON);
	if (err < 0) {
		if (err == -E2BIG) {
			LOG_WRN("nrf_cloud_coap_shadow_get, shadow truncated (%zu bytes)", buf_len);
			LOG_DBG("Device shadow desired config: %.*s", (int)buf_len, buf);
		} else {
			LOG_ERR("nrf_cloud_coap_shadow_get, error: %d", err);
		}

		goto done;
	}

	if (err > 0) {
		LOG_ERR("nrf_cloud_coap_shadow_get, CoAP error: %d", err);
		goto done;
	}

	if (buf_len == 0) {
		LOG_DBG("Device shadow desired config is empty");
	} else {
		LOG_DBG("Device shadow desired config: %.*s", (int)buf_len, buf);
	}

done:
	cloud_msg_publish(CLOUD_SHADOW_POLLED, NULL, 0);
}

static void connect_attempt(void)
{
	int err;
	char device_id[NRF_CLOUD_CLIENT_ID_MAX_LEN + 1];

	if (!credentials_ready()) {
		connect_retry_schedule();
		return;
	}

	if (!date_time_is_valid()) {
		LOG_DBG("Waiting for valid date/time");

		/* The date/time handler asks for a new attempt as soon as time arrives,
		 * ahead of the scheduled retry.
		 */
		err = date_time_update_async(date_time_event_handler);
		if (err) {
			LOG_WRN("date_time_update_async, error: %d", err);
		}

		connect_retry_schedule();
		return;
	}

	err = nrf_cloud_client_id_get(device_id, sizeof(device_id));
	if (err) {
		LOG_ERR("nrf_cloud_client_id_get, error: %d", err);
		connect_retry_schedule();
		return;
	}

	LOG_DBG("nRF Cloud client ID: %s", device_id);

	err = nrf_cloud_coap_connect(NULL);
	if (err) {
		LOG_ERR("nrf_cloud_coap_connect, error: %d", err);
		connect_retry_schedule();
		return;
	}

	LOG_DBG("Connected to nRF Cloud");

	cloud_msg_publish(CLOUD_CONNECTED, NULL, 0);
}

static void state_disconnected_entry(void *obj)
{
	ARG_UNUSED(obj);

	LOG_DBG("Cloud module disconnected");
}

static enum smf_state_result state_disconnected_run(void *obj)
{
	struct cloud_state_object *state_object = obj;
	const struct cloud_msg *msg = (const struct cloud_msg *)state_object->msg_buf;

	if (state_object->chan != &cloud_chan) {
		return SMF_EVENT_PROPAGATE;
	}

	if (msg->type == CLOUD_CONNECT) {
		smf_set_state(SMF_CTX(state_object), &states[STATE_CONNECTING]);
	}

	return SMF_EVENT_HANDLED;
}

static void state_connecting_entry(void *obj)
{
	ARG_UNUSED(obj);

	LOG_DBG("Cloud module connecting");

	priv_cloud_msg_publish(CLOUD_PRIV_CONNECT_ATTEMPT);
}

static enum smf_state_result state_connecting_run(void *obj)
{
	struct cloud_state_object *state_object = obj;

	if (state_object->chan == &priv_cloud_chan) {
		const struct priv_cloud_msg *msg =
			(const struct priv_cloud_msg *)state_object->msg_buf;

		if (msg->type == CLOUD_PRIV_CONNECT_ATTEMPT) {
			connect_attempt();
		}

		return SMF_EVENT_HANDLED;
	}

	if (state_object->chan == &cloud_chan) {
		const struct cloud_msg *msg = (const struct cloud_msg *)state_object->msg_buf;

		if (msg->type == CLOUD_DISCONNECT) {
			(void)nrf_cloud_coap_disconnect();
			cloud_msg_publish(CLOUD_DISCONNECTED, NULL, 0);
			smf_set_state(SMF_CTX(state_object), &states[STATE_DISCONNECTED]);
		} else if (msg->type == CLOUD_CONNECTED) {
			smf_set_state(SMF_CTX(state_object), &states[STATE_CONNECTED]);
		}

		return SMF_EVENT_HANDLED;
	}

	return SMF_EVENT_PROPAGATE;
}

static void state_connecting_exit(void *obj)
{
	ARG_UNUSED(obj);

	(void)k_work_cancel_delayable(&connect_retry_work);
}

static void memfault_data_post(void)
{
	int err;

	memfault_metrics_heartbeat_debug_trigger();

	err = memfault_zephyr_port_post_data();
	if (err) {
		LOG_WRN("memfault_zephyr_port_post_data, error: %d", err);
		return;
	}

	LOG_DBG("Memfault data posted");
}

static void state_connected_entry(void *obj)
{
	ARG_UNUSED(obj);

	LOG_DBG("Cloud module connected");
}

static enum smf_state_result state_connected_run(void *obj)
{
	int err;
	struct cloud_state_object *state_object = obj;
	const struct cloud_msg *msg = (const struct cloud_msg *)state_object->msg_buf;

	if (state_object->chan != &cloud_chan) {
		return SMF_EVENT_PROPAGATE;
	}

	switch (msg->type) {
	case CLOUD_DISCONNECT:
		cloud_msg_publish(CLOUD_DISCONNECTED, NULL, 0);
		smf_set_state(SMF_CTX(state_object), &states[STATE_DISCONNECTED]);

		return SMF_EVENT_HANDLED;
	case CLOUD_SHADOW_POLL_REQUEST:
		cloud_shadow_poll();

		return SMF_EVENT_HANDLED;
	case CLOUD_MEMFAULT_POST_REQUEST:
		memfault_data_post();

		return SMF_EVENT_HANDLED;
	case CLOUD_MESSAGE_SEND:
		err = nrf_cloud_coap_json_message_send(msg->payload, false, true);
		if (err) {
			LOG_ERR("nrf_cloud_coap_json_message_send, error: %d", err);
		}

		return SMF_EVENT_HANDLED;
#if defined(CONFIG_APP_LOCATION)
	case CLOUD_LOCATION_REQUEST:
		cloud_location_request_handle(&msg->location_request);

		return SMF_EVENT_HANDLED;
#endif /* CONFIG_APP_LOCATION */
	default:
		return SMF_EVENT_PROPAGATE;
	}
}

static void state_connected_exit(void *obj)
{
	ARG_UNUSED(obj);

	(void)nrf_cloud_coap_disconnect();
}

static const struct smf_state states[] = {
	[STATE_DISCONNECTED] = SMF_CREATE_STATE(state_disconnected_entry,
						state_disconnected_run, NULL, NULL, NULL),
	[STATE_CONNECTING] = SMF_CREATE_STATE(state_connecting_entry, state_connecting_run,
					      state_connecting_exit, NULL, NULL),
	[STATE_CONNECTED] = SMF_CREATE_STATE(state_connected_entry,
					     state_connected_run, state_connected_exit, NULL, NULL),
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

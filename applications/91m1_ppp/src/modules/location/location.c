/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/smf.h>
#include <zephyr/task_wdt/task_wdt.h>
#include <zephyr/zbus/zbus.h>
#include <modem/location.h>
#include <net/wifi_location_common.h>

#include "app_common.h"
#include "location.h"

LOG_MODULE_REGISTER(location_module, CONFIG_APP_LOCATION_LOG_LEVEL);

BUILD_ASSERT(CONFIG_APP_LOCATION_WATCHDOG_TIMEOUT_SECONDS >
	     CONFIG_APP_LOCATION_MSG_PROCESSING_TIMEOUT_SECONDS,
	     "Watchdog timeout must be greater than maximum message processing time");

BUILD_ASSERT(CONFIG_APP_LOCATION_WIFI_APS_MAX >=
	     CONFIG_LOCATION_METHOD_WIFI_SCANNING_RESULTS_MAX_CNT,
	     "Wi-Fi AP storage must fit all Wi-Fi scanning results");

ZBUS_CHAN_DEFINE(location_chan,
		 struct location_msg,
		 NULL,
		 NULL,
		 ZBUS_OBSERVERS_EMPTY,
		 ZBUS_MSG_INIT(0));

ZBUS_MSG_SUBSCRIBER_DEFINE(location);

ZBUS_CHAN_ADD_OBS(location_chan, location, 0);

/* Location module states */
enum location_module_state {
	/* The module is running */
	STATE_RUNNING,
		/* Location search is inactive. */
		STATE_LOCATION_SEARCH_INACTIVE,
		/* Location search is active. */
		STATE_LOCATION_SEARCH_ACTIVE,
};

/* State object.
 * Used to transfer context data between state changes.
 */
struct location_state_object {
	/* This must be first */
	struct smf_ctx ctx;

	/* Last channel type that a message was received on */
	const struct zbus_channel *chan;

	/* Last received message */
	uint8_t msg_buf[sizeof(struct location_msg)];
};

/* Forward declarations */
static void location_event_handler(const struct location_event_data *event_data);
static void state_running_entry(void *obj);
static void state_location_search_inactive_entry(void *obj);
static enum smf_state_result state_location_search_inactive_run(void *obj);
static void state_location_search_active_entry(void *obj);
static enum smf_state_result state_location_search_active_run(void *obj);

static const struct smf_state states[] = {
	[STATE_RUNNING] =
		SMF_CREATE_STATE(state_running_entry,
				 NULL,
				 NULL,
				 NULL,
				 &states[STATE_LOCATION_SEARCH_INACTIVE]),
	[STATE_LOCATION_SEARCH_INACTIVE] =
		SMF_CREATE_STATE(state_location_search_inactive_entry,
				 state_location_search_inactive_run,
				 NULL,
				 &states[STATE_RUNNING],
				 NULL),
	[STATE_LOCATION_SEARCH_ACTIVE] =
		SMF_CREATE_STATE(state_location_search_active_entry,
				 state_location_search_active_run,
				 NULL,
				 &states[STATE_RUNNING],
				 NULL),
};

static struct location_state_object location_state;

static void message_send(enum location_msg_type msg_type)
{
	struct location_msg location_msg = { .type = msg_type };
	int err;

	err = zbus_chan_pub(&location_chan, &location_msg, PUB_TIMEOUT);
	if (err) {
		LOG_ERR("zbus_chan_pub, error: %d", err);
		FATAL_ERROR();
	}
}

static int wifi_data_copy(struct location_cloud_request_data *dest,
			  const struct wifi_scan_info *src)
{
	if ((src == NULL) || (src->ap_info == NULL) || (src->cnt == 0)) {
		LOG_ERR("Invalid Wi-Fi scan info");
		return -EINVAL;
	}

	if (src->cnt > ARRAY_SIZE(dest->wifi_aps)) {
		LOG_ERR("Not enough memory for %d Wi-Fi access points", src->cnt);
		return -ENOMEM;
	}

	for (uint16_t i = 0; i < src->cnt; i++) {
		dest->wifi_aps[i].rssi = src->ap_info[i].rssi;
		memcpy(dest->wifi_aps[i].mac, src->ap_info[i].mac, MAC_ADDR_LEN);
		dest->wifi_aps[i].mac_length = src->ap_info[i].mac_length;
	}

	dest->wifi_cnt = src->cnt;

	LOG_DBG("Copied %d Wi-Fi APs", dest->wifi_cnt);

	return 0;
}

static void cloud_request_send(const struct location_data_cloud *cloud_request)
{
	struct location_msg location_msg = { .type = LOCATION_CLOUD_REQUEST };
	int err;

	err = wifi_data_copy(&location_msg.cloud_request, cloud_request->wifi_data);
	if (err) {
		LOG_ERR("wifi_data_copy, error: %d", err);
		return;
	}

	err = zbus_chan_pub(&location_chan, &location_msg, PUB_TIMEOUT);
	if (err) {
		LOG_ERR("zbus_chan_pub, error: %d", err);
		FATAL_ERROR();
	}
}

/* State handlers */

static void state_running_entry(void *obj)
{
	ARG_UNUSED(obj);

	int err;

	LOG_DBG("%s", __func__);

	err = location_init(location_event_handler);
	if (err) {
		LOG_ERR("location_init, error: %d", err);
		FATAL_ERROR();
		return;
	}

	LOG_DBG("Location library initialized");

	message_send(LOCATION_MODULE_READY);
}

static void state_location_search_inactive_entry(void *obj)
{
	ARG_UNUSED(obj);

	LOG_DBG("%s", __func__);
}

static enum smf_state_result state_location_search_inactive_run(void *obj)
{
	struct location_state_object *state_object = obj;
	int err;

	if (state_object->chan == &location_chan) {
		const struct location_msg *msg =
			(const struct location_msg *)state_object->msg_buf;

		if (msg->type == LOCATION_SEARCH_CANCEL) {
			LOG_DBG("Location search cancel received in inactive state, ignoring");
		} else if (msg->type == LOCATION_SEARCH_TRIGGER) {
			LOG_DBG("Location search trigger received");

			err = location_request(NULL);
			if (err) {
				LOG_WRN("location_request, error: %d", err);

				return SMF_EVENT_HANDLED;
			}

			smf_set_state(SMF_CTX(state_object), &states[STATE_LOCATION_SEARCH_ACTIVE]);

			return SMF_EVENT_HANDLED;
		}
	}

	return SMF_EVENT_PROPAGATE;
}

static void state_location_search_active_entry(void *obj)
{
	ARG_UNUSED(obj);

	LOG_DBG("%s", __func__);
}

static enum smf_state_result state_location_search_active_run(void *obj)
{
	struct location_state_object *state_object = obj;
	int err;

	if (state_object->chan == &location_chan) {
		const struct location_msg *msg =
			(const struct location_msg *)state_object->msg_buf;

		if (msg->type == LOCATION_SEARCH_TRIGGER) {
			LOG_DBG("Location trigger received while active, ignoring");
		} else if (msg->type == LOCATION_SEARCH_CANCEL) {
			LOG_DBG("Location search cancel received, cancelling location request");

			err = location_request_cancel();
			if (err) {
				LOG_ERR("location_request_cancel, error: %d", err);
			}

			/* The location library only emits LOCATION_EVT_CANCELLED when
			 * CONFIG_LOCATION_DATA_DETAILS is enabled, which it cannot be here,
			 * so the search is completed from this context instead.
			 */
			message_send(LOCATION_SEARCH_DONE);
		} else if (msg->type == LOCATION_SEARCH_DONE) {
			LOG_DBG("Location search done message received, going to inactive state");

			smf_set_state(SMF_CTX(state_object),
				      &states[STATE_LOCATION_SEARCH_INACTIVE]);

			return SMF_EVENT_HANDLED;
		}
	}

	return SMF_EVENT_PROPAGATE;
}

#if defined(CONFIG_LOCATION_DATA_DETAILS)
static void location_print_data_details(enum location_method method,
					const struct location_data_details *details)
{
	LOG_DBG("Elapsed method time: %d ms", details->elapsed_time_method);

	if (method == LOCATION_METHOD_WIFI) {
		LOG_DBG("Wi-Fi APs: %d", details->wifi.ap_count);
	}
}
#endif /* CONFIG_LOCATION_DATA_DETAILS */

static void location_event_handler(const struct location_event_data *event_data)
{
	switch (event_data->id) {
	case LOCATION_EVT_LOCATION:
		LOG_DBG("Got location: lat: %f, lon: %f, acc: %f, method: %s",
			(double)event_data->location.latitude,
			(double)event_data->location.longitude,
			(double)event_data->location.accuracy,
			location_method_str(event_data->method));

		message_send(LOCATION_SEARCH_DONE);
		break;
	case LOCATION_EVT_STARTED:
		message_send(LOCATION_SEARCH_STARTED);
		break;
	case LOCATION_EVT_TIMEOUT:
		LOG_DBG("Getting location timed out");
		message_send(LOCATION_SEARCH_DONE);
		break;
	case LOCATION_EVT_ERROR:
		LOG_WRN("Location request failed:");
		LOG_WRN("Used method: %s (%d)", location_method_str(event_data->method),
						event_data->method);

#if defined(CONFIG_LOCATION_DATA_DETAILS)
		location_print_data_details(event_data->method, &event_data->error.details);
#endif /* CONFIG_LOCATION_DATA_DETAILS */

		message_send(LOCATION_SEARCH_DONE);
		break;
	case LOCATION_EVT_FALLBACK:
		LOG_DBG("Location request fallback has occurred:");
		LOG_DBG("Failed method: %s (%d)", location_method_str(event_data->method),
						  event_data->method);
#if defined(CONFIG_LOCATION_DATA_DETAILS)
		LOG_DBG("New method: %s (%d)",
			location_method_str(event_data->fallback.next_method),
			event_data->fallback.next_method);
		LOG_DBG("Cause: %s",
			(event_data->fallback.cause == LOCATION_EVT_TIMEOUT) ? "timeout" :
			(event_data->fallback.cause == LOCATION_EVT_ERROR) ? "error" :
			"unknown");

		location_print_data_details(event_data->method, &event_data->fallback.details);
#endif /* CONFIG_LOCATION_DATA_DETAILS */
		break;
	case LOCATION_EVT_CLOUD_LOCATION_EXT_REQUEST:
		LOG_DBG("Cloud location request received from location library");

		cloud_request_send(&event_data->cloud_location_request);

		/* Cancel the current location request to avoid falling back to the next
		 * location source. Treat the fact that we have found Wi-Fi APs as a successful
		 * location request, even if we don't know whether the cloud is able to resolve
		 * data to a location or not.
		 */
		message_send(LOCATION_SEARCH_CANCEL);
		break;
	case LOCATION_EVT_RESULT_UNKNOWN:
		LOG_DBG("Location result unknown");
		message_send(LOCATION_SEARCH_DONE);
		break;
	case LOCATION_EVT_CANCELLED:
		LOG_DBG("Location request cancelled");
		message_send(LOCATION_SEARCH_DONE);
		break;
	default:
		LOG_DBG("Getting location: Unknown event %d", event_data->id);
		break;
	}
}

static void location_wdt_callback(int channel_id, void *user_data)
{
	LOG_ERR("Location watchdog expired, channel: %d, thread: %s",
		channel_id, k_thread_name_get((k_tid_t)user_data));

	FATAL_ERROR_WATCHDOG_TIMEOUT();
}

static void location_module(void)
{
	int err;
	int task_wdt_id;
	const uint32_t wdt_timeout_ms =
		(CONFIG_APP_LOCATION_WATCHDOG_TIMEOUT_SECONDS * MSEC_PER_SEC);
	const uint32_t execution_time_ms =
		(CONFIG_APP_LOCATION_MSG_PROCESSING_TIMEOUT_SECONDS * MSEC_PER_SEC);
	const k_timeout_t zbus_wait = K_MSEC(wdt_timeout_ms - execution_time_ms);

	task_wdt_id = task_wdt_add(wdt_timeout_ms, location_wdt_callback, (void *)k_current_get());
	if (task_wdt_id < 0) {
		LOG_ERR("Failed to add task to watchdog: %d", task_wdt_id);
		FATAL_ERROR();
	}

	smf_set_initial(SMF_CTX(&location_state), &states[STATE_RUNNING]);

	while (true) {
		err = task_wdt_feed(task_wdt_id);
		if (err) {
			LOG_ERR("task_wdt_feed, error: %d", err);
			FATAL_ERROR();
		}

		err = zbus_sub_wait_msg(&location, &location_state.chan,
					location_state.msg_buf, zbus_wait);
		if (err == -ENOMSG) {
			continue;
		} else if (err) {
			LOG_ERR("zbus_sub_wait_msg, error: %d", err);
			FATAL_ERROR();
		}

		err = smf_run_state(SMF_CTX(&location_state));
		if (err) {
			LOG_ERR("smf_run_state(), error: %d", err);
			FATAL_ERROR();
		}
	}
}

K_THREAD_DEFINE(location_module_thread, CONFIG_APP_LOCATION_THREAD_STACK_SIZE,
		location_module, NULL, NULL, NULL, K_LOWEST_APPLICATION_THREAD_PRIO, 0, 0);

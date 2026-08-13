/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/task_wdt/task_wdt.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/smf.h>
#include <string.h>
#include <memfault/ports/zephyr/http.h>
#include <net/nrf_cloud_coap.h>
#include <date_time.h>

#include "app_common.h"
#include "cloud.h"
#if defined(CONFIG_APP_LOCATION)
#include <net/nrf_cloud_location.h>
#include <net/wifi_location_common.h>
#include <modem/lte_lc.h>
#include <zephyr/net/wifi_mgmt.h>
#include "modules/location/location.h"
#define LOCATION_CHANNEL(X) X(location_chan, struct location_msg)
#else
#define LOCATION_CHANNEL(X)
#endif

LOG_MODULE_REGISTER(cloud, CONFIG_APP_CLOUD_LOG_LEVEL);

BUILD_ASSERT(CONFIG_APP_CLOUD_WATCHDOG_TIMEOUT_SECONDS >
	     CONFIG_APP_CLOUD_MSG_PROCESSING_TIMEOUT_SECONDS,
	     "Watchdog timeout must be greater than maximum message processing time");

ZBUS_CHAN_DEFINE(cloud_chan,
		 struct cloud_msg,
		 NULL,
		 NULL,
		 ZBUS_OBSERVERS_EMPTY,
		 ZBUS_MSG_INIT(0));

ZBUS_MSG_SUBSCRIBER_DEFINE(cloud_subscriber);

/* Private channel message types for internal state management. */
enum priv_cloud_msg_type {
	/* date_time_update_async() completed. */
	CLOUD_PRIV_TIME_READY,
	/* date_time_update_async() could not obtain a valid time. */
	CLOUD_PRIV_TIME_NOT_OBTAINED,
	/* nrf_cloud_coap_connect() succeeded. */
	CLOUD_PRIV_SESSION_READY,
	/* Connect failed, or a request on an established session failed. */
	CLOUD_PRIV_SESSION_FAILED,
};

struct priv_cloud_msg {
	enum priv_cloud_msg_type type;
};

/* Create the private cloud channel for internal messaging that is not intended for
 * external use.
 */
ZBUS_CHAN_DEFINE(priv_cloud_chan,
		 struct priv_cloud_msg,
		 NULL,
		 NULL,
		 ZBUS_OBSERVERS_EMPTY,
		 ZBUS_MSG_INIT(0));

#define CHANNEL_LIST(X) \
	X(cloud_chan, struct cloud_msg) \
	X(priv_cloud_chan, struct priv_cloud_msg) \
	LOCATION_CHANNEL(X)

#define MAX_MSG_SIZE MAX_MSG_SIZE_FROM_LIST(CHANNEL_LIST)

#define ADD_OBSERVERS(_chan, _type) ZBUS_CHAN_ADD_OBS(_chan, cloud_subscriber, 0);

CHANNEL_LIST(ADD_OBSERVERS)

/* State machine */

enum cloud_module_state {
	/* One-time setup: initialise the CoAP library and wait for a valid clock. */
	STATE_INIT,
	/* No CoAP session, waiting for a sync or ground-fix request. */
	STATE_DISCONNECTED,
	/* Establishing the CoAP session. */
	STATE_CONNECTING,
	/* CoAP session established, requests are served immediately. */
	STATE_CONNECTED,
};

/* A request that arrived before the CoAP session was ready, deferred until it
 * connects. Only one request can be pending at a time.
 */
enum pending_request {
	PENDING_NONE,
	PENDING_DIAGNOSTICS,
#if defined(CONFIG_APP_LOCATION)
	PENDING_GROUND_FIX,
#endif
};

struct cloud_state_object {
	/* This must be first */
	struct smf_ctx ctx;

	/* Last channel type that a message was received on */
	const struct zbus_channel *chan;

	/* Buffer for last zbus message */
	uint8_t msg_buf[MAX_MSG_SIZE];

	enum pending_request pending;

#if defined(CONFIG_APP_LOCATION)
	struct location_msg pending_location;
#endif
};

/* Forward declarations of state handlers */
static void state_init_entry(void *obj);
static enum smf_state_result state_disconnected_run(void *obj);
static void state_connecting_entry(void *obj);
static enum smf_state_result state_connecting_run(void *obj);
static void state_connected_entry(void *obj);
static enum smf_state_result state_connected_run(void *obj);

static const struct smf_state states[] = {
	[STATE_INIT] =
		SMF_CREATE_STATE(state_init_entry,
				 NULL,
				 NULL,
				 NULL,
				 NULL),
	[STATE_DISCONNECTED] =
		SMF_CREATE_STATE(NULL,
				 state_disconnected_run,
				 NULL,
				 NULL,
				 NULL),
	[STATE_CONNECTING] =
		SMF_CREATE_STATE(state_connecting_entry,
				 state_connecting_run,
				 NULL,
				 NULL,
				 NULL),
	[STATE_CONNECTED] =
		SMF_CREATE_STATE(state_connected_entry,
				 state_connected_run,
				 NULL,
				 NULL,
				 NULL),
};

/* Helpers */

static void publish_priv_cloud(enum priv_cloud_msg_type type)
{
	struct priv_cloud_msg msg = { .type = type };
	int err = zbus_chan_pub(&priv_cloud_chan, &msg, PUB_TIMEOUT);

	if (err) {
		LOG_ERR("zbus_chan_pub priv_cloud_chan, error: %d", err);
		FATAL_ERROR();
	}
}

static void publish_sync_done(void)
{
	struct cloud_msg msg = { .type = CLOUD_SYNC_DONE };
	int err = zbus_chan_pub(&cloud_chan, &msg, PUB_TIMEOUT);

	if (err) {
		LOG_ERR("zbus_chan_pub cloud_chan, error: %d", err);
		FATAL_ERROR();
	}
}

static void date_time_evt_handler(const struct date_time_evt *evt)
{
	switch (evt->type) {
	case DATE_TIME_OBTAINED_MODEM:
	case DATE_TIME_OBTAINED_NTP:
	case DATE_TIME_OBTAINED_EXT:
		publish_priv_cloud(CLOUD_PRIV_TIME_READY);
		break;
	case DATE_TIME_NOT_OBTAINED:
		publish_priv_cloud(CLOUD_PRIV_TIME_NOT_OBTAINED);
		break;
	}
}

static int upload_diagnostics(void)
{
	int err = memfault_zephyr_port_post_data();

	if (err) {
		LOG_WRN("memfault_zephyr_port_post_data, error: %d", err);
		return err;
	}

	LOG_INF("Diagnostics uploaded");

	return 0;
}

#if defined(CONFIG_APP_LOCATION)
static const char *fix_type_str(enum nrf_cloud_location_type type)
{
	switch (type) {
	case LOCATION_TYPE_SINGLE_CELL:
		return "SCELL";
	case LOCATION_TYPE_MULTI_CELL:
		return "MCELL";
	case LOCATION_TYPE_WIFI:
		return "WIFI";
	default:
		return "?";
	}
}

static int ground_fix(const struct location_msg *msg)
{
	struct lte_lc_cells_info cells = {
		.current_cell = {
			.mcc = msg->cell.mcc,
			.mnc = msg->cell.mnc,
			.id = msg->cell.eci,
			.tac = msg->cell.tac,
			.earfcn = msg->cell.earfcn,
			.phys_cell_id = (uint16_t)msg->cell.pci,
			.timing_advance = LTE_LC_CELL_TIMING_ADVANCE_INVALID,
			/* Codec applies RSRP_IDX_TO_DBM(); convert dBm back to the index. */
			.rsrp = msg->cell.valid
					? (int16_t)CLAMP(msg->cell.rsrp + 141, 0, 97)
					: LTE_LC_CELL_RSRP_INVALID,
		},
	};
	static struct wifi_scan_result coap_aps[CONFIG_APP_LOCATION_MAX_WIFI_APS];
	struct wifi_scan_info wifi = { .ap_info = coap_aps, .cnt = 0 };
	struct nrf_cloud_location_config config = { .do_reply = true, .fallback = true };
	struct nrf_cloud_coap_location_request req = { .config = &config };
	struct nrf_cloud_location_result result;
	int err;

	for (uint8_t i = 0; i < msg->ap_count; i++) {
		memcpy(coap_aps[wifi.cnt].mac, msg->aps[i].mac, WIFI_MAC_ADDR_LEN);
		coap_aps[wifi.cnt].mac_length = WIFI_MAC_ADDR_LEN;
		coap_aps[wifi.cnt].channel = msg->aps[i].channel;
		coap_aps[wifi.cnt].rssi = msg->aps[i].rssi;
		wifi.cnt++;
	}

	req.cell_info = msg->cell.valid ? &cells : NULL;
	req.wifi_info = (wifi.cnt >= NRF_CLOUD_LOCATION_WIFI_AP_CNT_MIN) ? &wifi : NULL;

	if (req.cell_info == NULL && req.wifi_info == NULL) {
		LOG_WRN("No usable measurements for ground-fix");
		return 0;
	}

	err = nrf_cloud_coap_location_get(&req, &result);
	if (err) {
		LOG_WRN("nrf_cloud_coap_location_get, error: %d", err);
		return err;
	}

	LOG_INF("Location: %.7f,%.7f Uncertainty: %um Type: %s",
		result.lat, result.lon, result.unc, fix_type_str(result.type));

	return 0;
}
#endif /* CONFIG_APP_LOCATION */

/* State handlers */

static void state_init_entry(void *obj)
{
	int err;

	LOG_DBG("%s", __func__);

	err = nrf_cloud_coap_init();
	if (err) {
		LOG_ERR("nrf_cloud_coap_init, error: %d", err);
		FATAL_ERROR();

		return;
	}

	smf_set_state(SMF_CTX(obj), &states[STATE_DISCONNECTED]);
}

static enum smf_state_result state_disconnected_run(void *obj)
{
	struct cloud_state_object *state_object = obj;

	if (state_object->chan == &cloud_chan) {
		const struct cloud_msg *msg = (const struct cloud_msg *)state_object->msg_buf;

		if (msg->type == CLOUD_SYNC_REQUEST) {
			state_object->pending = PENDING_DIAGNOSTICS;
			smf_set_state(SMF_CTX(state_object), &states[STATE_CONNECTING]);

			return SMF_EVENT_HANDLED;
		}
	}
#if defined(CONFIG_APP_LOCATION)
	else if (state_object->chan == &location_chan) {
		const struct location_msg *msg =
			(const struct location_msg *)state_object->msg_buf;

		if (msg->type == LOCATION_SYNC_DONE) {
			state_object->pending = PENDING_GROUND_FIX;
			state_object->pending_location = *msg;
			smf_set_state(SMF_CTX(state_object), &states[STATE_CONNECTING]);

			return SMF_EVENT_HANDLED;
		}
	}
#endif

	return SMF_EVENT_PROPAGATE;
}

static void state_connecting_entry(void *obj)
{
	int err;

	ARG_UNUSED(obj);

	LOG_DBG("%s", __func__);

	if (!date_time_is_valid()) {
		LOG_INF("Updating date/time for the CoAP JWT");
		date_time_update_async(date_time_evt_handler);

		return;
	}

	err = nrf_cloud_coap_connect(NULL);
	if (err) {
		LOG_ERR("nrf_cloud_coap_connect, error: %d", err);
		publish_priv_cloud(CLOUD_PRIV_SESSION_FAILED);

		return;
	}

	publish_priv_cloud(CLOUD_PRIV_SESSION_READY);
}

static enum smf_state_result state_connecting_run(void *obj)
{
	struct cloud_state_object *state_object = obj;

	if (state_object->chan == &priv_cloud_chan) {
		const struct priv_cloud_msg *msg =
			(const struct priv_cloud_msg *)state_object->msg_buf;

		switch (msg->type) {
		case CLOUD_PRIV_TIME_READY:
			smf_set_state(SMF_CTX(state_object), &states[STATE_CONNECTING]);

			return SMF_EVENT_HANDLED;
		case CLOUD_PRIV_SESSION_READY:
			smf_set_state(SMF_CTX(state_object), &states[STATE_CONNECTED]);

			return SMF_EVENT_HANDLED;
		case CLOUD_PRIV_TIME_NOT_OBTAINED:
		case CLOUD_PRIV_SESSION_FAILED:
			switch (state_object->pending) {
			case PENDING_DIAGNOSTICS:
				/* main is waiting on CLOUD_SYNC_DONE to advance, send it
				 * even though nothing was uploaded, or main stalls.
				 */
				publish_sync_done();
				break;
#if defined(CONFIG_APP_LOCATION)
			case PENDING_GROUND_FIX:
				LOG_WRN("No CoAP session, dropping ground-fix");
				break;
#endif
			case PENDING_NONE:
				break;
			}
			state_object->pending = PENDING_NONE;
			smf_set_state(SMF_CTX(state_object), &states[STATE_DISCONNECTED]);

			return SMF_EVENT_HANDLED;
		default:
			return SMF_EVENT_PROPAGATE;
		}
	}

	if (state_object->chan == &cloud_chan) {
		const struct cloud_msg *msg = (const struct cloud_msg *)state_object->msg_buf;

		if (msg->type == CLOUD_SYNC_REQUEST) {
			state_object->pending = PENDING_DIAGNOSTICS;

			return SMF_EVENT_HANDLED;
		}

		return SMF_EVENT_PROPAGATE;
	}

#if defined(CONFIG_APP_LOCATION)
	if (state_object->chan == &location_chan) {
		const struct location_msg *msg =
			(const struct location_msg *)state_object->msg_buf;

		if (msg->type == LOCATION_SYNC_DONE) {
			state_object->pending = PENDING_GROUND_FIX;
			state_object->pending_location = *msg;

			return SMF_EVENT_HANDLED;
		}

		return SMF_EVENT_PROPAGATE;
	}
#endif

	return SMF_EVENT_PROPAGATE;
}

static void state_connected_entry(void *obj)
{
	struct cloud_state_object *state_object = obj;
	enum pending_request pending = state_object->pending;

	LOG_DBG("%s", __func__);

	state_object->pending = PENDING_NONE;

	switch (pending) {
	case PENDING_DIAGNOSTICS:
		if (upload_diagnostics()) {
			publish_priv_cloud(CLOUD_PRIV_SESSION_FAILED);
		}

		publish_sync_done();
		break;
#if defined(CONFIG_APP_LOCATION)
	case PENDING_GROUND_FIX:
		if (ground_fix(&state_object->pending_location)) {
			publish_priv_cloud(CLOUD_PRIV_SESSION_FAILED);
		}
		break;
#endif
	case PENDING_NONE:
		break;
	}
}

static enum smf_state_result state_connected_run(void *obj)
{
	struct cloud_state_object *state_object = obj;

	if (state_object->chan == &cloud_chan) {
		const struct cloud_msg *msg = (const struct cloud_msg *)state_object->msg_buf;

		if (msg->type == CLOUD_SYNC_REQUEST) {
			if (upload_diagnostics()) {
				publish_priv_cloud(CLOUD_PRIV_SESSION_FAILED);
			}

			publish_sync_done();

			return SMF_EVENT_HANDLED;
		}
	} else if (state_object->chan == &priv_cloud_chan) {
		const struct priv_cloud_msg *msg =
			(const struct priv_cloud_msg *)state_object->msg_buf;

		if (msg->type == CLOUD_PRIV_SESSION_FAILED) {
			smf_set_state(SMF_CTX(state_object), &states[STATE_DISCONNECTED]);

			return SMF_EVENT_HANDLED;
		}
	}
#if defined(CONFIG_APP_LOCATION)
	else if (state_object->chan == &location_chan) {
		const struct location_msg *msg =
			(const struct location_msg *)state_object->msg_buf;

		if (msg->type == LOCATION_SYNC_DONE) {
			if (ground_fix(msg)) {
				publish_priv_cloud(CLOUD_PRIV_SESSION_FAILED);
			}

			return SMF_EVENT_HANDLED;
		}
	}
#endif

	return SMF_EVENT_PROPAGATE;
}

static void cloud_wdt_callback(int channel_id, void *user_data)
{
	LOG_ERR("Cloud watchdog expired, channel: %d, thread: %s",
		channel_id, k_thread_name_get((k_tid_t)user_data));

	FATAL_ERROR_WATCHDOG_TIMEOUT();
}

static void cloud_module_thread(void)
{
	int err;
	int task_wdt_id;
	static struct cloud_state_object cloud_state;
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

	task_wdt_id = task_wdt_add(wdt_timeout_ms, cloud_wdt_callback,
				   (void *)k_current_get());
	if (task_wdt_id < 0) {
		LOG_ERR("Failed to add task to watchdog: %d", task_wdt_id);
		FATAL_ERROR();
		return;
	}

	smf_set_initial(SMF_CTX(&cloud_state), &states[STATE_INIT]);

	while (true) {
		err = task_wdt_feed(task_wdt_id);
		if (err) {
			LOG_ERR("task_wdt_feed, error: %d", err);
			FATAL_ERROR();
			return;
		}

		err = zbus_sub_wait_msg(&cloud_subscriber, &cloud_state.chan,
					cloud_state.msg_buf, zbus_wait);
		if (err == -ENOMSG) {
			continue;
		} else if (err) {
			LOG_ERR("zbus_sub_wait_msg, error: %d", err);
			FATAL_ERROR();
			return;
		}

		err = smf_run_state(SMF_CTX(&cloud_state));
		if (err) {
			LOG_ERR("smf_run_state(), error: %d", err);
			FATAL_ERROR();
			return;
		}
	}
}

K_THREAD_DEFINE(cloud_module_thread_id, CONFIG_APP_CLOUD_THREAD_STACK_SIZE,
		cloud_module_thread, NULL, NULL, NULL, 3, 0, 0);

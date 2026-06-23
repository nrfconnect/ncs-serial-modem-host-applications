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

#include "app_common.h"
#include "modules/network/network.h"
#if defined(CONFIG_APP_LOCATION)
#include "modules/location/location.h"
#endif
#if defined(CONFIG_APP_BATTERY)
#include "modules/battery/battery.h"
#endif

LOG_MODULE_REGISTER(main, CONFIG_APP_MAIN_LOG_LEVEL);

BUILD_ASSERT(CONFIG_APP_MAIN_WATCHDOG_TIMEOUT_SECONDS >
	     CONFIG_APP_MAIN_MSG_PROCESSING_TIMEOUT_SECONDS,
	     "Watchdog timeout must exceed the maximum message processing time");

enum main_msg_type {
	MAIN_SYNC,
};

struct main_msg {
	enum main_msg_type type;
};

ZBUS_CHAN_DEFINE(main_chan, struct main_msg, NULL, NULL, ZBUS_OBSERVERS_EMPTY, ZBUS_MSG_INIT(0));
ZBUS_MSG_SUBSCRIBER_DEFINE(main_subscriber);

#define CHANNEL_LIST(X) \
	X(main_chan, struct main_msg) \
	X(network_chan, struct network_msg)

#define MAX_MSG_SIZE MAX_MSG_SIZE_FROM_LIST(CHANNEL_LIST)

#define ADD_OBSERVER(_chan, _type) ZBUS_CHAN_ADD_OBS(_chan, main_subscriber, 0);
CHANNEL_LIST(ADD_OBSERVER)

enum app_state {
	STATE_DISCONNECTED,
	STATE_CONNECTED,
};

struct app_object {
	struct smf_ctx ctx;
	const struct zbus_channel *chan;
	uint8_t msg_buf[MAX_MSG_SIZE];
};

static void sync_timer_fn(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(sync_timer, sync_timer_fn);

static void request_module_updates(void);
static enum smf_state_result disconnected_run(void *obj);
static void connected_entry(void *obj);
static enum smf_state_result connected_run(void *obj);
static void connected_exit(void *obj);
static void wdt_callback(int channel_id, void *user_data);

static const struct smf_state states[] = {
	[STATE_DISCONNECTED] = SMF_CREATE_STATE(NULL, disconnected_run, NULL, NULL, NULL),
	[STATE_CONNECTED] = SMF_CREATE_STATE(connected_entry, connected_run, connected_exit,
					     NULL, NULL),
};

static void sync_timer_fn(struct k_work *work)
{
	ARG_UNUSED(work);

	struct main_msg msg = { .type = MAIN_SYNC };
	int err;

	err = zbus_chan_pub(&main_chan, &msg, PUB_TIMEOUT);
	if (err) {
		LOG_ERR("zbus_chan_pub main_chan, error: %d", err);
	}
}

static void request_module_updates(void)
{
	int err;

	LOG_DBG("Requesting module updates");
#if defined(CONFIG_APP_LOCATION)
	struct location_msg loc_msg = { .type = LOCATION_FIX_REQUEST };

	err = zbus_chan_pub(&location_chan, &loc_msg, PUB_TIMEOUT);
	if (err) {
		LOG_ERR("zbus_chan_pub location_chan, error: %d", err);
	}
#endif
#if defined(CONFIG_APP_BATTERY)
	struct battery_msg bat_msg = { .type = BATTERY_SAMPLE };

	err = zbus_chan_pub(&battery_chan, &bat_msg, PUB_TIMEOUT);
	if (err) {
		LOG_ERR("zbus_chan_pub battery_chan, error: %d", err);
	}
#endif
	ARG_UNUSED(err);
}

static enum smf_state_result disconnected_run(void *obj)
{
	struct app_object *state = obj;

	if (state->chan == &network_chan) {
		const struct network_msg *msg = (const struct network_msg *)state->msg_buf;

		if (msg->type == NETWORK_CONNECTED) {
			smf_set_state(SMF_CTX(state), &states[STATE_CONNECTED]);
		}
	}

	return SMF_EVENT_HANDLED;
}

static void connected_entry(void *obj)
{
	ARG_UNUSED(obj);
	int err;

	LOG_INF("Connected");

	err = k_work_reschedule(&sync_timer, K_SECONDS(CONFIG_APP_SYNC_BOOT_DELAY_SECONDS));
	if (err < 0) {
		LOG_ERR("k_work_reschedule sync_timer, error: %d", err);
	}
}

static enum smf_state_result connected_run(void *obj)
{
	struct app_object *state = obj;

	if (state->chan == &network_chan) {
		const struct network_msg *msg = (const struct network_msg *)state->msg_buf;

		if (msg->type == NETWORK_DISCONNECTED) {
			smf_set_state(SMF_CTX(state), &states[STATE_DISCONNECTED]);
		}
	} else if (state->chan == &main_chan) {
		const struct main_msg *msg = (const struct main_msg *)state->msg_buf;

		if (msg->type == MAIN_SYNC) {
			int err;

			request_module_updates();
			err = k_work_reschedule(&sync_timer, K_SECONDS(CONFIG_APP_SYNC_INTERVAL));
			if (err < 0) {
				LOG_ERR("k_work_reschedule sync_timer, error: %d", err);
			}
		}
	}

	return SMF_EVENT_HANDLED;
}

static void connected_exit(void *obj)
{
	ARG_UNUSED(obj);
	(void)k_work_cancel_delayable(&sync_timer);
}

static void wdt_callback(int channel_id, void *user_data)
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
	static struct app_object app;

	task_wdt_id = task_wdt_add(wdt_timeout_ms, wdt_callback, (void *)k_current_get());
	if (task_wdt_id < 0) {
		LOG_ERR("task_wdt_add, error: %d", task_wdt_id);
		SEND_FATAL_ERROR();
		return -EFAULT;
	}

	smf_set_initial(SMF_CTX(&app), &states[STATE_DISCONNECTED]);

	while (true) {
		err = task_wdt_feed(task_wdt_id);
		if (err) {
			LOG_ERR("task_wdt_feed, error: %d", err);
			SEND_FATAL_ERROR();
			return -EFAULT;
		}

		err = zbus_sub_wait_msg(&main_subscriber, &app.chan, app.msg_buf, zbus_wait);
		if (err == -ENOMSG) {
			continue;
		} else if (err) {
			LOG_ERR("zbus_sub_wait_msg, error: %d", err);
			SEND_FATAL_ERROR();
			return -EFAULT;
		}

		err = smf_run_state(SMF_CTX(&app));
		if (err) {
			LOG_ERR("smf_run_state, error: %d", err);
			SEND_FATAL_ERROR();
			return -EFAULT;
		}
	}

	return 0;
}

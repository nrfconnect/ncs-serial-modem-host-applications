/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/task_wdt/task_wdt.h>
#include <zephyr/smf.h>
#include <zephyr/sys/reboot.h>

#include "app_common.h"
#include "modules/network/network.h"
#if defined(CONFIG_APP_FOTA)
#include "modules/fota/fota.h"
#define FOTA_CHANNEL(X) X(fota_chan, struct fota_msg)
#else
#define FOTA_CHANNEL(X)
#endif
#if defined(CONFIG_APP_LOCATION)
#include "modules/location/location.h"
#define LOCATION_CHANNEL(X) X(location_chan, struct location_msg)
#else
#define LOCATION_CHANNEL(X)
#endif
#if defined(CONFIG_APP_BATTERY)
#include "modules/battery/battery.h"
#endif
#if defined(CONFIG_APP_CLOUD)
#include "modules/cloud/cloud.h"
#define CLOUD_CHANNEL(X) X(cloud_chan, struct cloud_msg)
#else
#define CLOUD_CHANNEL(X)
#endif

LOG_MODULE_REGISTER(main, CONFIG_APP_MAIN_LOG_LEVEL);

BUILD_ASSERT(CONFIG_APP_MAIN_WATCHDOG_TIMEOUT_SECONDS >
	     CONFIG_APP_MAIN_MSG_PROCESSING_TIMEOUT_SECONDS,
	     "Watchdog timeout must be greater than maximum message processing time");

enum main_msg_type {
	MAIN_SYNC,
};

struct main_msg {
	enum main_msg_type type;
};

ZBUS_CHAN_DEFINE(main_chan,
		 struct main_msg,
		 NULL,
		 NULL,
		 ZBUS_OBSERVERS_EMPTY,
		 ZBUS_MSG_INIT(0));

ZBUS_MSG_SUBSCRIBER_DEFINE(main_subscriber);

#define CHANNEL_LIST(X) \
	X(main_chan, struct main_msg) \
	X(network_chan, struct network_msg) \
	CLOUD_CHANNEL(X) \
	LOCATION_CHANNEL(X) \
	FOTA_CHANNEL(X)

#define MAX_MSG_SIZE MAX_MSG_SIZE_FROM_LIST(CHANNEL_LIST)

#define ADD_OBSERVERS(_chan, _type) ZBUS_CHAN_ADD_OBS(_chan, main_subscriber, 0);

CHANNEL_LIST(ADD_OBSERVERS)

enum main_app_state {
	STATE_RUNNING,
		STATE_DISCONNECTED,
		STATE_CONNECTED,
			/* Waiting for the next sync tick. */
			STATE_SYNC_IDLE,
			/* Uploading diagnostics. */
			STATE_SYNC_CLOUD,
			/* Scanning for location measurements. */
			STATE_SYNC_LOCATION,
			/* Polling for a FOTA job. */
			STATE_SYNC_FOTA,
#if defined(CONFIG_APP_FOTA)
	STATE_FOTA,
	STATE_REBOOTING,
#endif
};

struct main_state {
	struct smf_ctx ctx;
	const struct zbus_channel *chan;
	uint8_t msg_buf[MAX_MSG_SIZE];
	enum main_app_state running_history;
};

static void sync_handler(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(sync_work, sync_handler);
#if defined(CONFIG_APP_BATTERY)
static void battery_sample_handler(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(battery_sample_work, battery_sample_handler);
#endif

static enum smf_state_result running_run(void *obj);
static void disconnected_entry(void *obj);
static enum smf_state_result disconnected_run(void *obj);
static void connected_entry(void *obj);
static enum smf_state_result connected_run(void *obj);
static void connected_exit(void *obj);
static enum smf_state_result sync_idle_run(void *obj);
static void sync_cloud_entry(void *obj);
static enum smf_state_result sync_cloud_run(void *obj);
static void sync_location_entry(void *obj);
static enum smf_state_result sync_location_run(void *obj);
static void sync_fota_entry(void *obj);
static enum smf_state_result sync_fota_run(void *obj);
#if defined(CONFIG_APP_FOTA)
static enum smf_state_result fota_run(void *obj);
static void rebooting_entry(void *obj);
#endif

static const struct smf_state states[] = {
	[STATE_RUNNING] = SMF_CREATE_STATE(NULL, running_run, NULL,
					   NULL, &states[STATE_DISCONNECTED]),
	[STATE_DISCONNECTED] = SMF_CREATE_STATE(disconnected_entry, disconnected_run, NULL,
						&states[STATE_RUNNING], NULL),
	[STATE_CONNECTED] = SMF_CREATE_STATE(connected_entry, connected_run, connected_exit,
					     &states[STATE_RUNNING], &states[STATE_SYNC_IDLE]),
	[STATE_SYNC_IDLE] = SMF_CREATE_STATE(NULL, sync_idle_run, NULL,
					     &states[STATE_CONNECTED], NULL),
	[STATE_SYNC_CLOUD] = SMF_CREATE_STATE(sync_cloud_entry, sync_cloud_run, NULL,
					      &states[STATE_CONNECTED], NULL),
	[STATE_SYNC_LOCATION] = SMF_CREATE_STATE(sync_location_entry, sync_location_run, NULL,
						 &states[STATE_CONNECTED], NULL),
	[STATE_SYNC_FOTA] = SMF_CREATE_STATE(sync_fota_entry, sync_fota_run, NULL,
					     &states[STATE_CONNECTED], NULL),
#if defined(CONFIG_APP_FOTA)
	[STATE_FOTA] = SMF_CREATE_STATE(NULL, fota_run, NULL, NULL, NULL),
	[STATE_REBOOTING] = SMF_CREATE_STATE(rebooting_entry, NULL, NULL, NULL, NULL),
#endif
};

static void sync_handler(struct k_work *work)
{
	struct main_msg msg = { .type = MAIN_SYNC };
	int err;

	err = zbus_chan_pub(&main_chan, &msg, PUB_TIMEOUT);
	if (err) {
		LOG_ERR("zbus_chan_pub main_chan, error: %d", err);
		FATAL_ERROR();
	}

	err = k_work_reschedule(&sync_work, K_SECONDS(CONFIG_APP_MAIN_SYNC_INTERVAL_SECONDS));
	if (err < 0) {
		LOG_ERR("k_work_reschedule sync_work, error: %d", err);
		FATAL_ERROR();
	}
}

#if defined(CONFIG_APP_BATTERY)
static void battery_sample_handler(struct k_work *work)
{
	struct battery_msg msg = { .type = BATTERY_SAMPLE };
	int err;

	err = zbus_chan_pub(&battery_chan, &msg, PUB_TIMEOUT);
	if (err) {
		LOG_ERR("zbus_chan_pub battery_chan, error: %d", err);
		FATAL_ERROR();
	}

	err = k_work_reschedule(&battery_sample_work,
				K_SECONDS(CONFIG_APP_BATTERY_SAMPLE_INTERVAL_SECONDS));
	if (err < 0) {
		LOG_ERR("k_work_reschedule battery_sample_work, error: %d", err);
		FATAL_ERROR();
	}
}
#endif

static enum smf_state_result running_run(void *obj)
{
#if defined(CONFIG_APP_FOTA)
	struct main_state *state_object = obj;

	if (state_object->chan == &fota_chan) {
		const struct fota_msg *msg = (const struct fota_msg *)state_object->msg_buf;

		switch (msg->type) {
		case FOTA_STARTING:
			LOG_INF("FOTA download starting");
			smf_set_state(SMF_CTX(state_object), &states[STATE_FOTA]);

			return SMF_EVENT_HANDLED;
		case FOTA_SUCCESS:
			smf_set_state(SMF_CTX(state_object), &states[STATE_REBOOTING]);

			return SMF_EVENT_HANDLED;
		case FOTA_ABORTED:
			LOG_INF("No FOTA update available");

			return SMF_EVENT_HANDLED;
		default:
			break;
		}
	}
#endif /* CONFIG_APP_FOTA */

	return SMF_EVENT_PROPAGATE;
}

static void disconnected_entry(void *obj)
{
	struct main_state *state_object = obj;

	LOG_INF("Network disconnected");

	state_object->running_history = STATE_DISCONNECTED;
}

static enum smf_state_result disconnected_run(void *obj)
{
	struct main_state *state_object = obj;

	if (state_object->chan == &network_chan) {
		const struct network_msg *msg =
			(const struct network_msg *)state_object->msg_buf;

		if (msg->type == NETWORK_CONNECTED) {
			smf_set_state(SMF_CTX(state_object), &states[STATE_CONNECTED]);

			return SMF_EVENT_HANDLED;
		}
	}

	return SMF_EVENT_PROPAGATE;
}

static void connected_entry(void *obj)
{
	struct main_state *state_object = obj;
	int err;

	LOG_INF("Network connected");

	state_object->running_history = STATE_CONNECTED;

	err = k_work_reschedule(&sync_work,
			       K_SECONDS(CONFIG_APP_MAIN_SYNC_BOOT_DELAY_SECONDS));
	if (err < 0) {
		LOG_ERR("k_work_reschedule sync_work, error: %d", err);
		FATAL_ERROR();
	}
}

static enum smf_state_result connected_run(void *obj)
{
	struct main_state *state_object = obj;

	if (state_object->chan == &network_chan) {
		const struct network_msg *msg =
			(const struct network_msg *)state_object->msg_buf;

		if (msg->type == NETWORK_DISCONNECTED) {
			smf_set_state(SMF_CTX(state_object), &states[STATE_DISCONNECTED]);

			return SMF_EVENT_HANDLED;
		}
	}

	return SMF_EVENT_PROPAGATE;
}

static void connected_exit(void *obj)
{
	ARG_UNUSED(obj);

	(void)k_work_cancel_delayable(&sync_work);
}

static enum smf_state_result sync_idle_run(void *obj)
{
	struct main_state *state_object = obj;

	if (state_object->chan == &main_chan) {
		const struct main_msg *msg = (const struct main_msg *)state_object->msg_buf;

		if (msg->type == MAIN_SYNC) {
#if defined(CONFIG_APP_CLOUD)
			smf_set_state(SMF_CTX(state_object), &states[STATE_SYNC_CLOUD]);
#elif defined(CONFIG_APP_LOCATION)
			smf_set_state(SMF_CTX(state_object), &states[STATE_SYNC_LOCATION]);
#elif defined(CONFIG_APP_FOTA)
			smf_set_state(SMF_CTX(state_object), &states[STATE_SYNC_FOTA]);
#endif

			return SMF_EVENT_HANDLED;
		}
	}

	return SMF_EVENT_PROPAGATE;
}

static void sync_cloud_entry(void *obj)
{
	ARG_UNUSED(obj);

#if defined(CONFIG_APP_CLOUD)
	struct cloud_msg msg = { .type = CLOUD_SYNC_REQUEST };
	int err = zbus_chan_pub(&cloud_chan, &msg, PUB_TIMEOUT);

	if (err) {
		LOG_ERR("zbus_chan_pub cloud_chan, error: %d", err);
		FATAL_ERROR();
	}
#endif
}

static enum smf_state_result sync_cloud_run(void *obj)
{
#if defined(CONFIG_APP_CLOUD)
	struct main_state *state_object = obj;

	if (state_object->chan == &cloud_chan) {
		const struct cloud_msg *msg = (const struct cloud_msg *)state_object->msg_buf;

		if (msg->type == CLOUD_SYNC_DONE) {
#if defined(CONFIG_APP_LOCATION)
			smf_set_state(SMF_CTX(state_object), &states[STATE_SYNC_LOCATION]);
#elif defined(CONFIG_APP_FOTA)
			smf_set_state(SMF_CTX(state_object), &states[STATE_SYNC_FOTA]);
#else
			smf_set_state(SMF_CTX(state_object), &states[STATE_SYNC_IDLE]);
#endif

			return SMF_EVENT_HANDLED;
		}
	}
#endif

	return SMF_EVENT_PROPAGATE;
}

static void sync_location_entry(void *obj)
{
	ARG_UNUSED(obj);

#if defined(CONFIG_APP_LOCATION)
	struct location_msg msg = {
		.type = LOCATION_FIX_REQUEST,
		.mode = LOCATION_MODE_ALL,
	};
	int err = zbus_chan_pub(&location_chan, &msg, PUB_TIMEOUT);

	if (err) {
		LOG_ERR("zbus_chan_pub location_chan, error: %d", err);
		FATAL_ERROR();
	}
#endif
}

static enum smf_state_result sync_location_run(void *obj)
{
#if defined(CONFIG_APP_LOCATION)
	struct main_state *state_object = obj;

	if (state_object->chan == &location_chan) {
		const struct location_msg *msg =
			(const struct location_msg *)state_object->msg_buf;

		if (msg->type == LOCATION_SYNC_DONE) {
#if defined(CONFIG_APP_FOTA)
			smf_set_state(SMF_CTX(state_object), &states[STATE_SYNC_FOTA]);
#else
			smf_set_state(SMF_CTX(state_object), &states[STATE_SYNC_IDLE]);
#endif

			return SMF_EVENT_HANDLED;
		}
	}
#else
	ARG_UNUSED(obj);
#endif

	return SMF_EVENT_PROPAGATE;
}

static void sync_fota_entry(void *obj)
{
	ARG_UNUSED(obj);

#if defined(CONFIG_APP_FOTA)
	struct fota_msg msg = { .type = FOTA_POLL_REQUEST };
	int err = zbus_chan_pub(&fota_chan, &msg, PUB_TIMEOUT);

	if (err) {
		LOG_ERR("zbus_chan_pub fota_chan, error: %d", err);
		FATAL_ERROR();
	}
#endif
}

static enum smf_state_result sync_fota_run(void *obj)
{
#if defined(CONFIG_APP_FOTA)
	struct main_state *state_object = obj;

	if (state_object->chan == &fota_chan) {
		const struct fota_msg *msg = (const struct fota_msg *)state_object->msg_buf;

		/* FOTA_STARTING propagates to STATE_RUNNING, which enters STATE_FOTA. */
		if (msg->type == FOTA_ABORTED) {
			LOG_INF("No FOTA update available");
			smf_set_state(SMF_CTX(state_object), &states[STATE_SYNC_IDLE]);

			return SMF_EVENT_HANDLED;
		}
	}
#endif

	return SMF_EVENT_PROPAGATE;
}

#if defined(CONFIG_APP_FOTA)
static enum smf_state_result fota_run(void *obj)
{
	struct main_state *state_object = obj;

	if (state_object->chan == &fota_chan) {
		const struct fota_msg *msg = (const struct fota_msg *)state_object->msg_buf;

		switch (msg->type) {
		case FOTA_NETWORK_DISCONNECT_NEEDED: {
			struct network_msg net_msg = { .type = NETWORK_DISCONNECT };
			int err = zbus_chan_pub(&network_chan, &net_msg, PUB_TIMEOUT);

			if (err) {
				LOG_ERR("zbus_chan_pub network_chan, error: %d", err);
				FATAL_ERROR();
			}

			return SMF_EVENT_HANDLED;
		}
		case FOTA_SUCCESS:
			smf_set_state(SMF_CTX(state_object), &states[STATE_REBOOTING]);

			return SMF_EVENT_HANDLED;
		case FOTA_ABORTED:
			LOG_INF("No FOTA update available");
			smf_set_state(SMF_CTX(state_object),
				      &states[state_object->running_history]);

			return SMF_EVENT_HANDLED;
		default:
			break;
		}
	} else if (state_object->chan == &network_chan) {
		const struct network_msg *msg =
			(const struct network_msg *)state_object->msg_buf;

		if (msg->type == NETWORK_DISCONNECTED) {
			struct fota_msg fota_msg = { .type = FOTA_NETWORK_DISCONNECTED };
			int err = zbus_chan_pub(&fota_chan, &fota_msg, PUB_TIMEOUT);

			if (err) {
				LOG_ERR("zbus_chan_pub fota_chan, error: %d", err);
				FATAL_ERROR();
			}

			state_object->running_history = STATE_DISCONNECTED;

			return SMF_EVENT_HANDLED;
		} else if (msg->type == NETWORK_CONNECTED) {
			state_object->running_history = STATE_CONNECTED;

			return SMF_EVENT_HANDLED;
		}
	}

	return SMF_EVENT_PROPAGATE;
}

static void rebooting_entry(void *obj)
{
	ARG_UNUSED(obj);

	LOG_INF("FOTA successful, rebooting to apply the update");
	LOG_PANIC();
	sys_reboot(SYS_REBOOT_COLD);
}
#endif /* CONFIG_APP_FOTA */

static void main_wdt_callback(int channel_id, void *user_data)
{
	LOG_ERR("Main watchdog expired, channel: %d, thread: %s",
		channel_id, k_thread_name_get((k_tid_t)user_data));

	FATAL_ERROR_WATCHDOG_TIMEOUT();
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
	static struct main_state main_state;

	LOG_INF("Serial Modem Host 93m1 starting");

	err = task_wdt_init(DEVICE_DT_GET(DT_ALIAS(watchdog0)));
	if (err) {
		LOG_ERR("task_wdt_init, error: %d", err);
		FATAL_ERROR();
		return -EFAULT;
	}

	task_wdt_id = task_wdt_add(wdt_timeout_ms, main_wdt_callback, (void *)k_current_get());
	if (task_wdt_id < 0) {
		LOG_ERR("Failed to add task to watchdog: %d", task_wdt_id);
		FATAL_ERROR();
		return -EFAULT;
	}

#if defined(CONFIG_APP_BATTERY)
	err = k_work_reschedule(&battery_sample_work, K_NO_WAIT);
	if (err < 0) {
		LOG_ERR("k_work_reschedule battery_sample_work, error: %d", err);
		FATAL_ERROR();
		return -EFAULT;
	}
#endif

	smf_set_initial(SMF_CTX(&main_state), &states[STATE_RUNNING]);

	while (true) {
		err = task_wdt_feed(task_wdt_id);
		if (err) {
			LOG_ERR("task_wdt_feed, error: %d", err);
			FATAL_ERROR();
			return -EFAULT;
		}

		err = zbus_sub_wait_msg(&main_subscriber, &main_state.chan,
					main_state.msg_buf, zbus_wait);
		if (err == -ENOMSG) {
			continue;
		} else if (err) {
			LOG_ERR("zbus_sub_wait_msg, error: %d", err);
			FATAL_ERROR();
			return -EFAULT;
		}

		err = smf_run_state(SMF_CTX(&main_state));
		if (err) {
			LOG_ERR("smf_run_state(), error: %d", err);
			FATAL_ERROR();
			return -EFAULT;
		}
	}

	return 0;
}

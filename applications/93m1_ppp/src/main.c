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
#endif
#if defined(CONFIG_APP_BATTERY)
#include "modules/battery/battery.h"
#endif

LOG_MODULE_REGISTER(main, CONFIG_APP_MAIN_LOG_LEVEL);

BUILD_ASSERT(CONFIG_APP_MAIN_WATCHDOG_TIMEOUT_SECONDS >
	     CONFIG_APP_MAIN_MSG_PROCESSING_TIMEOUT_SECONDS,
	     "Watchdog timeout must be greater than maximum message processing time");

enum main_msg_type {
	MAIN_START,
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
	FOTA_CHANNEL(X)

#define MAX_MSG_SIZE MAX_MSG_SIZE_FROM_LIST(CHANNEL_LIST)

#define ADD_OBSERVERS(_chan, _type) ZBUS_CHAN_ADD_OBS(_chan, main_subscriber, 0);

CHANNEL_LIST(ADD_OBSERVERS)

enum main_app_state {
	STATE_INIT,
	STATE_RUNNING,
};

struct main_state {
	struct smf_ctx ctx;
	const struct zbus_channel *chan;
	uint8_t msg_buf[MAX_MSG_SIZE];
};

#if defined(CONFIG_APP_LOCATION)
static void location_fix_handler(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(location_fix_work, location_fix_handler);
#endif
#if defined(CONFIG_APP_BATTERY)
static void battery_sample_handler(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(battery_sample_work, battery_sample_handler);
#endif

static enum smf_state_result init_run(void *obj);
static void running_entry(void *obj);
static enum smf_state_result running_run(void *obj);

static const struct smf_state states[] = {
	[STATE_INIT] = SMF_CREATE_STATE(NULL, init_run, NULL, NULL, NULL),
	[STATE_RUNNING] = SMF_CREATE_STATE(running_entry, running_run, NULL, NULL, NULL),
};

#if defined(CONFIG_APP_LOCATION)
static void location_fix_handler(struct k_work *work)
{
	struct location_msg msg = { .type = LOCATION_FIX_REQUEST, .mode = LOCATION_MODE_ALL };
	int err;

	err = zbus_chan_pub(&location_chan, &msg, PUB_TIMEOUT);
	if (err) {
		LOG_ERR("zbus_chan_pub location_chan, error: %d", err);
		FATAL_ERROR();
	}

	err = k_work_reschedule(&location_fix_work,
				K_SECONDS(CONFIG_APP_LOCATION_INTERVAL_SECONDS));
	if (err < 0) {
		LOG_ERR("k_work_reschedule location_fix_work, error: %d", err);
		FATAL_ERROR();
	}
}
#endif

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

static enum smf_state_result init_run(void *obj)
{
	struct main_state *state_object = obj;

	if (state_object->chan != &main_chan) {
		return SMF_EVENT_HANDLED;
	}

	const struct main_msg *msg = (const struct main_msg *)state_object->msg_buf;

	if (msg->type == MAIN_START) {
		smf_set_state(SMF_CTX(state_object), &states[STATE_RUNNING]);
	}

	return SMF_EVENT_HANDLED;
}

static void running_entry(void *obj)
{
	ARG_UNUSED(obj);

	LOG_INF("Serial Modem Host 93m1 starting");

#if defined(CONFIG_APP_BATTERY)
	int err = k_work_reschedule(&battery_sample_work, K_NO_WAIT);

	if (err < 0) {
		LOG_ERR("k_work_reschedule battery_sample_work, error: %d", err);
		FATAL_ERROR();
	}
#endif
}

static enum smf_state_result running_run(void *obj)
{
	int err;
	struct main_state *state_object = obj;

	if (state_object->chan == &network_chan) {
		const struct network_msg *msg =
			(const struct network_msg *)state_object->msg_buf;

		switch (msg->type) {
		case NETWORK_CONNECTED:
			LOG_INF("Network connected");
#if defined(CONFIG_APP_LOCATION)
			{
				int err = k_work_reschedule(&location_fix_work,
					  K_SECONDS(CONFIG_APP_LOCATION_BOOT_DELAY_SECONDS));

				if (err < 0) {
					LOG_ERR("k_work_reschedule location_fix_work, error: %d",
						err);
					FATAL_ERROR();
				}
			}
#endif
			break;
		case NETWORK_DISCONNECTED:
			LOG_INF("Network disconnected");
#if defined(CONFIG_APP_LOCATION)
			k_work_cancel_delayable(&location_fix_work);
#endif
#if defined(CONFIG_APP_FOTA)
			{
				struct fota_msg fota_msg = {
					.type = FOTA_NETWORK_DISCONNECTED
				};

				int err = zbus_chan_pub(&fota_chan, &fota_msg, PUB_TIMEOUT);

				if (err) {
					LOG_ERR("zbus_chan_pub fota_chan, error: %d", err);
					FATAL_ERROR();
				}
			}
#endif /* CONFIG_APP_FOTA */
			break;
		default:
			break;
		}

		return SMF_EVENT_HANDLED;
	}

#if defined(CONFIG_APP_FOTA)
	if (state_object->chan == &fota_chan) {
		const struct fota_msg *msg = (const struct fota_msg *)state_object->msg_buf;

		switch (msg->type) {
		case FOTA_NETWORK_DISCONNECT_NEEDED: {
			/* Bring the link down; the resulting NETWORK_DISCONNECTED */

			struct network_msg net_msg = {
				.type = NETWORK_DISCONNECT
			};

			err = zbus_chan_pub(&network_chan, &net_msg, PUB_TIMEOUT);
			if (err) {
				LOG_ERR("zbus_chan_pub network_chan, error: %d", err);
				FATAL_ERROR();
			}
			break;
		}
		case FOTA_SUCCESS:
			LOG_INF("FOTA successful, rebooting to apply the update");
			LOG_PANIC();
			sys_reboot(SYS_REBOOT_COLD);
			break;
		case FOTA_STARTING:
			LOG_INF("FOTA download starting");
			break;
		case FOTA_ABORTED:
			LOG_INF("No FOTA update available");
			break;
		default:
			break;
		}

		return SMF_EVENT_HANDLED;
	}
#endif /* CONFIG_APP_FOTA */

	(void)err;

	return SMF_EVENT_HANDLED;
}

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
	struct main_msg start_msg = {
		.type = MAIN_START,
	};

	task_wdt_id = task_wdt_add(wdt_timeout_ms, main_wdt_callback, (void *)k_current_get());
	if (task_wdt_id < 0) {
		LOG_ERR("Failed to add task to watchdog: %d", task_wdt_id);
		FATAL_ERROR();
		return -EFAULT;
	}

	smf_set_initial(SMF_CTX(&main_state), &states[STATE_INIT]);

	err = zbus_chan_pub(&main_chan, &start_msg, PUB_TIMEOUT);
	if (err) {
		LOG_ERR("zbus_chan_pub, error: %d", err);
		FATAL_ERROR();
		return -EFAULT;
	}

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

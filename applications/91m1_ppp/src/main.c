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
#include "modules/cloud/cloud.h"

#if defined(CONFIG_APP_FOTA)
#include "modules/fota/fota.h"
#endif /* CONFIG_APP_FOTA */

#if defined(CONFIG_APP_LOCATION)
#include "modules/location/location.h"
#endif /* CONFIG_APP_LOCATION */

LOG_MODULE_REGISTER(main, CONFIG_APP_MAIN_LOG_LEVEL);

BUILD_ASSERT(CONFIG_APP_MAIN_WATCHDOG_TIMEOUT_SECONDS >
	     CONFIG_APP_MAIN_MSG_PROCESSING_TIMEOUT_SECONDS,
	     "Watchdog timeout must be greater than maximum message processing time");

ZBUS_MSG_SUBSCRIBER_DEFINE(main_subscriber);

/* Private channel message types for internal state management. */
enum priv_main_msg_type {
	MAIN_PRIV_CLOUD_SYNCHRONIZATION,
};

struct priv_main_msg {
	enum priv_main_msg_type type;
};

ZBUS_CHAN_DEFINE(main_priv_chan,
		 struct priv_main_msg,
		 NULL,
		 NULL,
		 ZBUS_OBSERVERS_EMPTY,
		 ZBUS_MSG_INIT(0)
);

/* Define the channels that the module subscribes to, their associated message types
 * and the subscriber that will receive the messages on the channel.
 */
#define CHANNEL_LIST(X) \
	X(network_chan, struct network_msg) \
	X(cloud_chan, struct cloud_msg) \
	IF_ENABLED(CONFIG_APP_FOTA, (X(fota_chan, struct fota_msg))) \
	IF_ENABLED(CONFIG_APP_LOCATION, (X(location_chan, struct location_msg))) \
	X(main_priv_chan, struct priv_main_msg)

/* Calculate the maximum message size from the list of channels */
#define MAX_MSG_SIZE		    MAX_MSG_SIZE_FROM_LIST(CHANNEL_LIST)

/* Add main_subscriber as observer to all the channels in the list. */
#define ADD_OBSERVERS(_chan, _type) ZBUS_CHAN_ADD_OBS(_chan, main_subscriber, 0);

/* Expand to a call to ZBUS_CHAN_ADD_OBS for each channel in the list. */
CHANNEL_LIST(ADD_OBSERVERS)

/** Demo cloud payload */
#define DEMO_CLOUD_PAYLOAD \
	"{\"appId\":\"SMHA\",\"messageType\":\"DATA\",\"data\":\"hello\"}"

/** Application SMF states. */
enum main_app_state {
	/**
	 * Top-level state.
	 * The initial substate is @ref STATE_CLOUD_DISCONNECTED.
	 */
	STATE_RUNNING,
		/**
		 * Cloud is not connected. Periodic cloud synchronization is not
		 * scheduled, and synchronization triggers are ignored.
		 */
		STATE_CLOUD_DISCONNECTED,
		/**
		 * Cloud is connected. Periodic synchronization is scheduled on the
		 * dedicated cloud-sync workqueue.
		 *
		 * Each synchronization:
		 * - Sends demo payload
		 * - Sends location via scanned Wi-Fi Access Point MAC addresses. (Optional)
		 * - Polls device shadow
		 * - Polls for any available FOTA job. (Optional)
		 * - Posts any pending Memfault data
		 */
		STATE_CLOUD_CONNECTED,
#if defined(CONFIG_APP_FOTA)
	/**
	 * A FOTA download is in progress. Cloud state at entry is saved in
	 * @ref main_state::cloud_history for restore on @ref FOTA_ABORTED.
	 */
	STATE_FOTA,
	/**
	 * FOTA completed; application cleanup runs before reboot.
	 */
	STATE_REBOOTING,
#endif /* CONFIG_APP_FOTA */
};

/* State object for the main module.
 * Used to transfer data between state changes.
 */
struct main_state {
	struct smf_ctx ctx;
	const struct zbus_channel *chan;
	uint8_t msg_buf[MAX_MSG_SIZE];
	struct k_work_delayable cloud_sync_dwork;
	/** Cloud state to restore after @ref FOTA_ABORTED. */
	enum main_app_state cloud_history;
};

static struct main_state main_state;
static const struct smf_state states[];

/** Cloud synchronization workqueue */
static K_THREAD_STACK_DEFINE(cloud_sync_workq_stack,
			     CONFIG_APP_MAIN_CLOUD_SYNC_WORKQ_STACK_SIZE);
static struct k_work_q cloud_sync_workq;

/** Forward declarations */

static void state_running_entry(void *obj);
static enum smf_state_result state_running_run(void *obj);
static void state_cloud_disconnected_entry(void *obj);
static enum smf_state_result state_cloud_disconnected_run(void *obj);
static void state_cloud_disconnected_exit(void *obj);
static void state_cloud_connected_entry(void *obj);
static enum smf_state_result state_cloud_connected_run(void *obj);
static void state_cloud_connected_exit(void *obj);

#if defined(CONFIG_APP_FOTA)
static void state_fota_entry(void *obj);
static enum smf_state_result state_fota_run(void *obj);
static void state_rebooting_entry(void *obj);
#endif /* CONFIG_APP_FOTA */

/** SMF state table */
static const struct smf_state states[] = {
	[STATE_RUNNING] =
		SMF_CREATE_STATE(state_running_entry,
				 state_running_run,
				 NULL,
				 NULL,
				 &states[STATE_CLOUD_DISCONNECTED]),
	[STATE_CLOUD_DISCONNECTED] =
		SMF_CREATE_STATE(state_cloud_disconnected_entry,
				 state_cloud_disconnected_run,
				 state_cloud_disconnected_exit,
				 &states[STATE_RUNNING],
				 NULL),
	[STATE_CLOUD_CONNECTED] =
		SMF_CREATE_STATE(state_cloud_connected_entry,
				 state_cloud_connected_run,
				 state_cloud_connected_exit,
				 &states[STATE_RUNNING],
				 NULL),
#if defined(CONFIG_APP_FOTA)
	[STATE_FOTA] =
		SMF_CREATE_STATE(state_fota_entry,
				 state_fota_run,
				 NULL,
				 &states[STATE_RUNNING],
				 NULL),
	[STATE_REBOOTING] =
		SMF_CREATE_STATE(state_rebooting_entry,
				 NULL, NULL, NULL, NULL),
#endif /* CONFIG_APP_FOTA */
};

/** Convenience functions */

static void demo_cloud_message_send(void)
{
	int err;
	struct cloud_msg msg = {
		.type = CLOUD_MESSAGE_SEND,
		.payload = DEMO_CLOUD_PAYLOAD,
	};

	msg.payload_len = sizeof(DEMO_CLOUD_PAYLOAD) - 1;
	err = zbus_chan_pub(&cloud_chan, &msg, PUB_TIMEOUT);
	if (err) {
		LOG_ERR("zbus_chan_pub, error: %d", err);
		FATAL_ERROR();
	}
}

static void cloud_connect_request(void)
{
	int err;
	struct cloud_msg msg = {
		.type = CLOUD_CONNECT
	};

	err = zbus_chan_pub(&cloud_chan, &msg, PUB_TIMEOUT);
	if (err) {
		LOG_ERR("zbus_chan_pub, error: %d", err);
		FATAL_ERROR();
	}
}

static void cloud_disconnect_request(void)
{
	int err;
	struct cloud_msg msg = {
		.type = CLOUD_DISCONNECT
	};

	err = zbus_chan_pub(&cloud_chan, &msg, PUB_TIMEOUT);
	if (err) {
		LOG_ERR("zbus_chan_pub, error: %d", err);
		FATAL_ERROR();
	}
}

#if defined(CONFIG_APP_LOCATION)
static void location_search_request(void)
{
	int err;
	struct location_msg msg = {
		.type = LOCATION_SEARCH_TRIGGER
	};

	err = zbus_chan_pub(&location_chan, &msg, PUB_TIMEOUT);
	if (err) {
		LOG_ERR("zbus_chan_pub, error: %d", err);
		FATAL_ERROR();
	}
}

static void location_request_forward(const struct location_cloud_request_data *request)
{
	int err;
	struct cloud_msg msg = {
		.type = CLOUD_LOCATION_REQUEST
	};

	msg.location_request = *request;

	err = zbus_chan_pub(&cloud_chan, &msg, PUB_TIMEOUT);
	if (err) {
		LOG_ERR("zbus_chan_pub, error: %d", err);
		FATAL_ERROR();
	}
}
#endif /* CONFIG_APP_LOCATION */

#if defined(CONFIG_APP_FOTA)
static void fota_poll_request(void)
{
	int err;
	struct fota_msg fota_msg = {
		.type = FOTA_POLL_REQUEST
	};

	err = zbus_chan_pub(&fota_chan, &fota_msg, PUB_TIMEOUT);
	if (err) {
		LOG_ERR("zbus_chan_pub, error: %d", err);
		FATAL_ERROR();
	}
}
#endif /* CONFIG_APP_FOTA */

static void shadow_poll_request(void)
{
	int err;
	struct cloud_msg msg = {
		.type = CLOUD_SHADOW_POLL_REQUEST
	};

	err = zbus_chan_pub(&cloud_chan, &msg, PUB_TIMEOUT);
	if (err) {
		LOG_ERR("zbus_chan_pub, error: %d", err);
		FATAL_ERROR();
	}
}

static void memfault_post_request(void)
{
	int err;
	struct cloud_msg msg = {
		.type = CLOUD_MEMFAULT_POST_REQUEST
	};

	err = zbus_chan_pub(&cloud_chan, &msg, PUB_TIMEOUT);
	if (err) {
		LOG_ERR("zbus_chan_pub, error: %d", err);
		FATAL_ERROR();
	}
}

static void cloud_sync_run(void)
{
	demo_cloud_message_send();

#if defined(CONFIG_APP_LOCATION)
	location_search_request();
#endif /* CONFIG_APP_LOCATION */

	shadow_poll_request();

#if defined(CONFIG_APP_FOTA)
	fota_poll_request();
#endif /* CONFIG_APP_FOTA */

	/* Posted last so the module handles it once its own requests above are done. */
	memfault_post_request();
}

static void cloud_sync_schedule(struct main_state *state)
{
	int err;

	err = k_work_schedule_for_queue(
		&cloud_sync_workq,
		&state->cloud_sync_dwork,
		K_SECONDS(CONFIG_APP_MAIN_CLOUD_SYNCHRONIZATION_PERIOD_SECONDS));
	if (err < 0) {
		LOG_ERR("k_work_schedule_for_queue, error: %d", err);
		FATAL_ERROR();
	}
}

static void cloud_sync_cancel(struct main_state *state)
{
	(void)k_work_cancel_delayable(&state->cloud_sync_dwork);
}

static void cloud_sync_delayed_work_handler(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct main_state *state = CONTAINER_OF(dwork, struct main_state, cloud_sync_dwork);
	int err;
	struct priv_main_msg msg = {
		.type = MAIN_PRIV_CLOUD_SYNCHRONIZATION
	};

	err = zbus_chan_pub(&main_priv_chan, &msg, PUB_TIMEOUT);
	if (err) {
		LOG_ERR("zbus_chan_pub main_priv_chan, error: %d", err);
		FATAL_ERROR();
	}

	cloud_sync_schedule(state);
}

#if defined(CONFIG_APP_FOTA)
static void fota_running_state_restore(struct main_state *state_object)
{
	if (state_object->cloud_history == STATE_CLOUD_CONNECTED) {
		smf_set_state(SMF_CTX(state_object), &states[STATE_CLOUD_CONNECTED]);
	} else {
		smf_set_state(SMF_CTX(state_object), &states[STATE_CLOUD_DISCONNECTED]);
	}
}
#endif /* CONFIG_APP_FOTA */

/** SMF state functions */

static void state_running_entry(void *obj)
{
	ARG_UNUSED(obj);

	LOG_INF("state_running_entry");
}

static enum smf_state_result state_running_run(void *obj)
{
	struct main_state *state_object = obj;

	if (state_object->chan == &network_chan) {
		const struct network_msg *msg =
			(const struct network_msg *)state_object->msg_buf;

		if (msg->type == NETWORK_CONNECTED) {
			LOG_INF("Network connected");
			cloud_connect_request();
		} else if (msg->type == NETWORK_DISCONNECTED) {
			LOG_INF("Network disconnected");
			cloud_disconnect_request();
		}

		return SMF_EVENT_HANDLED;
	}

#if defined(CONFIG_APP_LOCATION)
	if (state_object->chan == &location_chan) {
		const struct location_msg *msg =
			(const struct location_msg *)state_object->msg_buf;

		if (msg->type == LOCATION_CLOUD_REQUEST) {
			location_request_forward(&msg->cloud_request);
		}

		return SMF_EVENT_HANDLED;
	}
#endif /* CONFIG_APP_LOCATION */

#if defined(CONFIG_APP_FOTA)
	if (state_object->chan == &fota_chan) {
		const struct fota_msg *msg = (const struct fota_msg *)state_object->msg_buf;

		if (msg->type == FOTA_STARTING) {
			LOG_INF("FOTA download starting");
			smf_set_state(SMF_CTX(state_object), &states[STATE_FOTA]);

			return SMF_EVENT_HANDLED;
		}
	}
#endif /* CONFIG_APP_FOTA */

	return SMF_EVENT_PROPAGATE;
}

static void state_cloud_disconnected_entry(void *obj)
{
	struct main_state *state_object = obj;

	LOG_INF("state_cloud_disconnected_entry");

	state_object->cloud_history = STATE_CLOUD_DISCONNECTED;
}

static void state_cloud_disconnected_exit(void *obj)
{
	ARG_UNUSED(obj);

	LOG_INF("state_cloud_disconnected_exit");
}

static enum smf_state_result state_cloud_disconnected_run(void *obj)
{
	struct main_state *state_object = obj;

	if (state_object->chan == &cloud_chan) {
		const struct cloud_msg *msg = (const struct cloud_msg *)state_object->msg_buf;

		if (msg->type == CLOUD_CONNECTED) {
			LOG_INF("Cloud connected");

			smf_set_state(SMF_CTX(state_object), &states[STATE_CLOUD_CONNECTED]);

			return SMF_EVENT_HANDLED;
		}
	}

	return SMF_EVENT_PROPAGATE;
}

static void state_cloud_connected_entry(void *obj)
{
	struct main_state *state_object = obj;

	LOG_INF("state_cloud_connected_entry");

	state_object->cloud_history = STATE_CLOUD_CONNECTED;
	cloud_sync_schedule(state_object);

	/* Synchronize once immediately so connecting always syncs without waiting a period. */
	cloud_sync_run();
}

static void state_cloud_connected_exit(void *obj)
{
	struct main_state *state_object = obj;

	LOG_INF("state_cloud_connected_exit");

	cloud_sync_cancel(state_object);
}

static enum smf_state_result state_cloud_connected_run(void *obj)
{
	struct main_state *state_object = obj;

	if (state_object->chan == &cloud_chan) {
		const struct cloud_msg *msg = (const struct cloud_msg *)state_object->msg_buf;

		if (msg->type == CLOUD_DISCONNECTED) {
			LOG_INF("Cloud disconnected");

			smf_set_state(SMF_CTX(state_object), &states[STATE_CLOUD_DISCONNECTED]);

			return SMF_EVENT_HANDLED;
		}
	}

	if (state_object->chan == &main_priv_chan) {
		const struct priv_main_msg *msg =
			(const struct priv_main_msg *)state_object->msg_buf;

		if (msg->type == MAIN_PRIV_CLOUD_SYNCHRONIZATION) {
			cloud_sync_run();

			return SMF_EVENT_HANDLED;
		}
	}

	return SMF_EVENT_PROPAGATE;
}

#if defined(CONFIG_APP_FOTA)
static void state_fota_entry(void *obj)
{
	ARG_UNUSED(obj);

	LOG_INF("state_fota_entry");
}

static enum smf_state_result state_fota_run(void *obj)
{
	struct main_state *state_object = obj;

	if (state_object->chan == &fota_chan) {
		const struct fota_msg *msg = (const struct fota_msg *)state_object->msg_buf;

		if (msg->type == FOTA_ABORTED) {
			LOG_INF("FOTA download aborted");
			fota_running_state_restore(state_object);

			return SMF_EVENT_HANDLED;
		} else if (msg->type == FOTA_REBOOT_REQUEST) {
			smf_set_state(SMF_CTX(state_object), &states[STATE_REBOOTING]);

			return SMF_EVENT_HANDLED;
		}
	} else if (state_object->chan == &cloud_chan) {
		const struct cloud_msg *msg = (const struct cloud_msg *)state_object->msg_buf;

		if (msg->type == CLOUD_DISCONNECTED) {
			state_object->cloud_history = STATE_CLOUD_DISCONNECTED;

			return SMF_EVENT_HANDLED;
		}
	}

	return SMF_EVENT_PROPAGATE;
}

static void state_rebooting_entry(void *obj)
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
	struct k_work_queue_config cloud_sync_workq_cfg = {
		.name = "cloud_sync",
	};

	k_work_queue_init(&cloud_sync_workq);
	k_work_queue_start(&cloud_sync_workq,
			   cloud_sync_workq_stack,
			   K_THREAD_STACK_SIZEOF(cloud_sync_workq_stack),
			   CONFIG_APP_MAIN_CLOUD_SYNC_WORKQ_PRIORITY,
			   &cloud_sync_workq_cfg);
	k_work_init_delayable(&main_state.cloud_sync_dwork, cloud_sync_delayed_work_handler);

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

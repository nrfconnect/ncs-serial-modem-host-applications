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
#include "modules/fota/fota.h"

LOG_MODULE_REGISTER(main, CONFIG_APP_MAIN_LOG_LEVEL);

BUILD_ASSERT(IS_ENABLED(CONFIG_APP_FOTA), "FOTA module is required");
BUILD_ASSERT(CONFIG_APP_MAIN_WATCHDOG_TIMEOUT_SECONDS >
	     CONFIG_APP_MAIN_MSG_PROCESSING_TIMEOUT_SECONDS,
	     "Watchdog timeout must be greater than maximum message processing time");

ZBUS_MSG_SUBSCRIBER_DEFINE(main_subscriber);

enum main_sync_msg_type {
	MAIN_CLOUD_SYNCHRONIZATION,
};

struct main_sync_msg {
	enum main_sync_msg_type type;
};

ZBUS_CHAN_DEFINE(main_sync_chan,
		 struct main_sync_msg,
		 NULL,
		 NULL,
		 ZBUS_OBSERVERS_EMPTY,
		 ZBUS_MSG_INIT(0)
);

#define CHANNEL_LIST(X) \
	X(network_chan, struct network_msg) \
	X(cloud_chan, struct cloud_msg) \
	X(fota_chan, struct fota_msg) \
	X(main_sync_chan, struct main_sync_msg)

#define MAX_MSG_SIZE MAX_MSG_SIZE_FROM_LIST(CHANNEL_LIST)
#define ADD_OBSERVERS(_chan, _type) ZBUS_CHAN_ADD_OBS(_chan, main_subscriber, 0);

CHANNEL_LIST(ADD_OBSERVERS)

/** Demo cloud payload */
#define DEMO_CLOUD_PAYLOAD \
	"{\"appId\":\"SMHA\",\"messageType\":\"DATA\",\"data\":\"hello\"}"

/** Application SMF states. */
enum main_app_state {
	/**
	 * Top-level state. Handles network, FOTA, and cloud-synchronization
	 * events. The initial substate is @ref STATE_CLOUD_DISCONNECTED.
	 */
	STATE_RUNNING,
	/**
	 * Cloud is not connected. Periodic cloud synchronization is not
	 * scheduled.
	 */
	STATE_CLOUD_DISCONNECTED,
	/**
	 * Cloud is connected. An initial cloud synchronization runs on
	 * entry and periodic synchronization is scheduled on the dedicated
	 * cloud-sync workqueue.
	 */
	STATE_CLOUD_CONNECTED,
};

struct main_state {
	struct smf_ctx ctx;
	const struct zbus_channel *chan;
	uint8_t msg_buf[MAX_MSG_SIZE];
	struct k_work_delayable cloud_sync_dwork;
};

static struct main_state main_state;
static const struct smf_state states[];

/** Cloud synchronization workqueue */
static K_THREAD_STACK_DEFINE(cloud_sync_workq_stack,
			     CONFIG_APP_MAIN_CLOUD_SYNC_WORKQ_STACK_SIZE);
static struct k_work_q cloud_sync_workq;

/** Forward declarations */

static void running_entry(void *obj);
static enum smf_state_result running_run(void *obj);
static void cloud_disconnected_entry(void *obj);
static enum smf_state_result cloud_disconnected_run(void *obj);
static void cloud_disconnected_exit(void *obj);
static void cloud_connected_entry(void *obj);
static enum smf_state_result cloud_connected_run(void *obj);
static void cloud_connected_exit(void *obj);

/** SMF state table */

static const struct smf_state states[] = {
	[STATE_RUNNING] =
		SMF_CREATE_STATE(running_entry,
				 running_run,
				 NULL,
				 NULL,
				 &states[STATE_CLOUD_DISCONNECTED]),
	[STATE_CLOUD_DISCONNECTED] =
		SMF_CREATE_STATE(cloud_disconnected_entry,
				 cloud_disconnected_run,
				 cloud_disconnected_exit,
				 &states[STATE_RUNNING],
				 NULL),
	[STATE_CLOUD_CONNECTED] =
		SMF_CREATE_STATE(cloud_connected_entry,
				 cloud_connected_run,
				 cloud_connected_exit,
				 &states[STATE_RUNNING],
				 NULL),
};

/** Convenience functions */

static void send_demo_cloud_message(void)
{
	struct cloud_msg msg = {
		.type = CLOUD_SEND_MESSAGE,
		.payload = DEMO_CLOUD_PAYLOAD,
	};
	int err;

	msg.payload_len = sizeof(DEMO_CLOUD_PAYLOAD) - 1;
	err = zbus_chan_pub(&cloud_chan, &msg, PUB_TIMEOUT);
	if (err) {
		LOG_ERR("zbus_chan_pub cloud_chan, error: %d", err);
		SEND_FATAL_ERROR();
	}
}

static void perform_cloud_synchronization(void)
{
	struct fota_msg fota_msg = { .type = FOTA_POLL_REQUEST };
	int err;

	send_demo_cloud_message();

	err = zbus_chan_pub(&fota_chan, &fota_msg, PUB_TIMEOUT);
	if (err) {
		LOG_ERR("zbus_chan_pub fota_chan, error: %d", err);
		SEND_FATAL_ERROR();
	}
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
		SEND_FATAL_ERROR();
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
	struct main_sync_msg msg = { .type = MAIN_CLOUD_SYNCHRONIZATION };
	int err;

	err = zbus_chan_pub(&main_sync_chan, &msg, PUB_TIMEOUT);
	if (err) {
		LOG_ERR("zbus_chan_pub main_sync_chan, error: %d", err);
		SEND_FATAL_ERROR();
	}

	cloud_sync_schedule(state);
}

/** SMF state functions */

static void running_entry(void *obj)
{
	ARG_UNUSED(obj);

	LOG_DBG("running_entry");
}

static enum smf_state_result running_run(void *obj)
{
	struct main_state *state_object = obj;

	if (state_object->chan == &main_sync_chan) {
		const struct main_sync_msg *msg =
			(const struct main_sync_msg *)state_object->msg_buf;

		if (msg->type == MAIN_CLOUD_SYNCHRONIZATION) {
			perform_cloud_synchronization();
		}

		return SMF_EVENT_HANDLED;
	}

	if (state_object->chan == &network_chan) {
		const struct network_msg *msg =
			(const struct network_msg *)state_object->msg_buf;

		switch (msg->type) {
		case NETWORK_CONNECTED:
			LOG_INF("Network connected");
			break;
		case NETWORK_DISCONNECTED:
			LOG_INF("Network disconnected");
			{
				struct fota_msg fota_msg = { .type = FOTA_NETWORK_DISCONNECTED };
				int err;

				err = zbus_chan_pub(&fota_chan, &fota_msg, PUB_TIMEOUT);
				if (err) {
					LOG_ERR("zbus_chan_pub fota_chan, error: %d", err);
					SEND_FATAL_ERROR();
				}
			}
			break;
		default:
			break;
		}

		return SMF_EVENT_HANDLED;
	}

	if (state_object->chan == &fota_chan) {
		const struct fota_msg *msg = (const struct fota_msg *)state_object->msg_buf;

		switch (msg->type) {
		case FOTA_NETWORK_DISCONNECT_NEEDED:
			LOG_INF("FOTA network disconnect needed");
			break;
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

	return SMF_EVENT_PROPAGATE;
}

static void cloud_disconnected_entry(void *obj)
{
	ARG_UNUSED(obj);

	LOG_DBG("cloud_disconnected_entry");
}

static void cloud_disconnected_exit(void *obj)
{
	ARG_UNUSED(obj);

	LOG_DBG("cloud_disconnected_exit");
}

static enum smf_state_result cloud_disconnected_run(void *obj)
{
	struct main_state *state_object = obj;

	if (state_object->chan != &cloud_chan) {
		return SMF_EVENT_PROPAGATE;
	}

	const struct cloud_msg *msg = (const struct cloud_msg *)state_object->msg_buf;

	switch (msg->type) {
	case CLOUD_CONNECTED:
		LOG_INF("Cloud connected");
		smf_set_state(SMF_CTX(state_object), &states[STATE_CLOUD_CONNECTED]);
		break;
	default:
		break;
	}

	return SMF_EVENT_HANDLED;
}

static void cloud_connected_entry(void *obj)
{
	struct main_state *state_object = obj;

	LOG_DBG("cloud_connected_entry");

	perform_cloud_synchronization();
	cloud_sync_schedule(state_object);
}

static void cloud_connected_exit(void *obj)
{
	struct main_state *state_object = obj;

	LOG_DBG("cloud_connected_exit");

	cloud_sync_cancel(state_object);
}

static enum smf_state_result cloud_connected_run(void *obj)
{
	struct main_state *state_object = obj;

	if (state_object->chan != &cloud_chan) {
		return SMF_EVENT_PROPAGATE;
	}

	const struct cloud_msg *msg = (const struct cloud_msg *)state_object->msg_buf;

	switch (msg->type) {
	case CLOUD_DISCONNECTED:
		LOG_INF("Cloud disconnected");
		smf_set_state(SMF_CTX(state_object), &states[STATE_CLOUD_DISCONNECTED]);
		break;
	case CLOUD_MESSAGE_SENT:
		LOG_INF("Cloud message sent");
		break;
	default:
		break;
	}

	return SMF_EVENT_HANDLED;
}

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

/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/smf.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/task_wdt/task_wdt.h>
#include <zephyr/dfu/mcuboot.h>
#include <dfu/dfu_target.h>
#include <net/fota_download.h>
#include <net/nrf_cloud.h>
#include <net/nrf_cloud_fota_poll.h>

#include "app_common.h"
#include "fota.h"

/* Register log module */
LOG_MODULE_REGISTER(fota, CONFIG_APP_FOTA_LOG_LEVEL);

BUILD_ASSERT(CONFIG_APP_FOTA_WATCHDOG_TIMEOUT_SECONDS >
	     CONFIG_APP_FOTA_MSG_PROCESSING_TIMEOUT_SECONDS,
	     "Watchdog timeout must be greater than maximum message processing time");

/* Register message subscriber - will be called everytime a channel that the module listens on
 * receives a new message.
 */
ZBUS_MSG_SUBSCRIBER_DEFINE(fota);

/* Define FOTA channel */
ZBUS_CHAN_DEFINE(fota_chan,
		 struct fota_msg,
		 NULL,
		 NULL,
		 ZBUS_OBSERVERS_EMPTY,
		 ZBUS_MSG_INIT(0)
);

/* Private channel message types for internal state management. */
enum priv_fota_msg_type {
	/* The firmware download has started. */
	FOTA_PRIV_DOWNLOADING,
	/* Reboot is required to apply the downloaded image. */
	FOTA_PRIV_REBOOT_NEEDED,
	/* FOTA sequence has been aborted. */
	FOTA_PRIV_ABORTED,
};

struct priv_fota_msg {
	enum priv_fota_msg_type type;
};

/* Create private fota channel for internal messaging that is not intended for external use. */
ZBUS_CHAN_DEFINE(priv_fota_chan,
		 struct priv_fota_msg,
		 NULL,
		 NULL,
		 ZBUS_OBSERVERS_EMPTY,
		 ZBUS_MSG_INIT(0)
);

/* Define the channels that the module subscribes to, their associated message types
 * and the subscriber that will receive the messages on the channel.
 */
#define CHANNEL_LIST(X)							\
	X(fota_chan,		struct fota_msg)			\
	X(priv_fota_chan,	struct priv_fota_msg)			\

/* Calculate the maximum message size from the list of channels */
#define MAX_MSG_SIZE			MAX_MSG_SIZE_FROM_LIST(CHANNEL_LIST)

/* Add the fota subscriber as observer to all the channels in the list. */
#define ADD_OBSERVERS(_chan, _type)	ZBUS_CHAN_ADD_OBS(_chan, fota, 0);

CHANNEL_LIST(ADD_OBSERVERS)

/* State machine */

enum fota_module_state {
	/* The module is initialized and running */
	STATE_RUNNING,
		/* The module is waiting for a poll request */
		STATE_WAITING_FOR_POLL_REQUEST,
		/* The module is polling for an update */
		STATE_POLLING_FOR_UPDATE,
		/* The module is downloading an update */
		STATE_DOWNLOADING_UPDATE,
		/* The module is waiting for the application to disconnect the network before
		 * triggering a reboot.
		 */
		STATE_AWAITING_NETWORK_DOWN_BEFORE_REBOOT,
		/* The FOTA module is waiting for a reboot */
		STATE_REBOOT_PENDING,
		/* The FOTA module is canceling the job */
		STATE_CANCELING,
};

/* State object.
 * Used to transfer context data between state changes.
 */
struct fota_state_object {
	/* This must be first */
	struct smf_ctx ctx;

	/* Last channel type that a message was received on */
	const struct zbus_channel *chan;

	/* Buffer for last zbus message */
	uint8_t msg_buf[MAX_MSG_SIZE];
};

/* Forward declarations of state handlers */
static void state_running_entry(void *obj);
static enum smf_state_result state_running_run(void *obj);
static enum smf_state_result state_waiting_for_poll_request_run(void *obj);
static void state_polling_for_update_entry(void *obj);
static enum smf_state_result state_polling_for_update_run(void *obj);
static void state_downloading_update_entry(void *obj);
static enum smf_state_result state_downloading_update_run(void *obj);
static void state_awaiting_network_down_before_reboot_entry(void *obj);
static enum smf_state_result state_awaiting_network_down_before_reboot_run(void *obj);
static void state_reboot_pending_entry(void *obj);
static void state_canceling_entry(void *obj);
static enum smf_state_result state_canceling_run(void *obj);

static const struct smf_state states[] = {
	[STATE_RUNNING] =
		SMF_CREATE_STATE(state_running_entry,
				 state_running_run,
				 NULL,
				 NULL,	/* No parent state */
				 &states[STATE_WAITING_FOR_POLL_REQUEST]),
	[STATE_WAITING_FOR_POLL_REQUEST] =
		SMF_CREATE_STATE(NULL,
				 state_waiting_for_poll_request_run,
				 NULL,
				 &states[STATE_RUNNING],
				 NULL),
	[STATE_POLLING_FOR_UPDATE] =
		SMF_CREATE_STATE(state_polling_for_update_entry,
				 state_polling_for_update_run,
				 NULL,
				 &states[STATE_RUNNING],
				 NULL),
	[STATE_DOWNLOADING_UPDATE] =
		SMF_CREATE_STATE(state_downloading_update_entry,
				 state_downloading_update_run,
				 NULL,
				 &states[STATE_RUNNING],
				 NULL),
	[STATE_AWAITING_NETWORK_DOWN_BEFORE_REBOOT] =
		SMF_CREATE_STATE(state_awaiting_network_down_before_reboot_entry,
				 state_awaiting_network_down_before_reboot_run,
				 NULL,
				 &states[STATE_RUNNING],
				 NULL),
	[STATE_REBOOT_PENDING] =
		SMF_CREATE_STATE(state_reboot_pending_entry,
				 NULL,
				 NULL,
				 &states[STATE_RUNNING],
				 NULL),
	[STATE_CANCELING] =
		SMF_CREATE_STATE(state_canceling_entry,
				 state_canceling_run,
				 NULL,
				 &states[STATE_RUNNING],
				 NULL),
};

/* Helpers */

static void publish_fota_event(enum fota_msg_type type)
{
	int err;
	struct fota_msg evt = { .type = type };

	err = zbus_chan_pub(&fota_chan, &evt, PUB_TIMEOUT);
	if (err) {
		LOG_ERR("zbus_chan_pub fota_chan, error: %d", err);
		SEND_FATAL_ERROR();
	}
}

static void publish_priv_fota(enum priv_fota_msg_type type)
{
	int err;
	struct priv_fota_msg msg = { .type = type };

	err = zbus_chan_pub(&priv_fota_chan, &msg, PUB_TIMEOUT);
	if (err) {
		LOG_ERR("zbus_chan_pub priv_fota_chan, error: %d", err);
		SEND_FATAL_ERROR();
	}
}

/* FOTA support functions */

/* nRF Cloud FOTA poll context. The Memfault NCS override backend --wraps
 * nrf_cloud_coap_fota_job_get(), so polling pulls Memfault OTA releases over the
 * nRF Cloud CoAP download proxy. status_fn is left NULL, so
 * nrf_cloud_fota_poll_process() runs synchronously (blocking download) and
 * invokes reboot_fn once the image is staged.
 */
static atomic_t reboot_requested;

static void fota_reboot_handler(enum nrf_cloud_fota_reboot_status status)
{
	/* Defer the reboot to the state machine so the network link is brought
	 * down first; here we only record that the poll wants a reboot.
	 */
	LOG_INF("FOTA reboot requested (status %d)", status);
	atomic_set(&reboot_requested, 1);
}

static struct nrf_cloud_fota_poll_ctx fota_poll_ctx = {
	.reboot_fn = fota_reboot_handler,
};

static void fota_wdt_callback(int channel_id, void *user_data)
{
	LOG_ERR("Watchdog expired, Channel: %d, Thread: %s",
		channel_id, k_thread_name_get((k_tid_t)user_data));

	SEND_FATAL_ERROR_WATCHDOG_TIMEOUT();
}

/* State handlers */

static void state_running_entry(void *obj)
{
	ARG_UNUSED(obj);

	LOG_DBG("%s", __func__);

#if defined(CONFIG_MCUBOOT_IMG_MANAGER)
	/* Confirm the image so MCUboot does not revert it on the next boot. */
	if (!boot_is_img_confirmed()) {
		int err = boot_write_img_confirmed();

		if (err) {
			LOG_ERR("boot_write_img_confirmed, error: %d", err);
		} else {
			LOG_INF("Running image confirmed");
		}
	}
#endif /* CONFIG_MCUBOOT_IMG_MANAGER */

	publish_fota_event(FOTA_MODULE_READY);
}

static enum smf_state_result state_running_run(void *obj)
{
	struct fota_state_object const *state_object = obj;

	if (&fota_chan == state_object->chan) {
		const struct fota_msg *msg = (const struct fota_msg *)state_object->msg_buf;

		if (msg->type == FOTA_DOWNLOAD_CANCEL) {
			smf_set_state(SMF_CTX(state_object), &states[STATE_CANCELING]);

			return SMF_EVENT_HANDLED;
		}
	}

	return SMF_EVENT_PROPAGATE;
}

static enum smf_state_result state_waiting_for_poll_request_run(void *obj)
{
	struct fota_state_object const *state_object = obj;

	if (&fota_chan == state_object->chan) {
		const struct fota_msg *msg = (const struct fota_msg *)state_object->msg_buf;

		if (msg->type == FOTA_POLL_REQUEST) {
			smf_set_state(SMF_CTX(state_object), &states[STATE_POLLING_FOR_UPDATE]);

			return SMF_EVENT_HANDLED;
		} else if (msg->type == FOTA_DOWNLOAD_CANCEL) {
			LOG_DBG("No ongoing FOTA update, nothing to cancel");

			return SMF_EVENT_HANDLED;
		}
	}

	return SMF_EVENT_PROPAGATE;
}

static void state_polling_for_update_entry(void *obj)
{
	static bool poll_initialized;
	int err;

	ARG_UNUSED(obj);

	LOG_DBG("%s", __func__);

	if (!poll_initialized) {
		err = nrf_cloud_fota_poll_init(&fota_poll_ctx);
		if (err) {
			LOG_ERR("nrf_cloud_fota_poll_init, error: %d", err);
			publish_priv_fota(FOTA_PRIV_ABORTED);
			return;
		}
		poll_initialized = true;
	}

	/* Blocking: checks Memfault for a release (via the --wrap'd job fetch),
	 * downloads it over the CoAP proxy, stages it, and calls reboot_fn.
	 * -EAGAIN = no job; -EBUSY = image staged, reboot pending.
	 */
	atomic_set(&reboot_requested, 0);
	err = nrf_cloud_fota_poll_process(&fota_poll_ctx);

	if (atomic_get(&reboot_requested) || err == -EBUSY) {
		LOG_INF("FOTA update staged for MCUboot");
		publish_priv_fota(FOTA_PRIV_REBOOT_NEEDED);
	} else if (err == -EAGAIN) {
		LOG_DBG("No FOTA update available");
		publish_priv_fota(FOTA_PRIV_ABORTED);
	} else {
		LOG_WRN("nrf_cloud_fota_poll_process, error: %d", err);
		publish_priv_fota(FOTA_PRIV_ABORTED);
	}
}

static enum smf_state_result state_polling_for_update_run(void *obj)
{
	struct fota_state_object const *state_object = obj;

	if (&priv_fota_chan == state_object->chan) {
		const struct priv_fota_msg *msg =
			(const struct priv_fota_msg *)state_object->msg_buf;

		switch (msg->type) {
		case FOTA_PRIV_REBOOT_NEEDED:
			/* Blocking poll already downloaded + staged the image. */
			publish_fota_event(FOTA_STARTING);
			smf_set_state(SMF_CTX(state_object),
				      &states[STATE_AWAITING_NETWORK_DOWN_BEFORE_REBOOT]);

			return SMF_EVENT_HANDLED;
		case FOTA_PRIV_ABORTED:
			publish_fota_event(FOTA_ABORTED);
			smf_set_state(SMF_CTX(state_object),
				      &states[STATE_WAITING_FOR_POLL_REQUEST]);

			return SMF_EVENT_HANDLED;
		default:
			break;
		}
	} else if (&fota_chan == state_object->chan) {
		const struct fota_msg *msg = (const struct fota_msg *)state_object->msg_buf;

		if (msg->type == FOTA_DOWNLOAD_CANCEL) {
			LOG_DBG("No ongoing FOTA update, nothing to cancel");

			return SMF_EVENT_HANDLED;
		}
	}

	return SMF_EVENT_PROPAGATE;
}

static void state_downloading_update_entry(void *obj)
{
	ARG_UNUSED(obj);

	LOG_DBG("%s", __func__);

	publish_fota_event(FOTA_STARTING);
}

static enum smf_state_result state_downloading_update_run(void *obj)
{
	struct fota_state_object const *state_object = obj;

	if (&priv_fota_chan == state_object->chan) {
		const struct priv_fota_msg *msg =
			(const struct priv_fota_msg *)state_object->msg_buf;

		switch (msg->type) {
		case FOTA_PRIV_REBOOT_NEEDED:
			smf_set_state(SMF_CTX(state_object),
				      &states[STATE_AWAITING_NETWORK_DOWN_BEFORE_REBOOT]);

			return SMF_EVENT_HANDLED;
		case FOTA_PRIV_ABORTED:
			publish_fota_event(FOTA_ABORTED);
			smf_set_state(SMF_CTX(state_object),
				      &states[STATE_WAITING_FOR_POLL_REQUEST]);

			return SMF_EVENT_HANDLED;
		default:
			break;
		}
	}

	return SMF_EVENT_PROPAGATE;
}

static void state_awaiting_network_down_before_reboot_entry(void *obj)
{
	ARG_UNUSED(obj);

	LOG_DBG("%s", __func__);

	publish_fota_event(FOTA_NETWORK_DISCONNECT_NEEDED);
}

static enum smf_state_result state_awaiting_network_down_before_reboot_run(void *obj)
{
	struct fota_state_object const *state_object = obj;

	if (&fota_chan == state_object->chan) {
		const struct fota_msg *msg = (const struct fota_msg *)state_object->msg_buf;

		if (msg->type == FOTA_NETWORK_DISCONNECTED) {
			smf_set_state(SMF_CTX(state_object), &states[STATE_REBOOT_PENDING]);

			return SMF_EVENT_HANDLED;
		}
	}

	return SMF_EVENT_PROPAGATE;
}

static void state_reboot_pending_entry(void *obj)
{
	ARG_UNUSED(obj);

	LOG_DBG("%s", __func__);
	LOG_DBG("Waiting for the application to reboot in order to apply the update");

	publish_fota_event(FOTA_SUCCESS);
}

static void state_canceling_entry(void *obj)
{
	int err;

	ARG_UNUSED(obj);

	LOG_DBG("%s", __func__);
	LOG_DBG("Canceling download");

	err = fota_download_cancel();
	if (err) {
		LOG_ERR("fota_download_cancel, error: %d", err);
		SEND_FATAL_ERROR();
	}
}

static enum smf_state_result state_canceling_run(void *obj)
{
	struct fota_state_object const *state_object = obj;

	if (&priv_fota_chan == state_object->chan) {
		const struct priv_fota_msg *msg =
			(const struct priv_fota_msg *)state_object->msg_buf;

		if (msg->type == FOTA_PRIV_ABORTED) {
			publish_fota_event(FOTA_ABORTED);
			smf_set_state(SMF_CTX(state_object),
				      &states[STATE_WAITING_FOR_POLL_REQUEST]);

			return SMF_EVENT_HANDLED;
		}
	}

	return SMF_EVENT_PROPAGATE;
}

static void fota_module_thread(void)
{
	int err;
	int task_wdt_id;
	const uint32_t wdt_timeout_ms = (CONFIG_APP_FOTA_WATCHDOG_TIMEOUT_SECONDS * MSEC_PER_SEC);
	const uint32_t execution_time_ms =
		(CONFIG_APP_FOTA_MSG_PROCESSING_TIMEOUT_SECONDS * MSEC_PER_SEC);
	const k_timeout_t zbus_wait_ms = K_MSEC(wdt_timeout_ms - execution_time_ms);
	static struct fota_state_object fota_state;

	LOG_DBG("FOTA module task started");

	task_wdt_id = task_wdt_add(wdt_timeout_ms, fota_wdt_callback, (void *)k_current_get());
	if (task_wdt_id < 0) {
		LOG_ERR("Failed to add task to watchdog: %d", task_wdt_id);
		SEND_FATAL_ERROR();
		return;
	}

	smf_set_initial(SMF_CTX(&fota_state), &states[STATE_RUNNING]);

	while (true) {
		err = task_wdt_feed(task_wdt_id);
		if (err) {
			LOG_ERR("task_wdt_feed, error: %d", err);
			SEND_FATAL_ERROR();
			return;
		}

		err = zbus_sub_wait_msg(&fota, &fota_state.chan, fota_state.msg_buf, zbus_wait_ms);
		if (err == -ENOMSG) {
			continue;
		} else if (err) {
			LOG_ERR("zbus_sub_wait_msg, error: %d", err);
			SEND_FATAL_ERROR();
			return;
		}

		err = smf_run_state(SMF_CTX(&fota_state));
		if (err) {
			LOG_ERR("smf_run_state(), error: %d", err);
			SEND_FATAL_ERROR();
			return;
		}
	}
}

K_THREAD_DEFINE(fota_module_thread_id,
		CONFIG_APP_FOTA_THREAD_STACK_SIZE,
		fota_module_thread, NULL, NULL, NULL, K_LOWEST_APPLICATION_THREAD_PRIO, 0, 0);

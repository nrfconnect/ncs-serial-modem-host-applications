/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/smf.h>
#include <zephyr/task_wdt/task_wdt.h>
#include <zephyr/dfu/mcuboot.h>
#include <dfu/dfu_target.h>
#include <net/fota_download.h>
#include <net/nrf_cloud.h>
#include <net/nrf_cloud_fota_poll.h>

#include "app_common.h"
#include "fota.h"

LOG_MODULE_REGISTER(fota, CONFIG_APP_FOTA_LOG_LEVEL);

BUILD_ASSERT(CONFIG_APP_FOTA_WATCHDOG_TIMEOUT_SECONDS >
	     CONFIG_APP_FOTA_MSG_PROCESSING_TIMEOUT_SECONDS,
	     "Watchdog timeout must be greater than maximum message processing time");

ZBUS_MSG_SUBSCRIBER_DEFINE(fota);

ZBUS_CHAN_DEFINE(fota_chan,
		 struct fota_msg,
		 NULL,
		 NULL,
		 ZBUS_OBSERVERS_EMPTY,
		 ZBUS_MSG_INIT(0)
);

enum priv_fota_msg_type {
	FOTA_PRIV_DOWNLOADING,
	FOTA_PRIV_REBOOT_NEEDED,
	FOTA_PRIV_ABORTED,
};

struct priv_fota_msg {
	enum priv_fota_msg_type type;
};

ZBUS_CHAN_DEFINE(priv_fota_chan,
		 struct priv_fota_msg,
		 NULL,
		 NULL,
		 ZBUS_OBSERVERS_EMPTY,
		 ZBUS_MSG_INIT(0)
);

#define CHANNEL_LIST(X)							\
	X(fota_chan,		struct fota_msg)			\
	X(priv_fota_chan,	struct priv_fota_msg)

#define MAX_MSG_SIZE			MAX_MSG_SIZE_FROM_LIST(CHANNEL_LIST)

#define ADD_OBSERVERS(_chan, _type)	ZBUS_CHAN_ADD_OBS(_chan, fota, 0);

CHANNEL_LIST(ADD_OBSERVERS)

enum fota_module_state {
	STATE_RUNNING,
	STATE_WAITING_FOR_POLL_REQUEST,
	STATE_POLLING_FOR_UPDATE,
	STATE_DOWNLOADING_UPDATE,
	STATE_REBOOT_PENDING,
	STATE_CANCELING,
};

struct fota_state_object {
	struct smf_ctx ctx;
	const struct zbus_channel *chan;
	uint8_t msg_buf[MAX_MSG_SIZE];
	struct nrf_cloud_fota_poll_ctx fota_ctx;
};

static void state_running_entry(void *obj);
static enum smf_state_result state_running_run(void *obj);
static enum smf_state_result state_waiting_for_poll_request_run(void *obj);
static void state_polling_for_update_entry(void *obj);
static enum smf_state_result state_polling_for_update_run(void *obj);
static void state_downloading_update_entry(void *obj);
static enum smf_state_result state_downloading_update_run(void *obj);
static void state_reboot_pending_entry(void *obj);
static void state_canceling_entry(void *obj);
static enum smf_state_result state_canceling_run(void *obj);

static const struct smf_state states[] = {
	[STATE_RUNNING] =
		SMF_CREATE_STATE(state_running_entry,
				 state_running_run,
				 NULL,
				 NULL,
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

static void publish_fota_event(enum fota_msg_type type)
{
	int err;
	struct fota_msg evt = { .type = type };

	err = zbus_chan_pub(&fota_chan, &evt, PUB_TIMEOUT);
	if (err) {
		LOG_ERR("zbus_chan_pub fota_chan, error: %d", err);
		FATAL_ERROR();
	}
}

static void publish_priv_fota(enum priv_fota_msg_type type)
{
	int err;
	struct priv_fota_msg msg = { .type = type };

	err = zbus_chan_pub(&priv_fota_chan, &msg, PUB_TIMEOUT);
	if (err) {
		LOG_ERR("zbus_chan_pub priv_fota_chan, error: %d", err);
		FATAL_ERROR();
	}
}

static void fota_reboot(enum nrf_cloud_fota_reboot_status status)
{
	LOG_DBG("Reboot requested with FOTA status %d", status);

	publish_priv_fota(FOTA_PRIV_REBOOT_NEEDED);
}

static void fota_status(enum nrf_cloud_fota_status status, const char *const status_details)
{
	LOG_DBG("FOTA status: %d, details: %s", status,
		status_details ? status_details : "None");

	switch (status) {
	case NRF_CLOUD_FOTA_DOWNLOADING:
		LOG_DBG("Downloading firmware update");

		publish_priv_fota(FOTA_PRIV_DOWNLOADING);
		break;
	case NRF_CLOUD_FOTA_FAILED:
		LOG_WRN("Firmware download failed");

		publish_priv_fota(FOTA_PRIV_ABORTED);
		break;
	case NRF_CLOUD_FOTA_CANCELED:
		LOG_WRN("Firmware download canceled");

		publish_priv_fota(FOTA_PRIV_ABORTED);
		break;
	case NRF_CLOUD_FOTA_REJECTED:
		LOG_WRN("Firmware update rejected");

		publish_priv_fota(FOTA_PRIV_ABORTED);
		break;
	case NRF_CLOUD_FOTA_TIMED_OUT:
		LOG_WRN("Firmware download timed out");

		publish_priv_fota(FOTA_PRIV_ABORTED);
		break;
	case NRF_CLOUD_FOTA_SUCCEEDED:
		LOG_DBG("Firmware update succeeded");
		LOG_DBG("Waiting for reboot request from the nRF Cloud FOTA Poll library");
		break;
	default:
		LOG_DBG("Unknown FOTA status: %d", status);
		break;
	}
}

static void fota_wdt_callback(int channel_id, void *user_data)
{
	LOG_ERR("Watchdog expired, Channel: %d, Thread: %s",
		channel_id, k_thread_name_get((k_tid_t)user_data));

	FATAL_ERROR_WATCHDOG_TIMEOUT();
}

static void state_running_entry(void *obj)
{
	int err;
	struct fota_state_object *state_object = obj;

	LOG_DBG("%s", __func__);

	err = nrf_cloud_fota_poll_init(&state_object->fota_ctx);
	if (err) {
		LOG_ERR("nrf_cloud_fota_poll_init, error: %d", err);
		FATAL_ERROR();
	}

#if defined(CONFIG_MCUBOOT_IMG_MANAGER)
	if (!boot_is_img_confirmed()) {
		err = boot_write_img_confirmed();

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
	} else if (&priv_fota_chan == state_object->chan) {
		const struct priv_fota_msg *msg =
			(const struct priv_fota_msg *)state_object->msg_buf;

		if (msg->type == FOTA_PRIV_REBOOT_NEEDED) {
			smf_set_state(SMF_CTX(state_object), &states[STATE_REBOOT_PENDING]);

			return SMF_EVENT_HANDLED;
		}
	}

	return SMF_EVENT_PROPAGATE;
}

static void state_polling_for_update_entry(void *obj)
{
	struct fota_state_object *state_object = obj;
	int err;

	LOG_DBG("%s", __func__);

	err = nrf_cloud_fota_poll_process(&state_object->fota_ctx);

	if (err == -EINVAL) {
		LOG_ERR("nrf_cloud_fota_poll_process, error: %d", err);
		FATAL_ERROR();
		return;
	} else if (err) {
		LOG_DBG("No FOTA job available");
		publish_priv_fota(FOTA_PRIV_ABORTED);
		return;
	}

	LOG_DBG("Job available, FOTA processing started");
}

static enum smf_state_result state_polling_for_update_run(void *obj)
{
	struct fota_state_object const *state_object = obj;

	if (&priv_fota_chan == state_object->chan) {
		const struct priv_fota_msg *msg =
			(const struct priv_fota_msg *)state_object->msg_buf;

		switch (msg->type) {
		case FOTA_PRIV_DOWNLOADING:
			smf_set_state(SMF_CTX(state_object), &states[STATE_DOWNLOADING_UPDATE]);

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
			smf_set_state(SMF_CTX(state_object), &states[STATE_REBOOT_PENDING]);

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

static void state_reboot_pending_entry(void *obj)
{
	ARG_UNUSED(obj);

	LOG_DBG("%s", __func__);
	LOG_DBG("Waiting for the application to reboot in order to apply the update");

	publish_fota_event(FOTA_REQUEST_REBOOT);
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
		FATAL_ERROR();
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
	static struct fota_state_object fota_state = {
		.fota_ctx.reboot_fn = fota_reboot,
		.fota_ctx.status_fn = fota_status,
	};

	LOG_DBG("FOTA module task started");

	task_wdt_id = task_wdt_add(wdt_timeout_ms, fota_wdt_callback, (void *)k_current_get());
	if (task_wdt_id < 0) {
		LOG_ERR("Failed to add task to watchdog: %d", task_wdt_id);
		FATAL_ERROR();
		return;
	}

	smf_set_initial(SMF_CTX(&fota_state), &states[STATE_RUNNING]);

	while (true) {
		err = task_wdt_feed(task_wdt_id);
		if (err) {
			LOG_ERR("task_wdt_feed, error: %d", err);
			FATAL_ERROR();
			return;
		}

		err = zbus_sub_wait_msg(&fota, &fota_state.chan, fota_state.msg_buf, zbus_wait_ms);
		if (err == -ENOMSG) {
			continue;
		} else if (err) {
			LOG_ERR("zbus_sub_wait_msg, error: %d", err);
			FATAL_ERROR();
			return;
		}

		err = smf_run_state(SMF_CTX(&fota_state));
		if (err) {
			LOG_ERR("smf_run_state(), error: %d", err);
			FATAL_ERROR();
			return;
		}
	}
}

K_THREAD_DEFINE(fota_module_thread_id,
		CONFIG_APP_FOTA_THREAD_STACK_SIZE,
		fota_module_thread, NULL, NULL, NULL, K_LOWEST_APPLICATION_THREAD_PRIO, 0, 0);

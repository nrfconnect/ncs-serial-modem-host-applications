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
#include <memfault/ports/zephyr/http.h>
#if defined(CONFIG_MEMFAULT_USE_NRF_CLOUD_COAP)
#include <net/nrf_cloud_coap.h>
#include <date_time.h>
#endif

#include "app_common.h"
#include "modules/network/network.h"
#if defined(CONFIG_APP_FOTA)
#include "modules/fota/fota.h"
#endif

LOG_MODULE_REGISTER(memfault_module, CONFIG_APP_MEMFAULT_LOG_LEVEL);

BUILD_ASSERT(CONFIG_APP_MEMFAULT_WATCHDOG_TIMEOUT_SECONDS >
	     CONFIG_APP_MEMFAULT_MSG_PROCESSING_TIMEOUT_SECONDS,
	     "Watchdog timeout must be greater than maximum message processing time");

/* On each (re)connect: upload pending Memfault data and trigger a FOTA check.
 * Root CAs are installed at boot by the NCS integration; periodic upload runs separately.
 */
ZBUS_MSG_SUBSCRIBER_DEFINE(memfault_subscriber);
ZBUS_CHAN_ADD_OBS(network_chan, memfault_subscriber, 0);

#if defined(CONFIG_MEMFAULT_USE_NRF_CLOUD_COAP)
#define DATE_TIME_WAIT_SECONDS 120

static K_SEM_DEFINE(date_time_ready, 0, 1);

static void date_time_evt_handler(const struct date_time_evt *evt)
{
	switch (evt->type) {
	case DATE_TIME_OBTAINED_MODEM:
	case DATE_TIME_OBTAINED_NTP:
	case DATE_TIME_OBTAINED_EXT:
		k_sem_give(&date_time_ready);
		break;
	default:
		break;
	}
}

/* The nRF Cloud CoAP transport (used to forward Memfault chunks) needs a
 * one-time init and a valid clock, since the auth JWT is signed with the
 * current time. The transport itself opens/keeps the DTLS connection on
 * the first post.
 */
static int coap_prepare(void)
{
	static bool initialized;
	int err;

	if (!initialized) {
		err = nrf_cloud_coap_init();
		if (err) {
			LOG_ERR("nrf_cloud_coap_init, error: %d", err);
			return err;
		}
		initialized = true;
	}

	if (date_time_is_valid()) {
		return 0;
	}

	/* Kick off the fetch (NTP over PPP); date/time does not auto-update here. */
	LOG_INF("Updating date/time for the CoAP JWT");
	date_time_update_async(date_time_evt_handler);

	int64_t deadline = k_uptime_get() + (DATE_TIME_WAIT_SECONDS * MSEC_PER_SEC);

	while (k_uptime_get() < deadline) {
		if (k_sem_take(&date_time_ready, K_SECONDS(1)) == 0 && date_time_is_valid()) {
			return 0;
		}
	}

	LOG_WRN("No valid date/time within %ds, skipping upload", DATE_TIME_WAIT_SECONDS);
	return -EAGAIN;
}
#endif /* CONFIG_MEMFAULT_USE_NRF_CLOUD_COAP */

static void upload_memfault_data(void)
{
	int err;

#if defined(CONFIG_MEMFAULT_USE_NRF_CLOUD_COAP)
	if (coap_prepare() != 0) {
		return;
	}
#endif

	err = memfault_zephyr_port_post_data();
	if (err) {
		LOG_WRN("memfault_zephyr_port_post_data, error: %d", err);
		return;
	}

	LOG_INF("Memfault data uploaded");
}

#if defined(CONFIG_APP_FOTA)
static void request_fota_poll(void)
{
	struct fota_msg msg = { .type = FOTA_POLL_REQUEST };
	int err = zbus_chan_pub(&fota_chan, &msg, PUB_TIMEOUT);

	if (err) {
		LOG_ERR("zbus_chan_pub fota_chan, error: %d", err);
		SEND_FATAL_ERROR();
	}
}
#endif /* CONFIG_APP_FOTA */

static void memfault_wdt_callback(int channel_id, void *user_data)
{
	LOG_ERR("Memfault watchdog expired, channel: %d, thread: %s",
		channel_id, k_thread_name_get((k_tid_t)user_data));

	SEND_FATAL_ERROR_WATCHDOG_TIMEOUT();
}

static void memfault_module_thread(void)
{
	int err;
	int task_wdt_id;
	const struct zbus_channel *chan;
	uint8_t msg_buf[sizeof(struct network_msg)];
	const uint32_t wdt_timeout_ms =
		(CONFIG_APP_MEMFAULT_WATCHDOG_TIMEOUT_SECONDS * MSEC_PER_SEC);
	const uint32_t execution_time_ms =
		(CONFIG_APP_MEMFAULT_MSG_PROCESSING_TIMEOUT_SECONDS * MSEC_PER_SEC);
	const k_timeout_t zbus_wait = K_MSEC(wdt_timeout_ms - execution_time_ms);

	err = settings_subsys_init();
	if (err) {
		LOG_ERR("settings_subsys_init, error: %d", err);
		SEND_FATAL_ERROR();
	}

	task_wdt_id = task_wdt_add(wdt_timeout_ms, memfault_wdt_callback,
				   (void *)k_current_get());
	if (task_wdt_id < 0) {
		LOG_ERR("Failed to add task to watchdog: %d", task_wdt_id);
		SEND_FATAL_ERROR();
		return;
	}

	while (true) {
		err = task_wdt_feed(task_wdt_id);
		if (err) {
			LOG_ERR("task_wdt_feed, error: %d", err);
			SEND_FATAL_ERROR();
			return;
		}

		err = zbus_sub_wait_msg(&memfault_subscriber, &chan, msg_buf, zbus_wait);
		if (err == -ENOMSG) {
			continue;
		} else if (err) {
			LOG_ERR("zbus_sub_wait_msg, error: %d", err);
			SEND_FATAL_ERROR();
			return;
		}

		if (chan != &network_chan) {
			continue;
		}

		const struct network_msg *msg = (const struct network_msg *)msg_buf;

		if (msg->type == NETWORK_CONNECTED) {
			LOG_INF("Network connected, syncing with Memfault");
			upload_memfault_data();
#if defined(CONFIG_APP_FOTA)
			request_fota_poll();
#endif
		}
	}
}

K_THREAD_DEFINE(memfault_module_thread_id, CONFIG_APP_MEMFAULT_THREAD_STACK_SIZE,
		memfault_module_thread, NULL, NULL, NULL, 3, 0, 0);

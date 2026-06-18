/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/task_wdt/task_wdt.h>
#include <zephyr/zbus/zbus.h>

#include "app_common.h"
#include "modules/modem_at/modem_at.h"
#include "network.h"

LOG_MODULE_REGISTER(network, CONFIG_APP_NETWORK_LOG_LEVEL);

ZBUS_CHAN_DEFINE(network_chan,
		 struct network_msg,
		 NULL,
		 NULL,
		 ZBUS_OBSERVERS_EMPTY,
		 ZBUS_MSG_INIT(.type = NETWORK_DISCONNECTED));

static K_SEM_DEFINE(cereg_sem, 0, 1);
static atomic_t cereg_stat;

/* +CEREG URC: argv[1] is the registration <stat>. Hand it to the thread. */
static void on_cereg(char **argv, uint16_t argc, void *user_data)
{
	ARG_UNUSED(user_data);

	if (argc < 2 || argv[1] == NULL) {
		return;
	}

	atomic_set(&cereg_stat, atoi(argv[1]));
	k_sem_give(&cereg_sem);
}

static int network_attach(void)
{
	int err;

	err = modem_at_run("AT+CEREG=1", NULL, 0, 10);
	if (err) {
		LOG_ERR("AT+CEREG=1 failed: %d", err);
		return err;
	}

	if (sizeof(CONFIG_APP_NETWORK_APN) > 1) {
		char cmd[64];

		(void)snprintf(cmd, sizeof(cmd), "AT+CGDCONT=1,\"IP\",\"%s\"",
			       CONFIG_APP_NETWORK_APN);
		err = modem_at_run(cmd, NULL, 0, 10);
		if (err) {
			LOG_ERR("%s failed: %d", cmd, err);
			return err;
		}
	}

	err = modem_at_run("AT+CFUN=1", NULL, 0, 30);
	if (err) {
		LOG_ERR("AT+CFUN=1 failed: %d", err);
		return err;
	}

	LOG_INF("Modem attaching; waiting for registration");
	return 0;
}

static void publish(enum network_msg_type type)
{
	struct network_msg msg = { .type = type };

	(void)zbus_chan_pub(&network_chan, &msg, PUB_TIMEOUT);
}

static bool pdp_has_ip(void)
{
	char resp[64];
	const char *p;

	if (modem_at_run("AT+CGPADDR=1", resp, sizeof(resp), 5) != 0) {
		return false;
	}

	p = strstr(resp, "+CGPADDR:");
	return p != NULL && (strchr(p, '.') != NULL || strchr(p, ':') != NULL);
}

static void network_wdt_callback(int channel_id, void *user_data)
{
	LOG_ERR("Network watchdog expired, channel: %d, thread: %s",
		channel_id, k_thread_name_get((k_tid_t)user_data));

	SEND_FATAL_ERROR_WATCHDOG_TIMEOUT();
}

static void network_module(void)
{
	const uint32_t wdt_timeout_ms = CONFIG_APP_NETWORK_WATCHDOG_TIMEOUT_SECONDS * MSEC_PER_SEC;
	const uint32_t execution_time_ms =
		CONFIG_APP_NETWORK_MSG_PROCESSING_TIMEOUT_SECONDS * MSEC_PER_SEC;
	const k_timeout_t wait = K_MSEC(wdt_timeout_ms - execution_time_ms);
	int task_wdt_id;
	bool connected = false;

	(void)modem_at_urc_subscribe("+CEREG: ", on_cereg, NULL);

	task_wdt_id = task_wdt_add(wdt_timeout_ms, network_wdt_callback, (void *)k_current_get());
	if (task_wdt_id < 0) {
		LOG_ERR("task_wdt_add, error: %d", task_wdt_id);
		SEND_FATAL_ERROR();
		return;
	}

	while (!modem_at_is_ready()) {
		(void)task_wdt_feed(task_wdt_id);
		k_sleep(K_MSEC(500));
	}
	(void)network_attach();

	while (true) {
		bool reg = (atomic_get(&cereg_stat) == 1 || atomic_get(&cereg_stat) == 5);
		k_timeout_t wake = (reg && !connected) ? K_MSEC(500) : wait;

		(void)task_wdt_feed(task_wdt_id);
		(void)k_sem_take(&cereg_sem, wake);

		reg = (atomic_get(&cereg_stat) == 1 || atomic_get(&cereg_stat) == 5);

		if (reg && !connected && pdp_has_ip()) {
			connected = true;
			publish(NETWORK_CONNECTED);
		} else if (!reg && connected) {
			connected = false;
			publish(NETWORK_DISCONNECTED);
		}
	}
}

K_THREAD_DEFINE(network_thread, CONFIG_APP_NETWORK_THREAD_STACK_SIZE,
		network_module, NULL, NULL, NULL, K_LOWEST_APPLICATION_THREAD_PRIO, 0, 0);

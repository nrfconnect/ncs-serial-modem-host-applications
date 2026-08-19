/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/task_wdt/task_wdt.h>
#include <zephyr/zbus/zbus.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_common.h"
#include "modules/modem_at/modem_at.h"
#include "location.h"

LOG_MODULE_REGISTER(location, CONFIG_APP_LOCATION_LOG_LEVEL);

BUILD_ASSERT(CONFIG_APP_LOCATION_WATCHDOG_TIMEOUT_SECONDS >
	     CONFIG_APP_LOCATION_MSG_PROCESSING_TIMEOUT_SECONDS,
	     "Watchdog timeout must be greater than maximum message processing time");

ZBUS_CHAN_DEFINE(location_chan,
		 struct location_msg,
		 NULL,
		 NULL,
		 ZBUS_OBSERVERS_EMPTY,
		 ZBUS_MSG_INIT(0));

ZBUS_MSG_SUBSCRIBER_DEFINE(location);

#define CHANNEL_LIST(X) X(location_chan, struct location_msg)

#define MAX_MSG_SIZE MAX_MSG_SIZE_FROM_LIST(CHANNEL_LIST)

#define ADD_OBSERVERS(_chan, _type) ZBUS_CHAN_ADD_OBS(_chan, location, 0);

CHANNEL_LIST(ADD_OBSERVERS)

static char at_resp[CONFIG_APP_LOCATION_AT_RESPONSE_SIZE];

static void scan_cell(struct location_msg *msg);
static bool parse_wifi_ap(const char *entry, struct location_wifi_ap *ap);
static void scan_wifi(struct location_msg *msg);
static void scan_and_publish(enum location_mode mode);

/* %BCINFOSC: <earfcn>,<pci>,<rsrp>,<rsrq>,"<mcc>","<mnc>","<cellid>","<tac>" */
static void scan_cell(struct location_msg *msg)
{
	int earfcn;
	int pci;
	int rsrp;
	int rsrq;
	char mcc[8];
	char mnc[8];
	char cellid[12];
	char tac[8];
	int err;

	err = modem_at_run("AT%BCINFO=1", at_resp, sizeof(at_resp),
			   CONFIG_APP_LOCATION_BCINFO_TIMEOUT_SECONDS);
	if (err) {
		LOG_WRN("AT%%BCINFO failed, error: %d", err);
		return;
	}

	const char *entry = strstr(at_resp, "%BCINFOSC:");

	if (entry == NULL) {
		LOG_WRN("No serving cell in %%BCINFO response");
		return;
	}

	if (sscanf(entry,
		   "%%BCINFOSC: %d,%d,%d,%d,\"%7[^\"]\",\"%7[^\"]\",\"%11[^\"]\",\"%7[^\"]\"",
		   &earfcn, &pci, &rsrp, &rsrq, mcc, mnc, cellid, tac) != 8) {
		LOG_WRN("Could not parse %%BCINFOSC entry");
		return;
	}

	msg->cell.mcc = atoi(mcc);
	msg->cell.mnc = atoi(mnc);
	msg->cell.eci = strtoul(cellid, NULL, 16);
	msg->cell.tac = strtoul(tac, NULL, 16);
	msg->cell.rsrp = rsrp;
	msg->cell.rsrq = rsrq;
	msg->cell.earfcn = earfcn;
	msg->cell.pci = pci;
	msg->cell.valid = true;

	LOG_DBG("Cell: mcc=%d mnc=%d eci=%u tac=%u rsrp=%d earfcn=%d pci=%d",
		msg->cell.mcc, msg->cell.mnc, (unsigned int)msg->cell.eci,
		(unsigned int)msg->cell.tac, rsrp, earfcn, pci);
}

/* %WIFISCAN:(<ecn>,"<ssid>",<rssi>,"<mac>",<channel>), anchored on the quote pairs
 * because the SSID may contain commas.
 */
static bool parse_wifi_ap(const char *entry, struct location_wifi_ap *ap)
{
	const char *ssid_open = strchr(entry, '"');
	const char *ssid_close = ssid_open ? strchr(ssid_open + 1, '"') : NULL;
	const char *mac_open = ssid_close ? strchr(ssid_close + 1, '"') : NULL;
	const char *mac_close = mac_open ? strchr(mac_open + 1, '"') : NULL;
	unsigned int mac[WIFI_MAC_ADDR_LEN];
	int rssi;
	int channel;

	if (mac_close == NULL) {
		return false;
	}

	if (sscanf(mac_open + 1, "%2x:%2x:%2x:%2x:%2x:%2x",
		   &mac[0], &mac[1], &mac[2], &mac[3], &mac[4],
		   &mac[5]) != WIFI_MAC_ADDR_LEN) {
		return false;
	}

	if (sscanf(ssid_close + 1, ",%d", &rssi) != 1) {
		return false;
	}

	if (sscanf(mac_close + 1, ",%d", &channel) != 1) {
		return false;
	}

	if (rssi < INT8_MIN || rssi > INT8_MAX || channel < 0 || channel > UINT8_MAX) {
		LOG_WRN("Discarding AP with rssi %d, channel %d", rssi, channel);
		return false;
	}

	for (size_t i = 0; i < WIFI_MAC_ADDR_LEN; i++) {
		ap->mac[i] = (uint8_t)mac[i];
	}

	ap->rssi = (int8_t)rssi;
	ap->channel = (uint8_t)channel;

	return true;
}

static void scan_wifi(struct location_msg *msg)
{
	int err;

	err = modem_at_run("AT%WIFISCAN=12000,1,5", at_resp, sizeof(at_resp),
			   CONFIG_APP_LOCATION_WIFISCAN_TIMEOUT_SECONDS);
	if (err) {
		LOG_WRN("AT%%WIFISCAN failed, error: %d", err);
		return;
	}

	const char *entry = strstr(at_resp, "%WIFISCAN:");

	while (entry != NULL && msg->ap_count < CONFIG_APP_LOCATION_MAX_WIFI_APS) {
		struct location_wifi_ap *ap = &msg->aps[msg->ap_count];

		if (parse_wifi_ap(entry, ap)) {
			LOG_DBG("AP: channel=%u rssi=%d", ap->channel, ap->rssi);
			msg->ap_count++;
		}

		entry = strstr(entry + 1, "%WIFISCAN:");
	}

	LOG_DBG("Access points found: %u", msg->ap_count);
}

static void scan_and_publish(enum location_mode mode)
{
	static const char *const mode_str[] = { "cell and Wi-Fi", "cell", "Wi-Fi" };
	static struct location_msg msg;
	int err;

	LOG_INF("Scanning for measurements (%s)",
		((size_t)mode < ARRAY_SIZE(mode_str)) ? mode_str[mode] : "unknown mode");

	memset(&msg, 0, sizeof(msg));
	msg.type = LOCATION_SYNC_DONE;
	msg.mode = mode;

	if (mode != LOCATION_MODE_WIFI) {
		scan_cell(&msg);
	}

	if (mode != LOCATION_MODE_CELL) {
		scan_wifi(&msg);
	}

	if (!msg.cell.valid && msg.ap_count == 0) {
		LOG_WRN("No measurements to report");
	}

	/* Published even when empty: the sync sequence advances on this message. */
	err = zbus_chan_pub(&location_chan, &msg, PUB_TIMEOUT);
	if (err) {
		LOG_ERR("zbus_chan_pub location_chan, error: %d", err);
		FATAL_ERROR();
	}
}

static void location_wdt_callback(int channel_id, void *user_data)
{
	LOG_ERR("Location watchdog expired, channel: %d, thread: %s",
		channel_id, k_thread_name_get((k_tid_t)user_data));

	FATAL_ERROR_WATCHDOG_TIMEOUT();
}

static void location_thread(void)
{
	int err;
	int task_wdt_id;
	const struct zbus_channel *chan;
	static uint8_t msg_buf[MAX_MSG_SIZE];
	const uint32_t wdt_timeout_ms =
		(CONFIG_APP_LOCATION_WATCHDOG_TIMEOUT_SECONDS * MSEC_PER_SEC);
	const uint32_t execution_time_ms =
		(CONFIG_APP_LOCATION_MSG_PROCESSING_TIMEOUT_SECONDS * MSEC_PER_SEC);
	const k_timeout_t zbus_wait = K_MSEC(wdt_timeout_ms - execution_time_ms);

	task_wdt_id = task_wdt_add(wdt_timeout_ms, location_wdt_callback,
				   (void *)k_current_get());
	if (task_wdt_id < 0) {
		LOG_ERR("Failed to add task to watchdog: %d", task_wdt_id);
		FATAL_ERROR();
		return;
	}

	while (true) {
		err = task_wdt_feed(task_wdt_id);
		if (err) {
			LOG_ERR("task_wdt_feed, error: %d", err);
			FATAL_ERROR();
			return;
		}

		err = zbus_sub_wait_msg(&location, &chan, msg_buf, zbus_wait);
		if (err == -ENOMSG) {
			continue;
		} else if (err) {
			LOG_ERR("zbus_sub_wait_msg, error: %d", err);
			FATAL_ERROR();
			return;
		}

		const struct location_msg *msg = (const struct location_msg *)msg_buf;

		if (msg->type == LOCATION_FIX_REQUEST) {
			scan_and_publish(msg->mode);
		}
	}
}

K_THREAD_DEFINE(location_tid, CONFIG_APP_LOCATION_THREAD_STACK_SIZE, location_thread,
		NULL, NULL, NULL, K_LOWEST_APPLICATION_THREAD_PRIO, 0, 0);

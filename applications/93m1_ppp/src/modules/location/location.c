/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/sys/atomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <net/nrf_cloud_coap.h>
#include <net/nrf_cloud_location.h>
#include <net/wifi_location_common.h>
#include <modem/lte_lc.h>
#include <zephyr/net/wifi_mgmt.h>

#include "modules/modem_at/modem_at.h"
#include "modules/network/network.h"
#include "location.h"

LOG_MODULE_REGISTER(location, CONFIG_APP_LOCATION_LOG_LEVEL);

#define SAMPLE_INTERVAL K_SECONDS(CONFIG_APP_LOCATION_INTERVAL_SECONDS)

/* Single-cell + Wi-Fi measurements over the shared AT pipe (DLCI 3), resolved
 * to a position by the nRF Cloud ground-fix service over the shared CoAP/DTLS
 * session (the same session Memfault uses; no OAT/HTTPS).
 */
struct cell_info {
	bool valid;
	int mcc;
	int mnc;
	int rsrp;
	int rsrq;
	int earfcn;
	int pci;
	uint32_t eci;
	uint32_t tac;
};

struct wifi_ap {
	char mac[20];
	int channel;
	int rssi;
};

static char at_resp[CONFIG_APP_LOCATION_AT_RESPONSE_SIZE];
static struct cell_info cell;
static struct wifi_ap aps[CONFIG_APP_LOCATION_MAX_WIFI_APS];
static int ap_count;

static atomic_t connected;
static atomic_t requested_mode = ATOMIC_INIT(LOCATION_MODE_ALL);
static K_SEM_DEFINE(connected_sem, 0, 1);
static K_SEM_DEFINE(fix_trigger, 0, 1);

static void on_network(const struct zbus_channel *chan)
{
	const struct network_msg *msg = zbus_chan_const_msg(chan);

	if (msg->type == NETWORK_CONNECTED) {
		atomic_set(&connected, 1);
		k_sem_give(&connected_sem);
	} else if (msg->type == NETWORK_DISCONNECTED) {
		atomic_set(&connected, 0);
	}
}

ZBUS_LISTENER_DEFINE(location_lis, on_network);
ZBUS_CHAN_ADD_OBS(network_chan, location_lis, 0);

static void scan_cell(void)
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

	cell.valid = false;

	err = modem_at_run("AT%BCINFO=1", at_resp, sizeof(at_resp),
			   CONFIG_APP_LOCATION_BCINFO_TIMEOUT_SECONDS);
	if (err) {
		LOG_WRN("AT%%BCINFO failed, error: %d", err);
		return;
	}

	const char *p = strstr(at_resp, "%BCINFOSC:");

	if (p == NULL) {
		LOG_WRN("No serving cell in %%BCINFO response");
		return;
	}

	/* %BCINFOSC: <earfcn>,<pci>,<rsrp>,<rsrq>,"<mcc>","<mnc>","<cellid>","<tac>" */
	if (sscanf(p, "%%BCINFOSC: %d,%d,%d,%d,\"%7[^\"]\",\"%7[^\"]\",\"%11[^\"]\",\"%7[^\"]\"",
		   &earfcn, &pci, &rsrp, &rsrq, mcc, mnc, cellid, tac) != 8) {
		LOG_WRN("Could not parse %%BCINFOSC line");
		return;
	}

	cell.mcc = atoi(mcc);
	cell.mnc = atoi(mnc);
	cell.eci = strtoul(cellid, NULL, 16);
	cell.tac = strtoul(tac, NULL, 16);
	cell.rsrp = rsrp;
	cell.rsrq = rsrq;
	cell.earfcn = earfcn;
	cell.pci = pci;
	cell.valid = true;

	LOG_DBG("Cell: mcc=%d mnc=%d eci=%u tac=%u rsrp=%d (earfcn=%d pci=%d)",
		cell.mcc, cell.mnc, (unsigned int)cell.eci, (unsigned int)cell.tac, rsrp,
		earfcn, pci);
}

/* %WIFISCAN:(<ecn>,"<ssid>",<rssi>,"<mac>",<channel>)
 * The SSID is quoted and may be empty or contain commas/non-ASCII, so anchor on
 * the MAC's quotes (the 3rd and 4th) instead of splitting the line on commas.
 */
static bool parse_wifi_ap(const char *line, char *mac, size_t mac_size, int *rssi, int *channel)
{
	const char *q1 = strchr(line, '"');             /* ssid open  */
	const char *q2 = q1 ? strchr(q1 + 1, '"') : NULL; /* ssid close */
	const char *q3 = q2 ? strchr(q2 + 1, '"') : NULL; /* mac open   */
	const char *q4 = q3 ? strchr(q3 + 1, '"') : NULL; /* mac close  */

	if (q4 == NULL) {
		return false;
	}

	size_t mac_len = q4 - (q3 + 1);

	if (mac_len == 0 || mac_len >= mac_size) {
		return false;
	}
	memcpy(mac, q3 + 1, mac_len);
	mac[mac_len] = '\0';

	/* rssi sits between ssid and mac (...",<rssi>,"...); channel follows the mac. */
	return sscanf(q2 + 1, ",%d", rssi) == 1 && sscanf(q4 + 1, ",%d", channel) == 1;
}

static void scan_wifi(void)
{
	int err;

	ap_count = 0;

	err = modem_at_run("AT%WIFISCAN=12000,1,5", at_resp, sizeof(at_resp),
			   CONFIG_APP_LOCATION_WIFISCAN_TIMEOUT_SECONDS);
	if (err) {
		LOG_WRN("AT%%WIFISCAN failed, error: %d", err);
		return;
	}

	const char *p = strstr(at_resp, "%WIFISCAN:");

	while (p != NULL && ap_count < CONFIG_APP_LOCATION_MAX_WIFI_APS) {
		struct wifi_ap *ap = &aps[ap_count];

		if (parse_wifi_ap(p, ap->mac, sizeof(ap->mac), &ap->rssi, &ap->channel)) {
			LOG_DBG("WiFi: mac=%s ch=%d rssi=%d", ap->mac, ap->channel, ap->rssi);
			ap_count++;
		}

		p = strstr(p + 1, "%WIFISCAN:");
	}

	LOG_DBG("WiFi APs found: %d", ap_count);
}

/* nRF Cloud ground-fix over the shared CoAP/DTLS session (no OAT, no JSON). */
static struct wifi_scan_result coap_aps[CONFIG_APP_LOCATION_MAX_WIFI_APS];

static const char *fix_type_str(enum nrf_cloud_location_type type)
{
	switch (type) {
	case LOCATION_TYPE_SINGLE_CELL:
		return "SCELL";
	case LOCATION_TYPE_MULTI_CELL:
		return "MCELL";
	case LOCATION_TYPE_WIFI:
		return "WIFI";
	default:
		return "?";
	}
}

static void ground_fix(void)
{
	struct lte_lc_cells_info cells = {
		.current_cell = {
			.mcc = cell.mcc,
			.mnc = cell.mnc,
			.id = cell.eci,
			.tac = cell.tac,
			.earfcn = cell.earfcn,
			.phys_cell_id = (uint16_t)cell.pci,
			.timing_advance = LTE_LC_CELL_TIMING_ADVANCE_INVALID,
			/* Codec applies RSRP_IDX_TO_DBM(); convert dBm back to the index. */
			.rsrp = cell.valid ? (int16_t)CLAMP(cell.rsrp + 141, 0, 97)
					     : LTE_LC_CELL_RSRP_INVALID,
		},
	};
	struct wifi_scan_info wifi = { .ap_info = coap_aps };
	struct nrf_cloud_location_config config = { .do_reply = true, .fallback = true };
	struct nrf_cloud_coap_location_request req = { .config = &config };
	struct nrf_cloud_location_result result;
	int err;

	for (int i = 0; i < ap_count; i++) {
		unsigned int b[6];

		if (sscanf(aps[i].mac, "%x:%x:%x:%x:%x:%x",
			   &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) != 6) {
			continue;
		}
		for (int j = 0; j < 6; j++) {
			coap_aps[wifi.cnt].mac[j] = (uint8_t)b[j];
		}
		coap_aps[wifi.cnt].mac_length = WIFI_MAC_ADDR_LEN;
		coap_aps[wifi.cnt].channel = (uint8_t)aps[i].channel;
		coap_aps[wifi.cnt].rssi = (int8_t)aps[i].rssi;
		wifi.cnt++;
	}

	req.cell_info = cell.valid ? &cells : NULL;
	req.wifi_info = (wifi.cnt >= NRF_CLOUD_LOCATION_WIFI_AP_CNT_MIN) ? &wifi : NULL;

	if (req.cell_info == NULL && req.wifi_info == NULL) {
		LOG_WRN("No usable measurements for ground-fix");
		return;
	}

	err = nrf_cloud_coap_location_get(&req, &result);
	if (err) {
		LOG_WRN("nrf_cloud_coap_location_get, error: %d", err);
		return;
	}

	LOG_INF("Location: %.7f,%.7f Uncertainty: %um Type: %s",
		result.lat, result.lon, result.unc, fix_type_str(result.type));
}

static void do_fix(enum location_mode mode)
{
	static const char *const mode_str[] = { "cell+wifi", "cell", "wifi" };

	LOG_INF("Location requested (%s)", mode_str[mode]);

	/* Clear both so a skipped source isn't carried over from a previous fix. */
	cell.valid = false;
	ap_count = 0;

	if (mode != LOCATION_MODE_WIFI) {
		scan_cell();
	}
	if (mode != LOCATION_MODE_CELL) {
		scan_wifi();
	}

	ground_fix();
}

void location_trigger(enum location_mode mode)
{
	atomic_set(&requested_mode, mode);
	k_sem_give(&fix_trigger);
}

static void location_thread(void)
{
	/* On-boot: first fix shortly after the network is up (let the initial
	 * Memfault sync settle so the Wi-Fi scan isn't starved by data traffic).
	 */
	enum location_mode mode = LOCATION_MODE_ALL;

	k_sem_take(&connected_sem, K_FOREVER);
	k_sleep(K_SECONDS(CONFIG_APP_LOCATION_BOOT_DELAY_SECONDS));

	while (true) {
		if (atomic_get(&connected)) {
			do_fix(mode);
		}

		/* Triggered wake uses the requested mode; a periodic timeout is a full fix. */
		mode = (k_sem_take(&fix_trigger, SAMPLE_INTERVAL) == 0)
			       ? (enum location_mode)atomic_get(&requested_mode)
			       : LOCATION_MODE_ALL;
	}
}

K_THREAD_DEFINE(location_tid, CONFIG_APP_LOCATION_THREAD_STACK_SIZE, location_thread,
		NULL, NULL, NULL, K_LOWEST_APPLICATION_THREAD_PRIO, 0, 0);

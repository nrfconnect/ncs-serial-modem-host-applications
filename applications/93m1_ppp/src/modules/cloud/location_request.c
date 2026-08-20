/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <net/nrf_cloud_coap.h>
#include <net/nrf_cloud_location.h>
#include <net/wifi_location_common.h>
#include <modem/lte_lc.h>
#include <zephyr/net/wifi_mgmt.h>

#include "location_request.h"

static const struct nrf_cloud_location_config location_config = {
	.do_reply = true,
	.fallback = true,
};
static struct lte_lc_cells_info cells;
static struct wifi_scan_result coap_aps[CONFIG_APP_LOCATION_MAX_WIFI_APS];
static struct wifi_scan_info wifi;

int location_request_build(const struct location_msg *msg,
			    struct nrf_cloud_coap_location_request *req)
{
	cells = (struct lte_lc_cells_info){
		.current_cell = {
			.mcc = msg->cell.mcc,
			.mnc = msg->cell.mnc,
			.id = msg->cell.eci,
			.tac = msg->cell.tac,
			.earfcn = msg->cell.earfcn,
			.phys_cell_id = (uint16_t)msg->cell.pci,
			.timing_advance = LTE_LC_CELL_TIMING_ADVANCE_INVALID,
			/* Codec applies RSRP_IDX_TO_DBM(); convert dBm back to the index. */
			.rsrp = msg->cell.valid
					? (int16_t)CLAMP(msg->cell.rsrp + 141, 0, 97)
					: LTE_LC_CELL_RSRP_INVALID,
		},
	};

	wifi = (struct wifi_scan_info){ .ap_info = coap_aps, .cnt = 0 };

	for (uint8_t i = 0; i < msg->ap_count; i++) {
		memcpy(coap_aps[wifi.cnt].mac, msg->aps[i].mac, WIFI_MAC_ADDR_LEN);
		coap_aps[wifi.cnt].mac_length = WIFI_MAC_ADDR_LEN;
		coap_aps[wifi.cnt].channel = msg->aps[i].channel;
		coap_aps[wifi.cnt].rssi = msg->aps[i].rssi;
		wifi.cnt++;
	}

	req->config = &location_config;
	req->cell_info = msg->cell.valid ? &cells : NULL;
	req->wifi_info = (wifi.cnt >= NRF_CLOUD_LOCATION_WIFI_AP_CNT_MIN) ? &wifi : NULL;

	if (req->cell_info == NULL && req->wifi_info == NULL) {
		return -ENODATA;
	}

	return 0;
}

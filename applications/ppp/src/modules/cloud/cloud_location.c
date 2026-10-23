/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include <zephyr/net/coap.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/zbus/zbus.h>
#include <net/nrf_cloud_coap.h>
#include <net/nrf_cloud_location.h>
#include <net/wifi_location_common.h>
#include <modem/lte_lc.h>

#include "app_common.h"
#include "modules/cloud/cloud.h"
#include "modules/cloud/cloud_internal.h"
#include "modules/cloud/cloud_location.h"

LOG_MODULE_DECLARE(cloud, CONFIG_APP_CLOUD_LOG_LEVEL);

static const struct nrf_cloud_location_config location_config = {
	.do_reply = true,
	.fallback = true,
};
static struct lte_lc_cells_info cells;
static struct wifi_scan_result coap_aps[CONFIG_APP_LOCATION_WIFI_APS_MAX];
static struct wifi_scan_info wifi;

/* Inverse of RSRQ_IDX_TO_DB(), which the codec applies. */
static int16_t rsrq_db_to_idx(int rsrq)
{
	int idx = 2 * rsrq + 40;

	if (idx < 0) {
		idx--;
	} else if (idx >= 35) {
		idx++;
	}

	return (int16_t)idx;
}

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

static int location_request_build(const struct location_search_wifi_cell_result *data,
				  struct nrf_cloud_coap_location_request *req)
{
	const struct location_cell_info *cell = &data->cell;

	cells = (struct lte_lc_cells_info){
		.current_cell = {
			.mcc = cell->mcc,
			.mnc = cell->mnc,
			.id = cell->eci,
			.tac = cell->tac,
			.earfcn = cell->earfcn,
			.phys_cell_id = (uint16_t)cell->pci,
			.timing_advance = LTE_LC_CELL_TIMING_ADVANCE_INVALID,
			/* Codec applies RSRP_IDX_TO_DBM(); convert dBm back to the index. */
			.rsrp = cell->valid ? (int16_t)CLAMP(cell->rsrp + 141, 0, 97)
					    : LTE_LC_CELL_RSRP_INVALID,
			.rsrq = cell->valid ? rsrq_db_to_idx(cell->rsrq)
					    : LTE_LC_CELL_RSRQ_INVALID,
		},
	};

	wifi = (struct wifi_scan_info){ .ap_info = coap_aps, .cnt = 0 };

	for (size_t i = 0; i < MIN(data->wifi_cnt, ARRAY_SIZE(coap_aps)); i++) {
		memcpy(coap_aps[wifi.cnt].mac, data->wifi_aps[i].mac, MAC_ADDR_LEN);
		coap_aps[wifi.cnt].mac_length = data->wifi_aps[i].mac_length;
		coap_aps[wifi.cnt].channel = data->wifi_aps[i].channel;
		coap_aps[wifi.cnt].rssi = data->wifi_aps[i].rssi;
		wifi.cnt++;
	}

	/* The ground-fix schema rejects fewer access points than the minimum, so send
	 * the cell alone rather than failing the whole request.
	 */
	if ((wifi.cnt > 0) && (wifi.cnt < NRF_CLOUD_LOCATION_WIFI_AP_CNT_MIN)) {
		LOG_DBG("Dropping Wi-Fi, %u APs is below the minimum of %d",
			wifi.cnt, NRF_CLOUD_LOCATION_WIFI_AP_CNT_MIN);
	}

	req->config = &location_config;
	req->cell_info = cell->valid ? &cells : NULL;
	req->wifi_info = (wifi.cnt >= NRF_CLOUD_LOCATION_WIFI_AP_CNT_MIN) ? &wifi : NULL;

	if ((req->cell_info == NULL) && (req->wifi_info == NULL)) {
		return -ENODATA;
	}

	return 0;
}

static void send_request_failed(void)
{
	int err;
	const struct priv_cloud_msg msg = { .type = CLOUD_PRIV_REQUEST_FAILED };

	err = zbus_chan_pub(&priv_cloud_chan, &msg, PUB_TIMEOUT);
	if (err) {
		LOG_ERR("zbus_chan_pub priv_cloud_chan, error: %d", err);
		FATAL_ERROR();
	}
}

void cloud_location_request_handle(const struct location_search_wifi_cell_result *request)
{
	struct nrf_cloud_coap_location_request req;
	struct nrf_cloud_location_result result;
	int err;

	LOG_DBG("Ground-fix requested, cell: %s, Wi-Fi APs: %u",
		request->cell.valid ? "yes" : "no", request->wifi_cnt);

	err = location_request_build(request, &req);
	if (err) {
		LOG_ERR("No usable measurements for ground-fix, error: %d", err);
		FATAL_ERROR();
		return;
	}

	LOG_DBG("Sending ground-fix request to nRF Cloud (cell: %s, Wi-Fi: %s)",
		req.cell_info ? "yes" : "no", req.wifi_info ? "yes" : "no");

	err = nrf_cloud_coap_location_get(&req, &result);
	if ((err == COAP_RESPONSE_CODE_NOT_FOUND) || (err == COAP_RESPONSE_CODE_BAD_REQUEST)) {
		LOG_WRN("nRF Cloud CoAP location coordinates not found, error: %d", err);
		return;
	} else if (err) {
		LOG_ERR("nrf_cloud_coap_location_get, error: %d", err);
		send_request_failed();
		return;
	}

	LOG_DBG("Location: %.06f, %.06f Uncertainty: %um Type: %s",
		result.lat, result.lon, result.unc, fix_type_str(result.type));
	LOG_DBG("Google maps URL: https://maps.google.com/?q=%.06f,%.06f",
		result.lat, result.lon);
}

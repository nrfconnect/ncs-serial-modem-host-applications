/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/coap.h>
#include <zephyr/net/wifi.h>
#include <net/nrf_cloud_coap.h>
#include <net/nrf_cloud_location.h>
#include <net/wifi_location_common.h>

#include "app_common.h"
#include "cloud.h"
#include "cloud_location.h"

LOG_MODULE_DECLARE(cloud, CONFIG_APP_CLOUD_LOG_LEVEL);

static int wifi_ap_data_construct(struct wifi_scan_info *dest,
				  struct wifi_scan_result *ap_info,
				  size_t ap_info_count,
				  const struct location_cloud_request_data *src)
{
	if (!dest || !src || !ap_info) {
		LOG_ERR("Invalid NULL parameter(s) provided");
		return -EINVAL;
	}

	if (ap_info_count < src->wifi_cnt) {
		LOG_ERR("Insufficient ap_info_count: %zu, required: %d",
			ap_info_count, src->wifi_cnt);
		return -ENOMEM;
	}

	if (sizeof(ap_info[0].mac) < sizeof(src->wifi_aps[0].mac)) {
		LOG_ERR("Insufficient MAC array size in wifi_scan_result");
		return -EINVAL;
	}

	for (uint16_t i = 0; i < src->wifi_cnt; i++) {
		ap_info[i].rssi = src->wifi_aps[i].rssi;
		memcpy(ap_info[i].mac, src->wifi_aps[i].mac, MAC_ADDR_LEN);
		ap_info[i].mac_length = src->wifi_aps[i].mac_length;
	}

	dest->ap_info = ap_info;
	dest->cnt = src->wifi_cnt;

	return 0;
}

void cloud_location_request_handle(const struct location_cloud_request_data *request)
{
	static struct wifi_scan_result ap_info[CONFIG_APP_LOCATION_WIFI_APS_MAX];
	struct nrf_cloud_location_config loc_config = {
		.do_reply = true,
	};
	struct wifi_scan_info wifi_info = { 0 };
	struct nrf_cloud_coap_location_request loc_req = {
		.config = &loc_config,
	};
	struct nrf_cloud_location_result result = { 0 };
	int err;

	if (request->wifi_cnt == 0) {
		LOG_WRN("No Wi-Fi access points in location request, ignoring");
		return;
	}

	err = wifi_ap_data_construct(&wifi_info, ap_info, ARRAY_SIZE(ap_info), request);
	if (err) {
		LOG_ERR("wifi_ap_data_construct, error: %d", err);
		return;
	}

	loc_req.wifi_info = &wifi_info;

	LOG_DBG("Requesting location from nRF Cloud using %d Wi-Fi access points",
		request->wifi_cnt);

	err = nrf_cloud_coap_location_get(&loc_req, &result);
	if ((err == COAP_RESPONSE_CODE_NOT_FOUND) || (err == COAP_RESPONSE_CODE_BAD_REQUEST)) {
		LOG_WRN("nRF Cloud CoAP location coordinates not found, error: %d", err);
		return;
	} else if (err) {
		LOG_ERR("nrf_cloud_coap_location_get, error: %d", err);
		return;
	}

	LOG_INF("Location: %.06f, %.06f Uncertainty: %um",
		result.lat, result.lon, result.unc);
	LOG_INF("Google maps URL: https://maps.google.com/?q=%.06f,%.06f",
		result.lat, result.lon);
}

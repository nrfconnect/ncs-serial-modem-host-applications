/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef LOCATION_H_
#define LOCATION_H_

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/net/wifi.h>
#include <zephyr/zbus/zbus.h>

#ifdef __cplusplus
extern "C" {
#endif

enum location_mode {
	LOCATION_MODE_ALL,  /* cell + Wi-Fi */
	LOCATION_MODE_CELL, /* cell only    */
	LOCATION_MODE_WIFI, /* Wi-Fi only   */
};

enum location_msg_type {
	/* Input: scan using .mode. */
	LOCATION_FIX_REQUEST,

	/* Output: sync done, scan results in .cell, .aps and .ap_count. */
	LOCATION_SYNC_DONE,
};

struct location_cell {
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

struct location_wifi_ap {
	uint8_t mac[WIFI_MAC_ADDR_LEN];
	uint8_t channel;
	int8_t rssi;
};

struct location_msg {
	enum location_msg_type type;
	enum location_mode mode;
	struct location_cell cell;
	struct location_wifi_ap aps[CONFIG_APP_LOCATION_MAX_WIFI_APS];
	uint8_t ap_count;
};

ZBUS_CHAN_DECLARE(location_chan);

#ifdef __cplusplus
}
#endif

#endif /* LOCATION_H_ */

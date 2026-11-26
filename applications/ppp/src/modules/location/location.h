/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/* Not LOCATION_H_, which <modem/location.h> uses. */
#ifndef APP_LOCATION_H_
#define APP_LOCATION_H_

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Channel carrying @ref location_msg messages to and from the location module. */
ZBUS_CHAN_DECLARE(location_chan);

/** Length of a Wi-Fi access point MAC address in bytes. */
#define MAC_ADDR_LEN 6

/** @brief Message types published on and handled by @ref location_chan. */
enum location_msg_type {
	/* Output message types (published by the location module). */

	/** A location search has started. */
	LOCATION_SEARCH_STARTED = 0x1,

	/** The location search has finished, with or without a result, and the module
	 *  accepts new requests.
	 */
	LOCATION_SEARCH_DONE,

	/** Measurements in @ref location_msg::wifi_cell_result are ready to be resolved
	 *  into a position.
	 */
	LOCATION_SEARCH_WIFI_CELL_RESULT,

	/** Location module is ready to use. */
	LOCATION_MODULE_READY,

	/* Input message types (handled by the location module). */

	/** Request to start a location search. Any measurements found are published as
	 *  @ref LOCATION_SEARCH_WIFI_CELL_RESULT, followed by @ref LOCATION_SEARCH_DONE.
	 */
	LOCATION_SEARCH_TRIGGER,

	/** Request to cancel an ongoing location search. @ref LOCATION_SEARCH_DONE follows
	 *  if a search was active. The modem AT backend finishes a search before handling
	 *  the next message, so it ignores this request.
	 */
	LOCATION_SEARCH_CANCEL,
};

/** Serving cell information. */
struct location_cell_info {
	/** True if the remaining fields hold a measured serving cell. */
	bool valid;

	/** Mobile Country Code. */
	int mcc;

	/** Mobile Network Code. */
	int mnc;

	/** Reference Signal Received Power in dBm. */
	int rsrp;

	/** Reference Signal Received Quality in dB. */
	int rsrq;

	/** E-UTRA Absolute Radio Frequency Channel Number. */
	int earfcn;

	/** Physical cell ID. */
	int pci;

	/** E-UTRAN cell ID. */
	uint32_t eci;

	/** Tracking area code. */
	uint32_t tac;
};

/** Wi-Fi access point information. */
struct location_wifi_ap_info {
	/** Received Signal Strength Indicator (RSSI) in dBm. */
	int8_t rssi;

	/** MAC address of the Wi-Fi access point. */
	uint8_t mac[MAC_ADDR_LEN];

	/** Length of the MAC address. */
	uint8_t mac_length;

	/** Wi-Fi channel the access point was found on. */
	uint8_t channel;
};

/** Cell and Wi-Fi scan data from a location search. */
struct location_search_wifi_cell_result {
	/** Serving cell information. */
	struct location_cell_info cell;

	/** Number of Wi-Fi access points. */
	uint16_t wifi_cnt;

	/** Wi-Fi access point information. */
	struct location_wifi_ap_info wifi_aps[CONFIG_APP_LOCATION_WIFI_APS_MAX];
};

/** @brief Message carried on @ref location_chan. */
struct location_msg {
	/** Message type, determines which of the remaining fields are valid. */
	enum location_msg_type type;

	/** Cell and Wi-Fi scan data from a location search.
	 *  Valid for @ref LOCATION_SEARCH_WIFI_CELL_RESULT messages.
	 */
	struct location_search_wifi_cell_result wifi_cell_result;
};

#ifdef __cplusplus
}
#endif

#endif /* APP_LOCATION_H_ */

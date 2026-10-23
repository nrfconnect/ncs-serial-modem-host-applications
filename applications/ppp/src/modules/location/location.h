/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/* Note: the guard must not be LOCATION_H_, which is used by <modem/location.h>. */
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

	/** A location search operation has been initiated and is now active. */
	LOCATION_SEARCH_STARTED = 0x1,

	/** A location search operation has completed successfully or due to timeout/error.
	 *  This message indicates that the location module has returned to an inactive state
	 *  and is ready to accept new location requests.
	 */
	LOCATION_SEARCH_DONE,

	/** Cell and Wi-Fi scan data from a location search is available to be resolved
	 *  into a position, for example by a cloud location service. The data is found in
	 *  @ref location_msg::wifi_cell_result.
	 */
	LOCATION_SEARCH_WIFI_CELL_RESULT,

	/** Location module is ready to use. */
	LOCATION_MODULE_READY,

	/* Input message types (handled by the location module). */

	/** Request to initiate a location search operation. This scans the serving cell
	 *  and Wi-Fi access points and publishes the result as
	 *  @ref LOCATION_SEARCH_WIFI_CELL_RESULT.
	 */
	LOCATION_SEARCH_TRIGGER,

	/** Request to cancel an ongoing location search operation.
	 *
	 *  The scans run to completion before the next message is handled, so a search
	 *  has always finished by the time this request is processed, and it is ignored.
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

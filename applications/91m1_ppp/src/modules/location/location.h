/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/* Note: the guard must not be LOCATION_H_, which is used by <modem/location.h>. */
#ifndef APP_LOCATION_H_
#define APP_LOCATION_H_

#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>

#ifdef __cplusplus
extern "C" {
#endif

ZBUS_CHAN_DECLARE(location_chan);

#define MAC_ADDR_LEN 6

enum location_msg_type {
	/* Output message types */

	/* A location search operation has been initiated and is now active. */
	LOCATION_SEARCH_STARTED = 0x1,

	/* A location search operation has completed successfully or due to timeout/error.
	 * This message indicates that the location module has returned to an inactive state
	 * and is ready to accept new location requests.
	 */
	LOCATION_SEARCH_DONE,

	/* A cloud location request with Wi-Fi scanning data is available for external
	 * processing. The cloud request data is found in the .cloud_request field of the
	 * message.
	 */
	LOCATION_CLOUD_REQUEST,

	/* Location module is ready to use */
	LOCATION_MODULE_READY,

	/* Input message types */

	/* Request to initiate a location search operation. This starts a Wi-Fi scan and
	 * publishes the result as LOCATION_CLOUD_REQUEST.
	 */
	LOCATION_SEARCH_TRIGGER,

	/* Request to cancel an ongoing location search operation.
	 *
	 * WARNING: This operation has known limitations and may cause issues with Wi-Fi
	 * scanning operations. Specifically:
	 * - Wi-Fi scans cannot be truly cancelled at the driver level and may continue
	 *   running, potentially causing -EBUSY errors on subsequent location requests
	 * - Race conditions may occur between cancellation and scan completion
	 * - Scan results may be lost if cancellation occurs during result collection
	 *
	 * Use this operation only when absolutely necessary. Before cancelling:
	 * - Ensure sufficient delay between subsequent location requests to avoid conflicts
	 * - Consider implementing retry logic to handle potential -EBUSY errors
	 * - Be aware that Wi-Fi scan results may be incomplete or lost
	 */
	LOCATION_SEARCH_CANCEL,
};

/** Wi-Fi access point information. */
struct location_wifi_ap_info {
	/** Received Signal Strength Indicator (RSSI) in dBm. */
	int8_t rssi;

	/** MAC address of the Wi-Fi access point. */
	uint8_t mac[MAC_ADDR_LEN];

	/** Length of the MAC address. */
	uint8_t mac_length;
};

/** Cloud location request data containing Wi-Fi scanning information. */
struct location_cloud_request_data {
	/** Number of Wi-Fi access points. */
	uint16_t wifi_cnt;

	/** Wi-Fi access point information. */
	struct location_wifi_ap_info wifi_aps[CONFIG_APP_LOCATION_WIFI_APS_MAX];
};

/* Structure to pass location data through zbus */
struct location_msg {
	enum location_msg_type type;

	/** Contains cloud location request data with Wi-Fi information.
	 *  cloud_request is valid for LOCATION_CLOUD_REQUEST messages.
	 */
	struct location_cloud_request_data cloud_request;
};

#ifdef __cplusplus
}
#endif

#endif /* APP_LOCATION_H_ */

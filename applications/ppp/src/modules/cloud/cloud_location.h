/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef CLOUD_LOCATION_H_
#define CLOUD_LOCATION_H_

#include "modules/location/location.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Resolve a location request using the nRF Cloud location service.
 *
 *  Requires an established nRF Cloud CoAP connection. A failed request publishes
 *  CLOUD_PRIV_REQUEST_FAILED on the private cloud channel.
 *
 *  @param request Location measurements received on the location channel.
 */
void cloud_location_request_handle(const struct location_search_wifi_cell_result *request);

#ifdef __cplusplus
}
#endif

#endif /* CLOUD_LOCATION_H_ */

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

/** @brief Resolve a Wi-Fi based location request using the nRF Cloud location service.
 *
 *  Requires an established nRF Cloud CoAP connection.
 *
 *  @param request Wi-Fi access point data received on the location channel.
 */
void cloud_location_request_handle(const struct location_cloud_request_data *request);

#ifdef __cplusplus
}
#endif

#endif /* CLOUD_LOCATION_H_ */

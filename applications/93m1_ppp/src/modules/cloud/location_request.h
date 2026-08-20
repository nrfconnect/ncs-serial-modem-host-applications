/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef LOCATION_REQUEST_H_
#define LOCATION_REQUEST_H_

#include <net/nrf_cloud_coap.h>

#include "modules/location/location.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Build an nRF Cloud location request from scan measurements.
 *
 * @param msg Scan measurements to package.
 * @param req Filled in on success. Points at module-static storage, valid
 *            until the next call.
 *
 * @retval 0        Built, req has usable cell and/or Wi-Fi data.
 * @retval -ENODATA No usable measurements, req was not filled in.
 */
int location_request_build(const struct location_msg *msg,
			    struct nrf_cloud_coap_location_request *req);

#ifdef __cplusplus
}
#endif

#endif /* LOCATION_REQUEST_H_ */

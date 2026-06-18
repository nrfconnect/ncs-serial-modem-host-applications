/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef LOCATION_H_
#define LOCATION_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Request an nRF Cloud location fix (AT%NRFCLOUDLOCATION).
 *
 * Blocks until the modem accepts the request; the resulting position is logged
 * asynchronously when the modem reports it. Called by the sync state machine.
 */
void location_update(void);

#ifdef __cplusplus
}
#endif

#endif /* LOCATION_H_ */

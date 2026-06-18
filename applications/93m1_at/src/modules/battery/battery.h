/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef BATTERY_H_
#define BATTERY_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Sample the fuel gauge and report state of charge to nRF Cloud.
 *
 * Reads the nPM1300 fuel gauge and sends the state of charge as an
 * AT%NRFCLOUDMESSAGE (appId "BATTERY"). Called by the sync state machine.
 *
 * @retval 0 on success, negative errno otherwise.
 */
int battery_report(void);

#ifdef __cplusplus
}
#endif

#endif /* BATTERY_H_ */

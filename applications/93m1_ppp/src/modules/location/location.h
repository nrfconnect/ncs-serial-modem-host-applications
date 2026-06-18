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

enum location_mode {
	LOCATION_MODE_ALL,  /* cell + Wi-Fi */
	LOCATION_MODE_CELL, /* cell only    */
	LOCATION_MODE_WIFI, /* Wi-Fi only   */
};

/**
 * @brief Request an on-demand location fix with the given source(s).
 *
 * Wakes the location thread to scan and POST now, in addition to the periodic
 * schedule. No-op until the network is connected.
 */
void location_trigger(enum location_mode mode);

#ifdef __cplusplus
}
#endif

#endif /* LOCATION_H_ */

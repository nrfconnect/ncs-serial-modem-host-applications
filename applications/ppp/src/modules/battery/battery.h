/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef BATTERY_H_
#define BATTERY_H_

#include <zephyr/zbus/zbus.h>

#ifdef __cplusplus
extern "C" {
#endif

enum battery_msg_type {
	BATTERY_SAMPLE,
};

struct battery_msg {
	enum battery_msg_type type;
};

ZBUS_CHAN_DECLARE(battery_chan);

#ifdef __cplusplus
}
#endif

#endif /* BATTERY_H_ */

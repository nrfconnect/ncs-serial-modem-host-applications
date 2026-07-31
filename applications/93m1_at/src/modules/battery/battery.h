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

/**
 * @brief Get the state of charge from the most recent sample.
 *
 * @param percent Populated with the state of charge in percent on success.
 *
 * @retval 0        Valid value written to @p percent.
 * @retval -EINVAL  @p percent is NULL.
 * @retval -ENODATA No valid sample available.
 */
int battery_percent_get(int *percent);

#ifdef __cplusplus
}
#endif

#endif /* BATTERY_H_ */

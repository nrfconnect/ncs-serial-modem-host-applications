/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>

#include "modules/modem_at/modem_at.h"
#include "location.h"

LOG_MODULE_REGISTER(location, CONFIG_APP_LOCATION_LOG_LEVEL);

/* A fix line starts with a numeric latitude; anything else (e.g. a "DEBUG,..."
 * trace) is a status URC, not a result.
 */
static bool is_coordinate(const char *s)
{
	return s != NULL && (*s == '-' || *s == '+' || (*s >= '0' && *s <= '9'));
}

/* %NRFCLOUDLOCATION: <lat>,<lon>,<unc>,<fulfilled_method> (async result URC). */
static void on_location(char **argv, uint16_t argc, void *user_data)
{
	ARG_UNUSED(user_data);

	if (argc >= 5 && is_coordinate(argv[1])) {
		LOG_INF("Location: %s,%s Uncertainty: %sm Method: %s",
			argv[1], argv[2], argv[3], argv[4]);
	} else if (argc >= 2) {
		LOG_DBG("NRFCLOUDLOCATION status: %s", argv[1]);
	}
}

void location_update(void)
{
	char cmd[40];
	int err;

	(void)snprintf(cmd, sizeof(cmd), "AT%%NRFCLOUDLOCATION=%d,1",
		       CONFIG_APP_LOCATION_METHOD);

	err = modem_at_run(cmd, NULL, 0, CONFIG_APP_LOCATION_AT_TIMEOUT_SECONDS);
	if (err) {
		LOG_WRN("%s failed: %d", cmd, err);
	}
}

static int location_init(void)
{
	int err = modem_at_urc_subscribe("%NRFCLOUDLOCATION: ", on_location, NULL);

	if (err) {
		LOG_ERR("Failed to subscribe to %%NRFCLOUDLOCATION URC: %d", err);
	}

	return 0;
}

SYS_INIT(location_init, APPLICATION, 0);

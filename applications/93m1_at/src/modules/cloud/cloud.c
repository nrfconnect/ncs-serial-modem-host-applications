/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "modules/modem_at/modem_at.h"
#include "cloud.h"

LOG_MODULE_REGISTER(cloud, CONFIG_APP_CLOUD_LOG_LEVEL);

static int extract(const char *resp, const char *prefix, char *out, size_t out_size)
{
	const char *p = strstr(resp, prefix);

	if (p == NULL) {
		return -ENOENT;
	}

	p += strlen(prefix);

	size_t i = 0;

	while (p[i] != '\0' && p[i] != '\n' && p[i] != '\r' && i < out_size - 1) {
		out[i] = p[i];
		i++;
	}
	out[i] = '\0';

	return 0;
}

void cloud_provision(void)
{
	char resp[64];
	char uuid[40];

	if (modem_at_run("AT%DEVICEUUID", resp, sizeof(resp), 10) == 0 &&
	    extract(resp, "%DEVICEUUID: ", uuid, sizeof(uuid)) == 0) {
		LOG_INF("Device UUID: %s", uuid);
	} else {
		LOG_ERR("Failed to read device UUID");
	}

	(void)modem_at_run("AT%CLOUDACCESSKEY", NULL, 0, 10);
}

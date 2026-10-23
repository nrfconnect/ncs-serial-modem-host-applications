/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/zbus/zbus.h>

#include "app_common.h"
#include "modules/location/location.h"

static int location_msg_publish(const struct shell *sh, enum location_msg_type type)
{
	struct location_msg msg = { .type = type };
	int err;

	err = zbus_chan_pub(&location_chan, &msg, PUB_TIMEOUT);
	if (err) {
		shell_error(sh, "zbus_chan_pub location_chan, error: %d", err);
		return err;
	}

	return 0;
}

static int cmd_location_search(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	int err = location_msg_publish(sh, LOCATION_SEARCH_TRIGGER);

	if (!err) {
		shell_print(sh, "Location search requested");
	}

	return err;
}

static int cmd_location_cancel(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	int err = location_msg_publish(sh, LOCATION_SEARCH_CANCEL);

	if (!err) {
		shell_print(sh, "Location search cancel requested");
	}

	return err;
}

SHELL_STATIC_SUBCMD_SET_CREATE(location_cmds,
	SHELL_CMD(search, NULL, "Trigger a location search now.", cmd_location_search),
	SHELL_CMD(cancel, NULL, "Cancel an ongoing location search.", cmd_location_cancel),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(location, &location_cmds, "Location module control", NULL);

/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>

#include "location.h"

static int cmd_location_fix(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	location_trigger(LOCATION_MODE_ALL);
	shell_print(sh, "Location fix requested (cell + wifi)");

	return 0;
}

static int cmd_location_cell(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	location_trigger(LOCATION_MODE_CELL);
	shell_print(sh, "Location fix requested (cell only)");

	return 0;
}

static int cmd_location_wifi(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	location_trigger(LOCATION_MODE_WIFI);
	shell_print(sh, "Location fix requested (wifi only)");

	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(location_cmds,
	SHELL_CMD(fix, NULL, "Trigger a cell + Wi-Fi location fix now.", cmd_location_fix),
	SHELL_CMD(cell, NULL, "Trigger a cell-only location fix now.", cmd_location_cell),
	SHELL_CMD(wifi, NULL, "Trigger a Wi-Fi-only location fix now.", cmd_location_wifi),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(location, &location_cmds, "Location module control", NULL);

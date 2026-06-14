/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>

#include "modem_at.h"

/* Replacement for the in-tree CONFIG_MODEM_AT_SHELL: the application owns the
 * single AT pipe, so the stock shell is disabled and `modem at` is re-added
 * here on top of the shared modem_at handler.
 */
static int cmd_modem_at(const struct shell *sh, size_t argc, char **argv)
{
	static char resp[CONFIG_APP_MODEM_AT_RESPONSE_MAX_SIZE];
	int ret;

	if (argc != 2) {
		shell_error(sh, "usage: modem at \"<command>\"");
		return -EINVAL;
	}

	ret = modem_at_run(argv[1], resp, sizeof(resp), CONFIG_APP_MODEM_AT_TIMEOUT_SECONDS);

	if (resp[0] != '\0') {
		shell_print(sh, "%s", resp);
	}

	switch (ret) {
	case 0:
		shell_print(sh, "OK");
		break;
	case -EPERM:
		shell_error(sh, "modem is not ready");
		break;
	case -EBUSY:
		shell_error(sh, "AT pipe busy");
		break;
	default:
		shell_error(sh, "AT command failed (%d)", ret);
		break;
	}

	return ret;
}

SHELL_STATIC_SUBCMD_SET_CREATE(modem_sub_cmds,
	SHELL_CMD_ARG(at, NULL, "Send AT command: modem at \"<command>\"", cmd_modem_at, 2, 0),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(modem, &modem_sub_cmds, "Modem commands", NULL);

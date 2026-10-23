/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/shell/shell.h>
#include <zephyr/zbus/zbus.h>

#include "app_common.h"
#include "fota.h"

static int cmd_poll(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	int err;
	const struct fota_msg msg = {
		.type = FOTA_POLL_REQUEST,
	};

	err = zbus_chan_pub(&fota_chan, &msg, PUB_TIMEOUT);
	if (err) {
		(void)shell_print(sh, "zbus_chan_pub, error: %d", err);
		return 1;
	}

	return 0;
}

static int cmd_cancel(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	int err;
	const struct fota_msg msg = {
		.type = FOTA_DOWNLOAD_CANCEL,
	};

	err = zbus_chan_pub(&fota_chan, &msg, PUB_TIMEOUT);
	if (err) {
		(void)shell_print(sh, "zbus_chan_pub, error: %d", err);
		return 1;
	}

	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	sub_cmds,
	SHELL_CMD(poll,
		  NULL,
		  "Check Memfault for a pending firmware update",
		  cmd_poll),
	SHELL_CMD(cancel,
		  NULL,
		  "Cancel an ongoing FOTA download",
		  cmd_cancel),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(fota,
		   &sub_cmds,
		   "Serial Modem Host FOTA module commands",
		   NULL);

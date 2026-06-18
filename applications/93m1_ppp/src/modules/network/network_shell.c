/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/zbus/zbus.h>

#include "app_common.h"
#include "network.h"

static int network_publish(const struct shell *sh, enum network_msg_type type)
{
	struct network_msg msg = { .type = type };
	int err;

	err = zbus_chan_pub(&network_chan, &msg, PUB_TIMEOUT);
	if (err) {
		shell_error(sh, "Failed to publish network message, error: %d", err);
		return err;
	}

	return 0;
}

static int cmd_network_connect(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	int err = network_publish(sh, NETWORK_CONNECT);

	if (!err) {
		shell_print(sh, "Network connect requested");
	}

	return err;
}

static int cmd_network_disconnect(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	int err = network_publish(sh, NETWORK_DISCONNECT);

	if (!err) {
		shell_print(sh, "Network disconnect requested");
	}

	return err;
}

SHELL_STATIC_SUBCMD_SET_CREATE(network_cmds,
	SHELL_CMD(connect, NULL, "Connect to the network (power on modem, attach + dial).",
		  cmd_network_connect),
	SHELL_CMD(disconnect, NULL, "Disconnect from the network (power off modem).",
		  cmd_network_disconnect),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(network, &network_cmds, "Serial Modem Host network control", NULL);

/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>
#include <zephyr/modem/at/user_pipe.h>
#include <zephyr/modem/chat.h>

#include "modem_at.h"

LOG_MODULE_REGISTER(modem_at, CONFIG_APP_MODEM_AT_LOG_LEVEL);

/* The modem exposes a single host AT pipe (DLCI 3); this handler owns it and
 * serialises access between the `modem at` shell and application modules.
 * A 2nd pipe (DLCI 4) needs a modem-firmware change - see the CMUX notes.
 */

static struct modem_chat at_chat;
static uint8_t chat_receive_buf[CONFIG_APP_MODEM_AT_CHAT_RECEIVE_BUF_SIZE];
static uint8_t *chat_argv[2];

static struct modem_chat_script_chat script_chat[1];
static struct modem_chat_match script_matches[2];
static uint8_t request_buf[CONFIG_APP_MODEM_AT_COMMAND_MAX_SIZE];

/* Serialises modem_at_run() and guards the collection target below. */
static K_MUTEX_DEFINE(run_lock);
static char *collect_buf;
static size_t collect_size;
static size_t collect_len;

static void collect_line(struct modem_chat *chat, char **argv, uint16_t argc, void *user_data)
{
	ARG_UNUSED(chat);
	ARG_UNUSED(user_data);

	/* Partial match hands the whole line back in argv[1]. */
	if (argc != 2 || collect_buf == NULL || collect_size == 0) {
		return;
	}

	size_t len = strnlen(argv[1], collect_size);

	if (len + 2 > collect_size - collect_len) {
		return;
	}

	memcpy(&collect_buf[collect_len], argv[1], len);
	collect_len += len;
	collect_buf[collect_len++] = '\n';
	collect_buf[collect_len] = '\0';
}

MODEM_CHAT_MATCHES_DEFINE(abort_matches, MODEM_CHAT_MATCH("ERROR", "", NULL));

/* Mutable (not MODEM_CHAT_SCRIPT_DEFINE, which is const in flash) so the
 * per-command timeout can be set at runtime in modem_at_run().
 */
static struct modem_chat_script at_script = {
	.name = "at_script",
	.script_chats = script_chat,
	.script_chats_size = ARRAY_SIZE(script_chat),
	.abort_matches = abort_matches,
	.abort_matches_size = ARRAY_SIZE(abort_matches),
	.callback = NULL,
	.timeout = CONFIG_APP_MODEM_AT_TIMEOUT_SECONDS,
};

static void init_script_chat(void)
{
	/* [0] matches every line and collects it (intermediate responses). */
	modem_chat_match_init(&script_matches[0]);
	modem_chat_match_set_match(&script_matches[0], "");
	modem_chat_match_set_separators(&script_matches[0], "");
	modem_chat_match_set_callback(&script_matches[0], collect_line);
	modem_chat_match_set_partial(&script_matches[0], true);
	modem_chat_match_enable_wildcards(&script_matches[0], false);

	/* [1] the terminating "OK" (set per run). */
	modem_chat_match_init(&script_matches[1]);
	modem_chat_match_set_match(&script_matches[1], "OK");
	modem_chat_match_set_separators(&script_matches[1], "");
	modem_chat_match_set_partial(&script_matches[1], false);
	modem_chat_match_enable_wildcards(&script_matches[1], false);

	modem_chat_script_chat_init(&script_chat[0]);
	modem_chat_script_chat_set_response_matches(&script_chat[0], script_matches,
						    ARRAY_SIZE(script_matches));
}

int modem_at_run(const char *req, char *resp, size_t resp_size, uint32_t timeout_s)
{
	int ret;

	if (req == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&run_lock, K_FOREVER);

	ret = modem_at_user_pipe_claim();
	if (ret < 0) {
		k_mutex_unlock(&run_lock);
		return ret;
	}

	collect_buf = resp;
	collect_size = resp_size;
	collect_len = 0;
	if (resp != NULL && resp_size > 0) {
		resp[0] = '\0';
	}

	if (snprintf(request_buf, sizeof(request_buf), "%s", req) >= (int)sizeof(request_buf)) {
		ret = -EINVAL;
		goto release;
	}

	ret = modem_chat_script_chat_set_request(&script_chat[0], request_buf);
	if (ret < 0) {
		ret = -EINVAL;
		goto release;
	}

	modem_chat_script_set_timeout(&at_script, timeout_s);

	ret = modem_chat_run_script(&at_chat, &at_script);
	if (ret == 0 && at_chat.script_result != MODEM_CHAT_SCRIPT_RESULT_SUCCESS) {
		ret = -EIO;
	}

release:
	collect_buf = NULL;
	modem_at_user_pipe_release();
	k_mutex_unlock(&run_lock);
	return ret;
}

static int modem_at_init(void)
{
	const struct modem_chat_config chat_config = {
		.receive_buf = chat_receive_buf,
		.receive_buf_size = sizeof(chat_receive_buf),
		.delimiter = "\r",
		.delimiter_size = sizeof("\r") - 1,
		.filter = "\n",
		.filter_size = sizeof("\n") - 1,
		.argv = chat_argv,
		.argv_size = ARRAY_SIZE(chat_argv),
	};

	modem_chat_init(&at_chat, &chat_config);
	init_script_chat();
	modem_at_user_pipe_init(&at_chat);

	return 0;
}

SYS_INIT(modem_at_init, POST_KERNEL, 99);

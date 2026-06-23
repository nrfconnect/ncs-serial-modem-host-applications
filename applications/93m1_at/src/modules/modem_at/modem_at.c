/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <stdio.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>
#include <zephyr/modem/at/user_pipe.h>
#include <zephyr/modem/chat.h>

#include "modem_at.h"

LOG_MODULE_REGISTER(modem_at, CONFIG_APP_MODEM_AT_LOG_LEVEL);

struct urc_sub {
	const char *prefix;
	modem_at_urc_cb cb;
	void *user_data;
};

struct modem_at_ctx {
	struct modem_chat chat;
	uint8_t chat_receive_buf[CONFIG_APP_MODEM_AT_CHAT_RECEIVE_BUF_SIZE];
	uint8_t *chat_argv[8];
	struct modem_chat_script_chat script_chat[1];
	struct modem_chat_match script_matches[2];
	struct modem_chat_script script;
	uint8_t request_buf[CONFIG_APP_MODEM_AT_COMMAND_MAX_SIZE];
	struct k_mutex run_lock;
	char *collect_buf;
	size_t collect_size;
	size_t collect_len;
	struct urc_sub urc_subs[CONFIG_APP_MODEM_AT_URC_SUBSCRIBERS];
	struct k_mutex urc_lock;
};

static struct modem_at_ctx at_ctx;

static void on_urc(struct modem_chat *c, char **argv, uint16_t argc, void *user_data);
static void collect_line(struct modem_chat *c, char **argv, uint16_t argc, void *user_data);
static void on_error_line(struct modem_chat *c, char **argv, uint16_t argc, void *user_data);
static void init_script_chat(void);
static int modem_at_init(void);

/* URCs that arrive asynchronously on the user pipe are routed to subscribers. */
MODEM_CHAT_MATCHES_DEFINE(unsol_matches,
	MODEM_CHAT_MATCH("%NRFCLOUDLOCATION: ", ",", on_urc));

MODEM_CHAT_MATCHES_DEFINE(script_abort_matches,
	MODEM_CHAT_MATCH("ERROR", "", NULL),
	MODEM_CHAT_MATCH("+CME ERROR:", "", on_error_line),
	MODEM_CHAT_MATCH("+CMS ERROR:", "", on_error_line));

static void on_urc(struct modem_chat *c, char **argv, uint16_t argc, void *user_data)
{
	ARG_UNUSED(c);
	ARG_UNUSED(user_data);

	if (argc < 1 || argv[0] == NULL) {
		return;
	}

	k_mutex_lock(&at_ctx.urc_lock, K_FOREVER);
	for (int i = 0; i < ARRAY_SIZE(at_ctx.urc_subs); i++) {
		if (at_ctx.urc_subs[i].cb != NULL &&
		    strcmp(at_ctx.urc_subs[i].prefix, argv[0]) == 0) {
			at_ctx.urc_subs[i].cb(argv, argc, at_ctx.urc_subs[i].user_data);
			break;
		}
	}
	k_mutex_unlock(&at_ctx.urc_lock);
}

int modem_at_urc_subscribe(const char *prefix, modem_at_urc_cb cb, void *user_data)
{
	if (prefix == NULL || cb == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&at_ctx.urc_lock, K_FOREVER);
	for (int i = 0; i < ARRAY_SIZE(at_ctx.urc_subs); i++) {
		if (at_ctx.urc_subs[i].cb == NULL) {
			at_ctx.urc_subs[i].prefix = prefix;
			at_ctx.urc_subs[i].cb = cb;
			at_ctx.urc_subs[i].user_data = user_data;
			k_mutex_unlock(&at_ctx.urc_lock);
			return 0;
		}
	}
	k_mutex_unlock(&at_ctx.urc_lock);

	return -ENOMEM;
}

static void collect_line(struct modem_chat *c, char **argv, uint16_t argc, void *user_data)
{
	ARG_UNUSED(c);
	ARG_UNUSED(user_data);

	if (argc != 2 || at_ctx.collect_buf == NULL || at_ctx.collect_size == 0) {
		return;
	}

	size_t len = strnlen(argv[1], at_ctx.collect_size);

	if (len + 2 > at_ctx.collect_size - at_ctx.collect_len) {
		return;
	}

	memcpy(&at_ctx.collect_buf[at_ctx.collect_len], argv[1], len);
	at_ctx.collect_len += len;
	at_ctx.collect_buf[at_ctx.collect_len++] = '\n';
	at_ctx.collect_buf[at_ctx.collect_len] = '\0';
}

static void on_error_line(struct modem_chat *c, char **argv, uint16_t argc, void *user_data)
{
	ARG_UNUSED(c);
	ARG_UNUSED(user_data);

	if (at_ctx.collect_buf == NULL || at_ctx.collect_size == 0 || argc < 1) {
		return;
	}

	at_ctx.collect_len = snprintk(at_ctx.collect_buf, at_ctx.collect_size,
				      "%s%s", argv[0],
				      (argc > 1 && argv[1] != NULL) ? argv[1] : "");
}

static void init_script_chat(void)
{
	modem_chat_match_init(&at_ctx.script_matches[0]);
	modem_chat_match_set_match(&at_ctx.script_matches[0], "");
	modem_chat_match_set_separators(&at_ctx.script_matches[0], "");
	modem_chat_match_set_callback(&at_ctx.script_matches[0], collect_line);
	modem_chat_match_set_partial(&at_ctx.script_matches[0], true);
	modem_chat_match_enable_wildcards(&at_ctx.script_matches[0], false);

	modem_chat_match_init(&at_ctx.script_matches[1]);
	modem_chat_match_set_match(&at_ctx.script_matches[1], "OK");
	modem_chat_match_set_separators(&at_ctx.script_matches[1], "");
	modem_chat_match_set_partial(&at_ctx.script_matches[1], false);
	modem_chat_match_enable_wildcards(&at_ctx.script_matches[1], false);

	modem_chat_script_chat_init(&at_ctx.script_chat[0]);
	modem_chat_script_chat_set_response_matches(&at_ctx.script_chat[0],
						    at_ctx.script_matches,
						    ARRAY_SIZE(at_ctx.script_matches));

	at_ctx.script.name = "at_script";
	at_ctx.script.script_chats = at_ctx.script_chat;
	at_ctx.script.script_chats_size = ARRAY_SIZE(at_ctx.script_chat);
	at_ctx.script.abort_matches = script_abort_matches;
	at_ctx.script.abort_matches_size = ARRAY_SIZE(script_abort_matches);
	at_ctx.script.callback = NULL;
	at_ctx.script.timeout = CONFIG_APP_MODEM_AT_TIMEOUT_SECONDS;
}

int modem_at_run(const char *req, char *resp, size_t resp_size, uint32_t timeout_s)
{
	int ret;

	if (req == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&at_ctx.run_lock, K_FOREVER);

	ret = modem_at_user_pipe_claim();
	if (ret < 0) {
		k_mutex_unlock(&at_ctx.run_lock);
		return ret;
	}

	at_ctx.collect_buf = resp;
	at_ctx.collect_size = resp_size;
	at_ctx.collect_len = 0;
	if (resp != NULL && resp_size > 0) {
		resp[0] = '\0';
	}

	ret = snprintk((char *)at_ctx.request_buf, sizeof(at_ctx.request_buf), "%s", req);
	if (ret >= (int)sizeof(at_ctx.request_buf)) {
		ret = -EINVAL;
		goto release;
	}

	ret = modem_chat_script_chat_set_request(&at_ctx.script_chat[0], at_ctx.request_buf);
	if (ret < 0) {
		ret = -EINVAL;
		goto release;
	}

	modem_chat_script_set_timeout(&at_ctx.script, timeout_s);

	ret = modem_chat_run_script(&at_ctx.chat, &at_ctx.script);
	if (ret == 0 && at_ctx.chat.script_result != MODEM_CHAT_SCRIPT_RESULT_SUCCESS) {
		ret = -EIO;
	}

release:
	at_ctx.collect_buf = NULL;
	modem_at_user_pipe_release();
	k_mutex_unlock(&at_ctx.run_lock);
	return ret;
}

static int modem_at_init(void)
{
	const struct modem_chat_config chat_config = {
		.receive_buf = at_ctx.chat_receive_buf,
		.receive_buf_size = sizeof(at_ctx.chat_receive_buf),
		.delimiter = "\r",
		.delimiter_size = sizeof("\r") - 1,
		.filter = "\n",
		.filter_size = sizeof("\n") - 1,
		.argv = at_ctx.chat_argv,
		.argv_size = ARRAY_SIZE(at_ctx.chat_argv),
		.unsol_matches = unsol_matches,
		.unsol_matches_size = ARRAY_SIZE(unsol_matches),
	};

	k_mutex_init(&at_ctx.run_lock);
	k_mutex_init(&at_ctx.urc_lock);

	modem_chat_init(&at_ctx.chat, &chat_config);
	init_script_chat();

	modem_at_user_pipe_init(&at_ctx.chat);

	return 0;
}

SYS_INIT(modem_at_init, POST_KERNEL, 99);

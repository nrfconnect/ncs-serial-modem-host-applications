/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/modem/backend/uart.h>
#include <zephyr/modem/chat.h>
#include <zephyr/modem/pipe.h>

#include "app_common.h"
#include "modem_at.h"

LOG_MODULE_REGISTER(modem_at, CONFIG_APP_MODEM_AT_LOG_LEVEL);

#define MODEM_NODE DT_PATH(zephyr_user)

BUILD_ASSERT(DT_NODE_HAS_PROP(MODEM_NODE, modem_uart),
	     "zephyr,user must have a modem-uart phandle to the Serial Modem UART");

struct urc_sub {
	const char *prefix;
	modem_at_urc_cb cb;
	void *user_data;
};

/* One URC that a caller of modem_at_run_wait_urc() is waiting for. */
struct urc_waiter {
	const char *prefix;
	char *buf;
	size_t size;
	bool in_use;
	bool armed;
	struct k_sem sem;
};

struct modem_at_ctx {
	struct modem_backend_uart backend;
	struct modem_pipe *pipe;
	uint8_t uart_rx_buf[CONFIG_APP_MODEM_AT_UART_RX_BUF_SIZE] __aligned(4);
	uint8_t uart_tx_buf[CONFIG_APP_MODEM_AT_UART_TX_BUF_SIZE];
	struct modem_chat chat;
	uint8_t chat_receive_buf[CONFIG_APP_MODEM_AT_CHAT_RECEIVE_BUF_SIZE];
	uint8_t *chat_argv[16];
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
	struct urc_waiter waiters[CONFIG_APP_MODEM_AT_URC_WAITERS];
	struct k_spinlock waiter_lock;
};

static struct modem_at_ctx at_ctx;

static const struct gpio_dt_spec dtr_gpio = GPIO_DT_SPEC_GET_OR(MODEM_NODE, mdm_dtr_gpios, {0});

static void on_urc(struct modem_chat *c, char **argv, uint16_t argc, void *user_data);
static void collect_line(struct modem_chat *c, char **argv, uint16_t argc, void *user_data);
static void on_error_line(struct modem_chat *c, char **argv, uint16_t argc, void *user_data);
static void init_script_chat(void);
static int modem_at_init(void);

/*
 * URCs routed to waiters and subscribers. A line with one of these prefixes that
 * nobody handles is collected as a response line instead, for example the
 * #XNRFCLOUD read response.
 */
MODEM_CHAT_MATCHES_DEFINE(unsol_matches,
	MODEM_CHAT_MATCH("+CEREG: ", ",", on_urc),
	MODEM_CHAT_MATCH("#XNRFPROV: ", ",", on_urc),
	MODEM_CHAT_MATCH("#XNRFCLOUD: ", ",", on_urc),
	MODEM_CHAT_MATCH("#XNRFCLOUDPOS: ", ",", on_urc),
	MODEM_CHAT_MATCH("#XGNSS: ", ",", on_urc),
	MODEM_CHAT_MATCH("#XGNSSPOS: ", ",", on_urc));

MODEM_CHAT_MATCHES_DEFINE(script_abort_matches,
	MODEM_CHAT_MATCH("ERROR", "", NULL),
	MODEM_CHAT_MATCH("+CME ERROR:", "", on_error_line),
	MODEM_CHAT_MATCH("+CMS ERROR:", "", on_error_line));

/* Join argv[1..] with commas, which restores the line after the prefix. */
static void join_args(char *buf, size_t size, char **argv, uint16_t argc)
{
	size_t len = 0;

	buf[0] = '\0';

	for (uint16_t i = 1; i < argc && argv[i] != NULL; i++) {
		int ret = snprintk(&buf[len], size - len, "%s%s", (i > 1) ? "," : "", argv[i]);

		if (ret < 0 || (size_t)ret >= size - len) {
			return;
		}
		len += ret;
	}
}

static void collect_append(const char *prefix, const char *line)
{
	size_t len;

	if (at_ctx.collect_buf == NULL || at_ctx.collect_size == 0) {
		return;
	}

	len = strlen(prefix) + strnlen(line, at_ctx.collect_size);
	if (len + 2 > at_ctx.collect_size - at_ctx.collect_len) {
		return;
	}

	at_ctx.collect_len += snprintk(&at_ctx.collect_buf[at_ctx.collect_len],
				       at_ctx.collect_size - at_ctx.collect_len,
				       "%s%s\n", prefix, line);
}

static bool urc_to_waiter(char **argv, uint16_t argc)
{
	struct urc_waiter *waiter = NULL;
	k_spinlock_key_t key = k_spin_lock(&at_ctx.waiter_lock);

	for (int i = 0; i < ARRAY_SIZE(at_ctx.waiters); i++) {
		if (at_ctx.waiters[i].armed && strcmp(at_ctx.waiters[i].prefix, argv[0]) == 0) {
			waiter = &at_ctx.waiters[i];
			join_args(waiter->buf, waiter->size, argv, argc);
			waiter->armed = false;
			break;
		}
	}
	k_spin_unlock(&at_ctx.waiter_lock, key);

	if (waiter == NULL) {
		return false;
	}

	k_sem_give(&waiter->sem);

	return true;
}

static bool urc_to_subscribers(char **argv, uint16_t argc)
{
	bool handled = false;

	k_mutex_lock(&at_ctx.urc_lock, K_FOREVER);
	for (int i = 0; i < ARRAY_SIZE(at_ctx.urc_subs); i++) {
		if (at_ctx.urc_subs[i].cb != NULL &&
		    strcmp(at_ctx.urc_subs[i].prefix, argv[0]) == 0) {
			at_ctx.urc_subs[i].cb(argv, argc, at_ctx.urc_subs[i].user_data);
			handled = true;
		}
	}
	k_mutex_unlock(&at_ctx.urc_lock);

	return handled;
}

static void on_urc(struct modem_chat *c, char **argv, uint16_t argc, void *user_data)
{
	ARG_UNUSED(c);
	ARG_UNUSED(user_data);

	/* Only the modem_chat receive context calls this, so one buffer is enough. */
	static char line[CONFIG_APP_MODEM_AT_URC_MAX_SIZE];

	if (argc < 1 || argv[0] == NULL) {
		return;
	}

	if (urc_to_waiter(argv, argc) || urc_to_subscribers(argv, argc)) {
		return;
	}

	join_args(line, sizeof(line), argv, argc);
	if (at_ctx.collect_buf != NULL) {
		collect_append(argv[0], line);
	} else {
		LOG_DBG("Unhandled URC: %s%s", argv[0], line);
	}
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

	if (argc != 2) {
		return;
	}

	collect_append("", argv[1]);
}

static void on_error_line(struct modem_chat *c, char **argv, uint16_t argc, void *user_data)
{
	ARG_UNUSED(c);
	ARG_UNUSED(user_data);

	if (at_ctx.collect_buf == NULL || at_ctx.collect_size == 0 || argc < 1) {
		return;
	}

	int ret = snprintk(at_ctx.collect_buf, at_ctx.collect_size,
			   "%s%s", argv[0],
			   (argc > 1 && argv[1] != NULL) ? argv[1] : "");

	if (ret < 0) {
		at_ctx.collect_len = 0;
		at_ctx.collect_buf[0] = '\0';
	} else if ((size_t)ret >= at_ctx.collect_size) {
		at_ctx.collect_len = at_ctx.collect_size - 1;
	} else {
		at_ctx.collect_len = (size_t)ret;
	}
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

	ret = k_mutex_lock(&at_ctx.run_lock, K_SECONDS(CONFIG_APP_MODEM_AT_WAIT_TIMEOUT_SECONDS));
	if (ret) {
		LOG_WRN("Timed out waiting for the AT channel after %us",
			CONFIG_APP_MODEM_AT_WAIT_TIMEOUT_SECONDS);
		return ret;
	}

	at_ctx.collect_buf = resp;
	at_ctx.collect_size = resp_size;
	at_ctx.collect_len = 0;
	if (resp != NULL && resp_size > 0) {
		resp[0] = '\0';
	}

	ret = snprintk((char *)at_ctx.request_buf, sizeof(at_ctx.request_buf), "%s", req);
	if (ret < 0 || ret >= (int)sizeof(at_ctx.request_buf)) {
		ret = -EINVAL;
		goto release;
	}

	ret = modem_chat_script_chat_set_request(&at_ctx.script_chat[0], at_ctx.request_buf);
	if (ret < 0) {
		ret = -EINVAL;
		goto release;
	}

	modem_chat_script_set_timeout(&at_ctx.script, timeout_s);

	LOG_DBG("AT> %s (timeout %us)", req, timeout_s);

	ret = modem_chat_run_script(&at_ctx.chat, &at_ctx.script);
	if (ret == 0 && at_ctx.chat.script_result != MODEM_CHAT_SCRIPT_RESULT_SUCCESS) {
		ret = -EIO;
	}

	if (ret) {
		LOG_DBG("AT command failed, error: %d, script result: %d", ret,
			at_ctx.chat.script_result);
	} else if (resp != NULL && resp_size > 0) {
		LOG_DBG("AT< %s", resp);
	} else {
		LOG_DBG("AT command OK");
	}

release:
	at_ctx.collect_buf = NULL;
	k_mutex_unlock(&at_ctx.run_lock);
	return ret;
}

static struct urc_waiter *waiter_arm(const char *prefix, char *buf, size_t size)
{
	struct urc_waiter *waiter = NULL;
	k_spinlock_key_t key = k_spin_lock(&at_ctx.waiter_lock);

	for (int i = 0; i < ARRAY_SIZE(at_ctx.waiters); i++) {
		if (!at_ctx.waiters[i].in_use) {
			waiter = &at_ctx.waiters[i];
			waiter->prefix = prefix;
			waiter->buf = buf;
			waiter->size = size;
			waiter->in_use = true;
			waiter->armed = true;
			k_sem_reset(&waiter->sem);
			break;
		}
	}
	k_spin_unlock(&at_ctx.waiter_lock, key);

	return waiter;
}

static void waiter_free(struct urc_waiter *waiter)
{
	k_spinlock_key_t key = k_spin_lock(&at_ctx.waiter_lock);

	waiter->armed = false;
	waiter->in_use = false;
	k_spin_unlock(&at_ctx.waiter_lock, key);
}

int modem_at_run_wait_urc(const char *req, const char *urc_prefix, char *urc, size_t urc_size,
			  uint32_t timeout_s, uint32_t urc_timeout_s)
{
	struct urc_waiter *waiter;
	int ret;

	if (urc_prefix == NULL || urc == NULL || urc_size == 0) {
		return -EINVAL;
	}

	urc[0] = '\0';

	waiter = waiter_arm(urc_prefix, urc, urc_size);
	if (waiter == NULL) {
		return -ENOMEM;
	}

	ret = modem_at_run(req, NULL, 0, timeout_s);
	if (ret == 0) {
		if (k_sem_take(&waiter->sem, K_SECONDS(urc_timeout_s)) == 0) {
			LOG_DBG("URC< %s%s", urc_prefix, urc);
		} else {
			LOG_DBG("No %s URC within %us", urc_prefix, urc_timeout_s);
			ret = -ETIMEDOUT;
		}
	}

	waiter_free(waiter);

	return ret;
}

static int modem_at_init(void)
{
	int err;
	const struct modem_backend_uart_config uart_config = {
		.uart = DEVICE_DT_GET(DT_PHANDLE(MODEM_NODE, modem_uart)),
		.dtr_gpio = (dtr_gpio.port != NULL) ? &dtr_gpio : NULL,
		.receive_buf = at_ctx.uart_rx_buf,
		.receive_buf_size = sizeof(at_ctx.uart_rx_buf),
		.transmit_buf = at_ctx.uart_tx_buf,
		.transmit_buf_size = sizeof(at_ctx.uart_tx_buf),
	};
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
	for (int i = 0; i < ARRAY_SIZE(at_ctx.waiters); i++) {
		k_sem_init(&at_ctx.waiters[i].sem, 0, 1);
	}

	if (!device_is_ready(uart_config.uart)) {
		LOG_ERR("Serial Modem UART not ready");
		FATAL_ERROR();
		return -ENODEV;
	}

	/* The UART backend asserts DTR when the pipe opens. Serial Modem keeps its
	 * UART enabled while DTR is asserted.
	 */
	if (uart_config.dtr_gpio != NULL) {
		err = gpio_pin_configure_dt(&dtr_gpio, GPIO_OUTPUT_INACTIVE);
		if (err) {
			LOG_ERR("gpio_pin_configure_dt DTR, error: %d", err);
			FATAL_ERROR();
			return err;
		}
	}

	at_ctx.pipe = modem_backend_uart_init(&at_ctx.backend, &uart_config);
	if (at_ctx.pipe == NULL) {
		LOG_ERR("modem_backend_uart_init failed");
		FATAL_ERROR();
		return -EIO;
	}

	err = modem_chat_init(&at_ctx.chat, &chat_config);
	if (err) {
		LOG_ERR("modem_chat_init, error: %d", err);
		FATAL_ERROR();
		return err;
	}

	init_script_chat();

	err = modem_pipe_open(at_ctx.pipe, K_SECONDS(CONFIG_APP_MODEM_AT_TIMEOUT_SECONDS));
	if (err) {
		LOG_ERR("modem_pipe_open, error: %d", err);
		FATAL_ERROR();
		return err;
	}

	err = modem_chat_attach(&at_ctx.chat, at_ctx.pipe);
	if (err) {
		LOG_ERR("modem_chat_attach, error: %d", err);
		FATAL_ERROR();
		return err;
	}

	return 0;
}

SYS_INIT(modem_at_init, APPLICATION, 0);

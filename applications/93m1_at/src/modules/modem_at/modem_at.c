/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <stdio.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/pm/device.h>
#include <zephyr/modem/backend/uart.h>
#include <zephyr/modem/chat.h>
#include <zephyr/modem/pipe.h>

#include "modem_at.h"

LOG_MODULE_REGISTER(modem_at, CONFIG_APP_MODEM_AT_LOG_LEVEL);

#define MODEM_UART DEVICE_DT_GET(DT_CHOSEN(zephyr_modem_uart))
#define MODEM_NODE DT_ALIAS(modem)

/* Timing mirrors the nrf93m1 vendor config. */
#define MODEM_POWER_PULSE_MS 100

static struct modem_backend_uart uart_backend;
static struct modem_pipe *uart_pipe;
static struct modem_chat chat;

static uint8_t uart_rx_buf[CONFIG_APP_MODEM_AT_UART_RECEIVE_BUF_SIZE] __aligned(4);
static uint8_t uart_tx_buf[CONFIG_APP_MODEM_AT_UART_TRANSMIT_BUF_SIZE];
static uint8_t chat_receive_buf[CONFIG_APP_MODEM_AT_CHAT_RECEIVE_BUF_SIZE];
static uint8_t *chat_argv[8];

static atomic_t transport_ready;
static atomic_t ready;

static struct modem_chat_script_chat script_chat[1];
static struct modem_chat_match script_matches[2];
static uint8_t request_buf[CONFIG_APP_MODEM_AT_COMMAND_MAX_SIZE];

static K_MUTEX_DEFINE(run_lock);
static char *collect_buf;
static size_t collect_size;
static size_t collect_len;

static const struct gpio_dt_spec power_gpio =
	GPIO_DT_SPEC_GET_OR(MODEM_NODE, mdm_power_gpios, {0});
static const struct gpio_dt_spec reset_gpio =
	GPIO_DT_SPEC_GET_OR(MODEM_NODE, mdm_reset_gpios, {0});

#if DT_NODE_EXISTS(DT_NODELABEL(modem_dcdc))
static const struct device *const modem_dcdc = DEVICE_DT_GET(DT_NODELABEL(modem_dcdc));
#else
static const struct device *const modem_dcdc;
#endif

struct urc_sub {
	const char *prefix;
	modem_at_urc_cb cb;
	void *user_data;
};

static struct urc_sub urc_subs[CONFIG_APP_MODEM_AT_URC_SUBSCRIBERS];
static K_MUTEX_DEFINE(urc_lock);

/* Shared callback for every registered unsol match. argv[0] is the matched
 * prefix; route to the subscriber that registered it.
 */
static void on_urc(struct modem_chat *c, char **argv, uint16_t argc, void *user_data)
{
	ARG_UNUSED(c);
	ARG_UNUSED(user_data);

	if (argc < 1 || argv[0] == NULL) {
		return;
	}

	k_mutex_lock(&urc_lock, K_FOREVER);
	for (int i = 0; i < ARRAY_SIZE(urc_subs); i++) {
		if (urc_subs[i].cb != NULL && strcmp(urc_subs[i].prefix, argv[0]) == 0) {
			urc_subs[i].cb(argv, argc, urc_subs[i].user_data);
			break;
		}
	}
	k_mutex_unlock(&urc_lock);
}

/* Registered unsolicited result codes. A subscriber may only attach to one of
 * these prefixes (extend the list to add more URCs).
 */
MODEM_CHAT_MATCHES_DEFINE(unsol_matches,
	MODEM_CHAT_MATCH("+CEREG: ", ",", on_urc),
	MODEM_CHAT_MATCH("%NRFCLOUDLOCATION: ", ",", on_urc));

int modem_at_urc_subscribe(const char *prefix, modem_at_urc_cb cb, void *user_data)
{
	if (prefix == NULL || cb == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&urc_lock, K_FOREVER);
	for (int i = 0; i < ARRAY_SIZE(urc_subs); i++) {
		if (urc_subs[i].cb == NULL) {
			urc_subs[i].prefix = prefix;
			urc_subs[i].cb = cb;
			urc_subs[i].user_data = user_data;
			k_mutex_unlock(&urc_lock);
			return 0;
		}
	}
	k_mutex_unlock(&urc_lock);

	return -ENOMEM;
}

static void collect_line(struct modem_chat *c, char **argv, uint16_t argc, void *user_data)
{
	ARG_UNUSED(c);
	ARG_UNUSED(user_data);

	/* Catch-all response match hands the whole line back in argv[1]. */
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

static void on_error_line(struct modem_chat *c, char **argv, uint16_t argc, void *user_data)
{
	ARG_UNUSED(c);
	ARG_UNUSED(user_data);

	if (collect_buf == NULL || collect_size == 0 || argc < 1) {
		return;
	}

	collect_len = snprintf(collect_buf, collect_size, "%s%s",
			       argv[0], (argc > 1 && argv[1] != NULL) ? argv[1] : "");
}

MODEM_CHAT_MATCHES_DEFINE(script_abort_matches,
	MODEM_CHAT_MATCH("ERROR", "", NULL),
	MODEM_CHAT_MATCH("+CME ERROR:", "", on_error_line),
	MODEM_CHAT_MATCH("+CMS ERROR:", "", on_error_line));

static struct modem_chat_script at_script = {
	.name = "at_script",
	.script_chats = script_chat,
	.script_chats_size = ARRAY_SIZE(script_chat),
	.abort_matches = script_abort_matches,
	.abort_matches_size = ARRAY_SIZE(script_abort_matches),
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

	/* [1] the terminating "OK". */
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

	if (!atomic_get(&transport_ready)) {
		return -EPERM;
	}

	k_mutex_lock(&run_lock, K_FOREVER);

	collect_buf = resp;
	collect_size = resp_size;
	collect_len = 0;
	if (resp != NULL && resp_size > 0) {
		resp[0] = '\0';
	}

	if (strlen(req) >= sizeof(request_buf)) {
		ret = -EINVAL;
		goto release;
	}
	strcpy(request_buf, req);

	ret = modem_chat_script_chat_set_request(&script_chat[0], request_buf);
	if (ret < 0) {
		ret = -EINVAL;
		goto release;
	}

	modem_chat_script_set_timeout(&at_script, timeout_s);

	ret = modem_chat_run_script(&chat, &at_script);
	if (ret == 0 && chat.script_result != MODEM_CHAT_SCRIPT_RESULT_SUCCESS) {
		ret = -EIO;
	}

release:
	collect_buf = NULL;
	k_mutex_unlock(&run_lock);
	return ret;
}

static void modem_power_on(void)
{
	if (reset_gpio.port != NULL && gpio_is_ready_dt(&reset_gpio)) {
		(void)gpio_pin_configure_dt(&reset_gpio, GPIO_OUTPUT_ACTIVE);
	}

	if (modem_dcdc != NULL && device_is_ready(modem_dcdc)) {
		(void)pm_device_action_run(modem_dcdc, PM_DEVICE_ACTION_SUSPEND);
		k_msleep(CONFIG_APP_MODEM_AT_POWER_OFF_MS);
		(void)pm_device_action_run(modem_dcdc, PM_DEVICE_ACTION_RESUME);
	} else {
		LOG_WRN("No modem_dcdc power domain; cannot power-cycle the modem");
	}

	if (reset_gpio.port != NULL && gpio_is_ready_dt(&reset_gpio)) {
		(void)gpio_pin_configure_dt(&reset_gpio, GPIO_OUTPUT_INACTIVE);
	}
	k_msleep(CONFIG_APP_MODEM_AT_POWER_ON_DELAY_MS);

	if (power_gpio.port == NULL || !gpio_is_ready_dt(&power_gpio)) {
		LOG_WRN("No modem power-key GPIO; assuming modem powers on with the rail");
		return;
	}

	(void)gpio_pin_configure_dt(&power_gpio, GPIO_OUTPUT_ACTIVE);
	k_msleep(MODEM_POWER_PULSE_MS);
	gpio_pin_set_dt(&power_gpio, 0);

	LOG_INF("Modem power-cycled and power key pulsed; waiting for boot");
}

static int transport_open(void)
{
	const struct modem_backend_uart_config uart_config = {
		.uart = MODEM_UART,
		.receive_buf = uart_rx_buf,
		.receive_buf_size = sizeof(uart_rx_buf),
		.transmit_buf = uart_tx_buf,
		.transmit_buf_size = sizeof(uart_tx_buf),
	};
	const struct modem_chat_config chat_config = {
		.receive_buf = chat_receive_buf,
		.receive_buf_size = sizeof(chat_receive_buf),
		.delimiter = "\r",
		.delimiter_size = sizeof("\r") - 1,
		.filter = "\n",
		.filter_size = sizeof("\n") - 1,
		.argv = chat_argv,
		.argv_size = ARRAY_SIZE(chat_argv),
		.unsol_matches = unsol_matches,
		.unsol_matches_size = ARRAY_SIZE(unsol_matches),
	};
	int ret;

	if (!device_is_ready(MODEM_UART)) {
		LOG_ERR("Modem UART not ready");
		return -ENODEV;
	}

	uart_pipe = modem_backend_uart_init(&uart_backend, &uart_config);
	if (uart_pipe == NULL) {
		LOG_ERR("Failed to init UART backend");
		return -ENODEV;
	}

	ret = modem_chat_init(&chat, &chat_config);
	if (ret < 0) {
		LOG_ERR("modem_chat_init, error: %d", ret);
		return ret;
	}

	init_script_chat();

	ret = modem_chat_attach(&chat, uart_pipe);
	if (ret < 0) {
		LOG_ERR("modem_chat_attach, error: %d", ret);
		return ret;
	}

	ret = modem_pipe_open(uart_pipe, K_MSEC(1000));
	if (ret < 0) {
		LOG_ERR("modem_pipe_open, error: %d", ret);
		return ret;
	}

	return 0;
}

static int wait_for_modem(void)
{
	for (int i = 0; i < CONFIG_APP_MODEM_AT_BOOT_RETRIES; i++) {
		if (modem_at_run("AT", NULL, 0, 2) == 0) {
			return 0;
		}
		k_sleep(K_MSEC(500));
	}

	return -ETIMEDOUT;
}

static int run_startup_commands(void)
{
	static const char *const cmds[] = {
		"ATE0",
		"AT+CMEE=1",
		"AT+IFC=2,2",
	};

	for (int i = 0; i < ARRAY_SIZE(cmds); i++) {
		int ret = modem_at_run(cmds[i], NULL, 0, 10);

		if (ret < 0) {
			LOG_ERR("Startup command '%s' failed: %d", cmds[i], ret);
			return ret;
		}
	}

	return 0;
}

int modem_at_setup(void)
{
	int ret;

	modem_power_on();

	ret = transport_open();
	if (ret < 0) {
		LOG_ERR("AT transport open failed: %d", ret);
		return ret;
	}

	/* The pipe is up; allow modem_at_run() so the ping/startup can issue. */
	atomic_set(&transport_ready, 1);

	if (wait_for_modem() < 0) {
		LOG_WRN("Modem did not answer AT; transport up, will retry on demand");
	}

	if (run_startup_commands() < 0) {
		/* Keep the transport ready anyway; the shell can retry by hand. */
		LOG_WRN("Startup configuration incomplete");
	}

	atomic_set(&ready, 1);
	LOG_INF("AT transport ready");
	return 0;
}

bool modem_at_is_ready(void)
{
	return atomic_get(&ready) != 0;
}

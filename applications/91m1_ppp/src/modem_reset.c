/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>

#include "app_common.h"

#define MODEM_NODE DT_ALIAS(modem)

#if DT_NODE_HAS_PROP(MODEM_NODE, mdm_reset_gpios)

#define MODEM_RESET_PULSE_MS 500
#define MODEM_STARTUP_TIME_MS 2000

static int modem_reset_on_host_boot(void)
{
	const struct gpio_dt_spec reset = GPIO_DT_SPEC_GET(MODEM_NODE, mdm_reset_gpios);
	int err;

	if (!gpio_is_ready_dt(&reset)) {
		FATAL_ERROR();
		return -ENODEV;
	}

	err = gpio_pin_configure_dt(&reset, GPIO_OUTPUT_INACTIVE);
	if (err != 0) {
		FATAL_ERROR();
		return err;
	}

	gpio_pin_set_dt(&reset, 1);
	k_msleep(MODEM_RESET_PULSE_MS);
	gpio_pin_set_dt(&reset, 0);
	k_msleep(MODEM_STARTUP_TIME_MS);

	return 0;
}

SYS_INIT(modem_reset_on_host_boot, POST_KERNEL, 50);

#endif

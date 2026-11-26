/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef APP_LED_H_
#define APP_LED_H_

#include <zephyr/zbus/zbus.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Message types on @ref led_chan. */
enum led_msg_type {
	/** Input: set the RGB LED color and blink pattern, replacing the running one. */
	LED_RGB_SET = 1,
};

/** @brief Message published on @ref led_chan. */
struct led_msg {
	enum led_msg_type type;

	/** RGB values (0 to 255). The LEDs are GPIO driven, so any non-zero value is on. */
	uint8_t red;
	uint8_t green;
	uint8_t blue;

	/** On and off time of each blink cycle. A zero off time keeps the LED on. */
	uint32_t duration_on_msec;
	uint32_t duration_off_msec;

	/** Number of on/off cycles (-1 indicates forever, 0 turns the LED off) */
	int repetitions;
};

/** @brief LED module channel. Carries @ref LED_RGB_SET to the module. */
ZBUS_CHAN_DECLARE(led_chan);

#ifdef __cplusplus
}
#endif

#endif /* APP_LED_H_ */

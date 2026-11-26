/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef CLOUD_H_
#define CLOUD_H_

#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Cloud module channel.
 *
 * Carries @ref CLOUD_BATTERY_SAMPLE and @ref CLOUD_OBSERVABILITY_UPLOAD to the module, and
 * @ref CLOUD_BATTERY_DONE and @ref CLOUD_OBSERVABILITY_DONE back to its observers.
 */
ZBUS_CHAN_DECLARE(cloud_chan);

/** @brief Message types on @ref cloud_chan. */
enum cloud_msg_type {
	/**
	 * Input: report the battery state of charge in @ref cloud_msg.battery_percent to nRF Cloud
	 * as a device message with `AT%NRFCLOUDMESSAGE`.
	 */
	CLOUD_BATTERY_SAMPLE = 0x1,
	/**
	 * Output: the module has finished handling a @ref CLOUD_BATTERY_SAMPLE, whether the
	 * report was sent or not. The outcome is only logged.
	 */
	CLOUD_BATTERY_DONE,
	/**
	 * Input: upload the modem observability data to nRF Cloud with `AT%NRFCLOUDOBSUPLOAD`,
	 * after triggering a heartbeat with `AT%NRFCLOUDOBSHEARTBEAT` when
	 * CONFIG_APP_CLOUD_OBSERVABILITY_HEARTBEAT is set.
	 *
	 * nRF Cloud lists the device as seen, with its hardware and software versions, only
	 * after an upload.
	 */
	CLOUD_OBSERVABILITY_UPLOAD,
	/**
	 * Output: the module has finished handling a @ref CLOUD_OBSERVABILITY_UPLOAD, whether
	 * the upload succeeded or not. The outcome is only logged.
	 */
	CLOUD_OBSERVABILITY_DONE,
};

/** @brief Message published on @ref cloud_chan. */
struct cloud_msg {
	enum cloud_msg_type type;
	/** State of charge in percent. Only set for @ref CLOUD_BATTERY_SAMPLE. */
	int battery_percent;
};

#ifdef __cplusplus
}
#endif

#endif /* CLOUD_H_ */

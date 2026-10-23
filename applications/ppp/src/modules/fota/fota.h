/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef _FOTA_H_
#define _FOTA_H_

#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Channel carrying @ref fota_msg messages to and from the FOTA module. */
ZBUS_CHAN_DECLARE(fota_chan);

/** @brief Message types published on and handled by @ref fota_chan. */
enum fota_msg_type {
	/* Output message types (published by the FOTA module). */

	/** FOTA module is ready to use. */
	FOTA_MODULE_READY = 0x1,

	/** A FOTA download has started. */
	FOTA_STARTING,

	/** The FOTA module requires the application to reboot the device to
	 *  continue or finalize the update.
	 */
	FOTA_REBOOT_REQUEST,

	/** The FOTA sequence was aborted (download failed, timed out,
	 *  canceled, rejected, or no update was available).
	 */
	FOTA_ABORTED,

	/* Input message types (handled by the FOTA module). */

	/** Request to poll cloud for any available firmware updates. */
	FOTA_POLL_REQUEST,

	/** Request to cancel the ongoing FOTA download. */
	FOTA_DOWNLOAD_CANCEL,
};

/** @brief Message carried on @ref fota_chan. */
struct fota_msg {
	/** Message type. */
	enum fota_msg_type type;
};

#ifdef __cplusplus
}
#endif

#endif /* _FOTA_H_ */

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

/* Channels provided by this module */
ZBUS_CHAN_DECLARE(fota_chan);

enum fota_msg_type {
	/* Output message types */

	/* FOTA module is ready to use. */
	FOTA_MODULE_READY = 0x1,

	/* A FOTA download has started. */
	FOTA_STARTING,

	/* The FOTA module requires the application to reboot the device to
	 * continue or finalize the update.
	 */
	FOTA_REQUEST_REBOOT,

	/* The FOTA sequence was aborted (download failed, timed out,
	 * canceled, rejected, or no update was available).
	 */
	FOTA_ABORTED,

	/* Input message types */

	/* Request to poll cloud for any available firmware updates. */
	FOTA_POLL_REQUEST,

	/* Cancel the FOTA download. */
	FOTA_DOWNLOAD_CANCEL,
};

struct fota_msg {
	enum fota_msg_type type;
};

#ifdef __cplusplus
}
#endif

#endif /* _FOTA_H_ */

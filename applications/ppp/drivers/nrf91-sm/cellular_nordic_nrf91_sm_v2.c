/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Local copy of the upstream Zephyr driver
 * drivers/modem/vendor_modem_cellular/cellular_nordic_nrf91_sm_v2.c, added in
 * zephyrproject-rtos/zephyr#115058. Remove this copy once the nRF Connect SDK Zephyr includes it.
 * Remaining differences are marked "Upstream:" and cover modem_cellular APIs that this Zephyr
 * does not have yet, and the PPP frame buffer size.
 */

#include <zephyr/drivers/modem/modem_cellular.h>
#include <zephyr/device.h>

#define DT_DRV_COMPAT nordic_nrf91_sm_v2

MODEM_CELLULAR_COMMON_CHAT_MATCHES();

MODEM_CHAT_MATCHES_DEFINE(nordic_nrf91_unsol, MODEM_CELLULAR_COMMON_UNSOL_MATCHES);

/* Upstream: MODEM_CELLULAR_OK_CHAT_MATCH_DEFINE(), which also matches the trailing OK. */
MODEM_CHAT_MATCH_DEFINE(xiccid_match, "%XICCID: ", "", modem_cellular_chat_on_iccid);
MODEM_CHAT_MATCH_DEFINE(uicc_initialized, "%XSIM: 1", "", NULL);

/*
 * Upstream: the identity matches also match the trailing OK, so each query is a single
 * MODEM_CHAT_SCRIPT_CMD_RESP_MULT() step instead of a response step followed by ok_match.
 */
MODEM_CHAT_SCRIPT_CMDS_DEFINE(init_chat_script_cmds,
	MODEM_CHAT_SCRIPT_CMD_RESP("AT", ok_match),
	MODEM_CHAT_SCRIPT_CMD_RESP("AT+CMEE=1", ok_match),
	MODEM_CHAT_SCRIPT_CMD_RESP("AT+CGSN", imei_match),
	MODEM_CHAT_SCRIPT_CMD_RESP("", ok_match),
	MODEM_CHAT_SCRIPT_CMD_RESP("AT+CGMM", cgmm_match),
	MODEM_CHAT_SCRIPT_CMD_RESP("", ok_match),
	MODEM_CHAT_SCRIPT_CMD_RESP("AT+CGMI", cgmi_match),
	MODEM_CHAT_SCRIPT_CMD_RESP("", ok_match),
	MODEM_CHAT_SCRIPT_CMD_RESP("AT+CGMR", cgmr_match),
	MODEM_CHAT_SCRIPT_CMD_RESP("", ok_match),
	MODEM_CHAT_SCRIPT_CMD_RESP("AT#XCMUXURC=1", ok_match),
	MODEM_CHAT_SCRIPT_CMD_RESP("AT%XSIM=1", ok_match),
	MODEM_CHAT_SCRIPT_CMD_RESP("AT+CFUN=41", uicc_initialized),
	MODEM_CHAT_SCRIPT_CMD_RESP("AT%XICCID", xiccid_match),
	MODEM_CHAT_SCRIPT_CMD_RESP("", ok_match),
	MODEM_CHAT_SCRIPT_CMD_RESP("AT+CIMI", cimi_match),
	MODEM_CHAT_SCRIPT_CMD_RESP("", ok_match),
	MODEM_CHAT_SCRIPT_CMD_RESP("AT%XSIM=0", ok_match),
	MODEM_CHAT_SCRIPT_CMD_RESP("AT+CMUX=0", ok_match));

MODEM_CHAT_SCRIPT_DEFINE(init_chat_script, init_chat_script_cmds, abort_matches,
			 modem_cellular_chat_callback_handler, 10);

MODEM_CHAT_SCRIPT_CMDS_DEFINE(network_cmds, MODEM_CHAT_SCRIPT_CMD_RESP("AT+CEREG=1", ok_match),
			      MODEM_CHAT_SCRIPT_CMD_RESP("AT+CEREG?", ok_match),
			      MODEM_CHAT_SCRIPT_CMD_RESP("AT+CFUN=1", ok_match));

MODEM_CHAT_SCRIPT_DEFINE(network_chat_script, network_cmds, abort_matches,
			 modem_cellular_chat_callback_handler, 10);

MODEM_CHAT_SCRIPT_CMDS_DEFINE(dial_chat_script_cmds,
			      MODEM_CHAT_SCRIPT_CMD_RESP("AT+CGDATA", connect_match));

MODEM_CHAT_SCRIPT_DEFINE(dial_chat_script, dial_chat_script_cmds, dial_abort_matches,
			 modem_cellular_chat_callback_handler, 10);

MODEM_CHAT_SCRIPT_CMDS_DEFINE(shutdown_chat_script_cmds,
			      MODEM_CHAT_SCRIPT_CMD_RESP("AT+CEREG=0", ok_match),
			      MODEM_CHAT_SCRIPT_CMD_RESP("AT+CFUN=0", ok_match));

MODEM_CHAT_SCRIPT_DEFINE(shutdown_chat_script, shutdown_chat_script_cmds, abort_matches,
			 modem_cellular_chat_callback_handler, 10);

static const struct modem_cellular_vendor_config nrf91_sm_vendor = {
	/* clang-format off */
	.scripts = {
		.init = &init_chat_script,
		.network = &network_chat_script,
		.dial = &dial_chat_script,
		.shutdown = &shutdown_chat_script,
	},
	.unsol_matches = {
		.matches = nordic_nrf91_unsol,
		.size = ARRAY_SIZE(nordic_nrf91_unsol),
	},
	/* clang-format on */
	/* Upstream: .chat_delimiter = "\r\n" is set here instead of in modem_cellular_data. */
	.power_pulse_duration_ms = 0,
	.reset_pulse_duration_ms = 500,
	.startup_time_ms = 2000,
	.shutdown_time_ms = 100,
	.cmux_disconnect_timeout_ms = 500,
	.force_autostart = true,
};

/*
 * Upstream: the PPP partial frame buffer is 1500 bytes. This copy keeps 64, as the larger buffer
 * lets PPP hand large bursts to the CMUX link (MTU 127), which stalls under sustained FOTA
 * download load.
 *
 * Upstream: modem_cellular_data is zero-initialized, as MODEM_CELLULAR_DEFINE_INSTANCE() takes the
 * PPP instance and a third, vendor-specific argument (NULL).
 */
#define MODEM_CELLULAR_DEVICE_NORDIC_NRF91_SM_V2(inst)                                             \
	MODEM_DT_INST_PPP_DEFINE(inst, MODEM_CELLULAR_INST_NAME(ppp, inst), NULL, 1500, 64);       \
                                                                                                   \
	static struct modem_cellular_data MODEM_CELLULAR_INST_NAME(data, inst) = {                 \
		.chat_delimiter = "\r\n",                                                          \
		.ppp = &MODEM_CELLULAR_INST_NAME(ppp, inst),                                       \
	};                                                                                         \
                                                                                                   \
	MODEM_CELLULAR_DEFINE_AND_INIT_USER_PIPES(inst, (user_pipe_0, 3), (user_pipe_1, 4))        \
                                                                                                   \
	MODEM_CELLULAR_DEFINE_INSTANCE(inst, &nrf91_sm_vendor)

DT_INST_FOREACH_STATUS_OKAY(MODEM_CELLULAR_DEVICE_NORDIC_NRF91_SM_V2)

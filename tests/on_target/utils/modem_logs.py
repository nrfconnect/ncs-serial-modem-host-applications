# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
"""Enable Serial Modem application logs for the remainder of a test."""

from __future__ import annotations

import re
import time
import types

from utils.logger import get_logger

logger = get_logger()

ENABLE_LOGS_AT_COMMAND = "AT#XLOG=1"
ENABLE_LOGS_SHELL_COMMAND = f'modem at "{ENABLE_LOGS_AT_COMMAND}"'

# Responses from the in-tree `modem at` shell when the CMUX AT pipe is not
# usable. CMUX runtime power save closes the pipe after the idle timeout, so
# this is an expected transient rather than an error.
PIPE_UNAVAILABLE_RESPONSES = (
    "modem is not ready",
    "AT pipe busy",
    "AT command failed",
)
_OK_RESPONSE = re.compile(r"^OK\s*$", re.MULTILINE)


def _send_and_confirm(dut: types.SimpleNamespace, *, timeout: float) -> bool:
    offset = len(dut.uart.snapshot_log())

    try:
        dut.uart.write_line(ENABLE_LOGS_SHELL_COMMAND)
    except (OSError, RuntimeError, TimeoutError) as exc:
        # OSError covers serial.SerialException from a closed or lost port.
        logger.warning("Could not send %r: %s", ENABLE_LOGS_SHELL_COMMAND, exc)
        return False

    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        tail = dut.uart.snapshot_log()[offset:]
        # Everything from the shell's echo onwards is the response to our command.
        echo_index = tail.rfind(ENABLE_LOGS_AT_COMMAND)
        if echo_index >= 0:
            response = tail[echo_index:]
            if any(text in response for text in PIPE_UNAVAILABLE_RESPONSES):
                return False
            if _OK_RESPONSE.search(response):
                return True
        time.sleep(0.5)

    return False


def enable_modem_application_logs(
    dut: types.SimpleNamespace,
    *,
    attempts: int = 3,
    retry_delay: float = 3.0,
    timeout: float = 10.0,
) -> bool:
    """Resume the Serial Modem log UART so the modem console keeps logging.

    Serial Modem prints its boot output over VCOM1 and then suspends both the
    log backend and the UART to avoid the power overhead, so the console stays
    silent for the rest of the run until ``AT#XLOG=1`` is issued. The command
    travels over the host's CMUX AT pipe, which only exists once the modem has
    attached, so call this after the host reports a cloud connection.

    Returns True once the modem acknowledges. Modem logs are a diagnostic aid,
    so failure is warned about rather than raised: it must not fail a test whose
    functional assertions all pass.
    """
    if dut.modem_uart is None:
        return False

    for attempt in range(1, attempts + 1):
        if _send_and_confirm(dut, timeout=timeout):
            logger.info("Serial Modem application logs enabled (%s)", ENABLE_LOGS_AT_COMMAND)
            return True
        if attempt < attempts:
            logger.info(
                "Serial Modem AT pipe not ready for %s, retrying in %.0fs (%d/%d)",
                ENABLE_LOGS_AT_COMMAND,
                retry_delay,
                attempt,
                attempts,
            )
            time.sleep(retry_delay)

    logger.warning(
        "Could not enable Serial Modem application logs with %s; the modem console "
        "will only contain boot output for this test",
        ENABLE_LOGS_AT_COMMAND,
    )
    return False

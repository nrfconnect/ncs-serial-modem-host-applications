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
# this is an expected transient worth retrying.
PIPE_UNAVAILABLE_RESPONSES = (
    "modem is not ready",
    "AT pipe busy",
    "AT command failed",
)
# The running image predates CONFIG_MODEM_AT_SHELL. Retrying cannot help; a FOTA
# update payload built before that option was added behaves this way.
SHELL_MISSING_RESPONSE = "command not found"

_ANSI_ESCAPE = re.compile(r"\x1b\[[0-9;?]*[a-zA-Z]")
_SHELL_PROMPT = re.compile(r"uart:~\$\s*")
_OK_RESPONSE = re.compile(r"^OK\s*$", re.MULTILINE)


class _ShellCommandMissing(Exception):
    """The running firmware does not have the `modem at` shell command."""


def _readable(text: str) -> str:
    """Strip ANSI escapes and shell prompts interleaved into captured output.

    The shell reprints its prompt around streaming log lines, so a bare `OK`
    reaches the log as `\x1b[1;32muart:~$ \x1b[m\x1b[8D\x1b[JOK`.
    """
    return _SHELL_PROMPT.sub("", _ANSI_ESCAPE.sub("", text))


def _send_and_confirm(dut: types.SimpleNamespace, *, timeout: float) -> bool:
    offset = len(dut.uart.snapshot_log())

    try:
        dut.uart.write_line(ENABLE_LOGS_SHELL_COMMAND)
    except (OSError, RuntimeError, TimeoutError) as exc:
        # OSError covers serial.SerialException from a closed or lost port.
        logger.warning("Could not send %r: %s", ENABLE_LOGS_SHELL_COMMAND, exc)
        return False

    deadline = time.monotonic() + timeout
    response = ""
    while time.monotonic() < deadline:
        tail = _readable(dut.uart.snapshot_log()[offset:])
        # Everything from the shell's echo onwards is the response to our command.
        echo_index = tail.rfind(ENABLE_LOGS_AT_COMMAND)
        if echo_index >= 0:
            response = tail[echo_index + len(ENABLE_LOGS_AT_COMMAND):]
            if SHELL_MISSING_RESPONSE in response:
                raise _ShellCommandMissing(
                    next(
                        line.strip()
                        for line in response.splitlines()
                        if SHELL_MISSING_RESPONSE in line
                    )
                )
            if any(text in response for text in PIPE_UNAVAILABLE_RESPONSES):
                logger.debug("Serial Modem AT pipe unavailable: %s", response.strip())
                return False
            if _OK_RESPONSE.search(response):
                return True
        time.sleep(0.5)

    logger.debug(
        "No verdict for %s within %.0fs; response so far: %r",
        ENABLE_LOGS_AT_COMMAND,
        timeout,
        response.strip(),
    )
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
        try:
            confirmed = _send_and_confirm(dut, timeout=timeout)
        except _ShellCommandMissing as exc:
            logger.warning(
                "Host firmware has no `modem at` shell command (%s); it predates "
                "CONFIG_MODEM_AT_SHELL, so Serial Modem logs stay at boot output only",
                exc,
            )
            return False

        if confirmed:
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

# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
"""Enable Serial Modem application logs for the remainder of a test."""

from __future__ import annotations

import re
import time
import types

from utils.logger import get_logger

logger = get_logger()

ENABLE_LOGS_AT_COMMAND = "AT#XLOG=1"
# Reports the mode the modem believes it is in. Worth recording because Serial
# Modem answers OK without touching the backend when the mode asked for already
# matches the one it recorded, so an OK alone does not pin down the state.
QUERY_LOGS_AT_COMMAND = "AT#XLOG?"

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
_XLOG_STATE = re.compile(r"#XLOG:\s*(\d)")


class _ShellCommandMissing(Exception):
    """The running firmware does not have the `modem at` shell command."""


def _readable(text: str) -> str:
    """Strip ANSI escapes and shell prompts interleaved into captured output.

    The shell reprints its prompt around streaming log lines, so a bare `OK`
    reaches the log as `\x1b[1;32muart:~$ \x1b[m\x1b[8D\x1b[JOK`.
    """
    return _SHELL_PROMPT.sub("", _ANSI_ESCAPE.sub("", text))


def _send_at(
    dut: types.SimpleNamespace, at_command: str, *, timeout: float
) -> str | None:
    """Run *at_command* through the host's `modem at` shell.

    Returns the modem's response once it ends in `OK`, or None if the AT pipe is
    unavailable or no verdict arrives within *timeout*.
    """
    shell_command = f'modem at "{at_command}"'
    offset = len(dut.uart.snapshot_log())

    try:
        dut.uart.write_line(shell_command)
    except (OSError, RuntimeError, TimeoutError) as exc:
        # OSError covers serial.SerialException from a closed or lost port.
        logger.warning("Could not send %r: %s", shell_command, exc)
        return None

    deadline = time.monotonic() + timeout
    response = ""
    while time.monotonic() < deadline:
        tail = _readable(dut.uart.snapshot_log()[offset:])
        # Everything from the shell's echo onwards is the response to our command.
        echo_index = tail.rfind(at_command)
        if echo_index >= 0:
            response = tail[echo_index + len(at_command):]
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
                return None
            if _OK_RESPONSE.search(response):
                return response
        time.sleep(0.5)

    logger.debug(
        "No verdict for %s within %.0fs; response so far: %r",
        at_command,
        timeout,
        response.strip(),
    )
    return None


def _log_reported_state(dut: types.SimpleNamespace, *, timeout: float) -> None:
    """Record whether the modem itself considers logging active.

    Purely diagnostic. It separates the two ways the modem log can come back
    holding only boot output: state 0 means the enable did not stick, while
    state 1 means the backend is on and the console path is where to look next.
    """
    response = _send_at(dut, QUERY_LOGS_AT_COMMAND, timeout=timeout)
    if response is None:
        logger.info("Serial Modem did not answer %s", QUERY_LOGS_AT_COMMAND)
        return

    match = _XLOG_STATE.search(response)
    if match is None:
        logger.info("Unparsed %s response: %r", QUERY_LOGS_AT_COMMAND, response.strip())
    elif match.group(1) == "1":
        logger.info("Serial Modem reports logging active (#XLOG: 1)")
    else:
        logger.warning(
            "Serial Modem reports logging inactive (#XLOG: %s) despite acknowledging %s",
            match.group(1),
            ENABLE_LOGS_AT_COMMAND,
        )


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

    Once the modem acknowledges, ``AT#XLOG?`` records the state it reports, so a
    console that stays silent can be attributed rather than guessed at.

    Returns True once the modem acknowledges. Modem logs are a diagnostic aid,
    so failure is warned about rather than raised: it must not fail a test whose
    functional assertions all pass.
    """
    if dut.modem_uart is None:
        return False

    for attempt in range(1, attempts + 1):
        try:
            confirmed = _send_at(dut, ENABLE_LOGS_AT_COMMAND, timeout=timeout) is not None
        except _ShellCommandMissing as exc:
            logger.warning(
                "Host firmware has no `modem at` shell command (%s); it predates "
                "CONFIG_MODEM_AT_SHELL, so Serial Modem logs stay at boot output only",
                exc,
            )
            return False

        if confirmed:
            logger.info("Serial Modem application logs enabled (%s)", ENABLE_LOGS_AT_COMMAND)
            _log_reported_state(dut, timeout=timeout)
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

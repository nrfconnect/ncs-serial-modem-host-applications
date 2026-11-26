# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
from __future__ import annotations

import re
import time

import serial

from utils.console import strip_ansi
from utils.logger import get_logger

logger = get_logger()

SHELL_PROMPT = "uart:~$"
# An idle shell only reprints its prompt on input or async log output.
PROMPT_NUDGE_INTERVAL = 5.0


def _wait_for_prompt_in_buffer(
    ser: serial.Serial,
    *,
    timeout: float,
    initial_buffer: str = "",
) -> str:
    deadline = time.monotonic() + timeout
    buffer = initial_buffer
    next_nudge = time.monotonic()

    while time.monotonic() < deadline:
        if time.monotonic() >= next_nudge:
            ser.write(b"\r\n")
            ser.flush()
            next_nudge = time.monotonic() + PROMPT_NUDGE_INTERVAL

        waiting = ser.in_waiting
        data = ser.read(waiting if waiting else 1)
        if not data:
            continue

        buffer += data.decode("utf-8", errors="replace")
        if SHELL_PROMPT in buffer:
            return buffer

        if len(buffer) > 8192:
            buffer = buffer[-4096:]

    raise TimeoutError(
        f"Timed out after {timeout:.0f}s waiting for shell prompt on {ser.port!r}"
    )


def wait_for_shell_prompt(
    serial_port: str,
    *,
    timeout: float = 60.0,
    baudrate: int = 115200,
) -> None:
    with serial.Serial(
        serial_port,
        baudrate=baudrate,
        timeout=1.0,
    ) as ser:
        ser.dtr = True
        ser.rts = True

        if ser.in_waiting:
            ser.reset_input_buffer()

        _wait_for_prompt_in_buffer(ser, timeout=timeout)
        logger.info("Shell prompt detected on %s", serial_port)


def send_shell_command(
    serial_port: str,
    command: str,
    *,
    timeout: float = 60.0,
    baudrate: int = 115200,
    wait_for_completion: bool = True,
) -> None:
    """Pass wait_for_completion=False for commands that crash or reboot the device."""
    with serial.Serial(
        serial_port,
        baudrate=baudrate,
        timeout=1.0,
    ) as ser:
        ser.dtr = True
        ser.rts = True

        if ser.in_waiting:
            ser.reset_input_buffer()

        _wait_for_prompt_in_buffer(ser, timeout=timeout)

        logger.info("Sending shell command on %s: %r", serial_port, command)
        ser.write(f"{command}\r\n".encode("utf-8"))
        ser.flush()

        if not wait_for_completion:
            logger.info("Shell command sent on %s (not waiting for prompt)", serial_port)
            return

        _wait_for_prompt_in_buffer(ser, timeout=timeout)
        logger.info("Shell command completed on %s", serial_port)


def send_shell_command_until(
    serial_port: str,
    command: str,
    pattern: str,
    *,
    timeout: float = 60.0,
    baudrate: int = 115200,
) -> str:
    """Return the ANSI-stripped output once regex *pattern* matches.

    Unlike waiting for the prompt, this tolerates log lines that make the shell
    reprint its prompt mid-output.
    """
    regex = re.compile(pattern, re.MULTILINE)
    with serial.Serial(
        serial_port,
        baudrate=baudrate,
        timeout=1.0,
    ) as ser:
        ser.dtr = True
        ser.rts = True

        if ser.in_waiting:
            ser.reset_input_buffer()

        _wait_for_prompt_in_buffer(ser, timeout=timeout)

        logger.info("Sending shell command on %s: %r", serial_port, command)
        ser.write(f"{command}\r\n".encode("utf-8"))
        ser.flush()

        raw = ""
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            waiting = ser.in_waiting
            data = ser.read(waiting if waiting else 1)
            if not data:
                continue

            raw += data.decode("utf-8", errors="replace")
            clean = strip_ansi(raw)
            if regex.search(clean):
                logger.info("Shell command output matched %r on %s", pattern, serial_port)
                return clean

    raise TimeoutError(
        f"Timed out after {timeout:.0f}s waiting for output matching {pattern!r} from "
        f"command {command!r} on {serial_port!r}. Captured:\n{strip_ansi(raw)}"
    )

# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
"""Zephyr shell interaction over UART."""

from __future__ import annotations

import time

import serial

from utils.logger import get_logger

logger = get_logger()

SHELL_PROMPT = "uart:~$"


def _wait_for_prompt_in_buffer(
    ser: serial.Serial,
    *,
    timeout: float,
    initial_buffer: str = "",
) -> str:
    deadline = time.monotonic() + timeout
    buffer = initial_buffer

    while time.monotonic() < deadline:
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
    """Block until the Zephyr shell prompt appears on *serial_port*."""
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
    """Send a Zephyr shell command.

    When *wait_for_completion* is False, return after writing the command. Use
    this for commands that crash or reboot the device (e.g. ``mflt test hardfault``).
    """
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

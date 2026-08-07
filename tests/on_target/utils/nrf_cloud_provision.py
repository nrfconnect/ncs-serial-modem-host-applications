# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
"""Provision nRF Cloud credentials using nrfcloud-utils."""

from __future__ import annotations

import subprocess
import time
from pathlib import Path

import serial

from utils.flash_tools import nrfutil_reset
from utils.logger import get_logger

logger = get_logger()

SHELL_PROMPT = "uart:~$"


def wait_for_shell_prompt(
    serial_port: str,
    *,
    timeout: float = 60.0,
    baudrate: int = 115200,
) -> None:
    """Block until the Zephyr shell prompt appears on *serial_port*."""
    deadline = time.monotonic() + timeout
    buffer = ""

    with serial.Serial(
        serial_port,
        baudrate=baudrate,
        timeout=1.0,
    ) as ser:
        ser.dtr = True
        ser.rts = True

        if ser.in_waiting:
            ser.reset_input_buffer()

        while time.monotonic() < deadline:
            waiting = ser.in_waiting
            data = ser.read(waiting if waiting else 1)
            if not data:
                continue

            buffer += data.decode("utf-8", errors="replace")
            if SHELL_PROMPT in buffer:
                logger.info("Shell prompt detected on %s", serial_port)
                return

            if len(buffer) > 8192:
                buffer = buffer[-4096:]

    raise TimeoutError(
        f"Timed out after {timeout:.0f}s waiting for shell prompt on {serial_port!r}"
    )


def install_device_credentials(
    *,
    work_dir: Path,
    device_id: str,
    serial_port: str,
    ca_cert: Path,
    ca_key: Path,
    segger_sn: str | None = None,
) -> Path:
    """Run device_credentials_installer and return the generated onboard.csv path."""
    work_dir.mkdir(parents=True, exist_ok=True)
    onboard_csv = work_dir / "onboard.csv"

    if segger_sn is not None:
        logger.info("Resetting device before credential install")
        nrfutil_reset(segger_sn)
        time.sleep(2)
        wait_for_shell_prompt(serial_port)

    command = [
        "device_credentials_installer",
        "--ca",
        str(ca_cert),
        "--ca-key",
        str(ca_key),
        "--id-str",
        device_id,
        "-s",
        "-d",
        "--verify",
        "--coap",
        "--local-cert",
        "--cmd-type",
        "tls_cred_shell",
        "--port",
        serial_port,
    ]

    logger.info("Installing device credentials for %s", device_id)
    subprocess.run(command, cwd=work_dir, check=True)

    if not onboard_csv.is_file():
        raise FileNotFoundError(f"Expected onboarding CSV not found: {onboard_csv}")

    return onboard_csv

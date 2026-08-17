# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
"""Provision nRF Cloud credentials using nrfcloud-utils."""

from __future__ import annotations

import subprocess
import time
from pathlib import Path

from utils.flash_tools import nrfutil_reset
from utils.logger import get_logger
from utils.shell import wait_for_shell_prompt

logger = get_logger()


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

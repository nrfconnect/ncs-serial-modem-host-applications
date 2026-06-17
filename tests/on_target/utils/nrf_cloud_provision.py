# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
"""Provision nRF Cloud credentials using nrfcloud-utils."""

from __future__ import annotations

import subprocess
from pathlib import Path

from utils.logger import get_logger

logger = get_logger()


def install_device_credentials(
    *,
    work_dir: Path,
    device_id: str,
    serial_port: str,
    ca_cert: Path,
    ca_key: Path,
) -> Path:
    """Run device_credentials_installer and return the generated onboard.csv path."""
    work_dir.mkdir(parents=True, exist_ok=True)
    onboard_csv = work_dir / "onboard.csv"

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

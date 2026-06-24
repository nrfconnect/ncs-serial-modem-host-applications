# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause

from __future__ import annotations

import json
import os
import re
import time
from pathlib import Path

REPO_ROOT = Path(os.environ["REPO_ROOT"])
SERIAL_LOG = REPO_ROOT / "build" / "hardware-serial.log"

# nRF54L15 host client ID from CONFIG_NRF_CLOUD_CLIENT_ID_SRC_HW_ID.
DUT_DEVICE_ID_RE = re.compile(r"^[0-9A-F]{16}$")
DEVICE_ID_LOG_RE = re.compile(
    r"(?:Device ID:|nRF Cloud client ID:)\s*([0-9A-Fa-f]{16})"
)


def load_test_config() -> dict:
    return json.loads(os.environ["TEST_JSON"])


def load_expected_device_id(test_config: dict) -> str:
    hardware = test_config.get("hardware", {})
    device_id_var = hardware.get("device_id_var", "CI_NRF54L15_DEVICE_ID")
    try:
        device_id = os.environ[device_id_var]
    except KeyError as exc:
        raise RuntimeError(
            f"{device_id_var} must be set to the DUT device ID allowlist "
            "before running cloud connect tests"
        ) from exc

    normalized = device_id.strip().upper()
    if not DUT_DEVICE_ID_RE.fullmatch(normalized):
        raise ValueError(
            f"{device_id_var} must be a 16-character hex device ID, got {device_id!r}"
        )
    return normalized


def assert_dut_device_id(device_id: str, expected_device_id: str) -> str:
    """Refuse cloud operations unless *device_id* matches the configured DUT."""
    normalized = device_id.strip().upper()
    if not DUT_DEVICE_ID_RE.fullmatch(normalized):
        raise ValueError(
            f"Refusing nRF Cloud operation for invalid device ID format: {device_id!r}"
        )
    if normalized != expected_device_id:
        raise ValueError(
            "Refusing nRF Cloud operation: serial log device ID "
            f"{normalized} does not match configured DUT allowlist {expected_device_id}"
        )
    return normalized


def parse_device_id(serial_log: str) -> str:
    match = DEVICE_ID_LOG_RE.search(serial_log)
    if not match:
        raise ValueError("Device ID not found in serial log")
    normalized = match.group(1).upper()
    if not DUT_DEVICE_ID_RE.fullmatch(normalized):
        raise ValueError(f"Unexpected device ID format in serial log: {normalized!r}")
    return normalized


def wait_for_device_id(uart, *, timeout: float = 120.0, poll_interval: float = 1.0) -> str:
    """Block until a device ID appears in the captured serial log."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            return parse_device_id(uart.whole_log)
        except ValueError:
            time.sleep(poll_interval)
    raise TimeoutError(
        f"Timed out after {timeout:.0f}s waiting for device ID in serial log"
    )

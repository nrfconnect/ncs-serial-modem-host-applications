# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
"""Onboard a Serial Modem to nRF Cloud from an AT host application (AT#XNRFPROV)."""

from __future__ import annotations

import re
import time

import pytest

from utils.helpers import MODEM_UUID_LOG_RE, assert_dut_modem_uuid
from utils.logger import get_logger
from utils.nrf_cloud_device import claim_modem, device_exists

logger = get_logger()

ATTESTATION_TOKEN_LOG_RE = re.compile(r"Attestation token:\s*([A-Za-z0-9_\-]+\.[A-Za-z0-9_\-]+)")
PROVISIONING_DONE_LOG = "Provisioning done"
CLOUD_CONNECTED_LOG = "Cloud connected"

BOOT_TIMEOUT = 120.0
# The application retries provisioning every CONFIG_APP_CLOUD_RETRY_SECONDS (60 s), and a
# session that installs credentials takes LTE down and up again.
PROVISIONING_TIMEOUT = 600.0
CLOUD_CONNECT_TIMEOUT = 120.0
DEVICE_LISTED_TIMEOUT = 60.0


def _wait_for_log_match(uart, pattern: re.Pattern, *, timeout: float) -> str:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        match = pattern.search(uart.snapshot_log())
        if match:
            return match.group(1)
        time.sleep(1)
    raise TimeoutError(f"Timed out after {timeout:.0f}s waiting for {pattern.pattern!r}")


@pytest.mark.slow
def test_at_cloud_provision(at_provision_dut) -> None:
    """Claim the unclaimed DUT modem and verify that it onboards and connects to nRF Cloud."""
    dut = at_provision_dut

    logger.info("Wait for the modem UUID and the not-claimed attestation token")
    modem_uuid = _wait_for_log_match(dut.uart, MODEM_UUID_LOG_RE, timeout=BOOT_TIMEOUT)
    assert_dut_modem_uuid(modem_uuid, dut.expected_uuid)
    token = _wait_for_log_match(dut.uart, ATTESTATION_TOKEN_LOG_RE, timeout=BOOT_TIMEOUT)

    claim_modem(token, modem_uuid, dut.expected_uuid)

    logger.info("Wait for the Serial Modem to install its nRF Cloud credentials")
    dut.uart.wait_for_substring(PROVISIONING_DONE_LOG, timeout=PROVISIONING_TIMEOUT)
    dut.uart.wait_for_substring(CLOUD_CONNECTED_LOG, timeout=CLOUD_CONNECT_TIMEOUT)

    deadline = time.monotonic() + DEVICE_LISTED_TIMEOUT
    while not device_exists(dut.expected_uuid):
        if time.monotonic() > deadline:
            pytest.fail(f"Device {dut.expected_uuid} connected but is not listed in nRF Cloud")
        time.sleep(5)

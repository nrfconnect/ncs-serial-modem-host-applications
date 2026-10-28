# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
"""Onboard an nRF93M1 to nRF Cloud with its registration JWT (AT%REGJWT)."""

from __future__ import annotations

import base64
import json
import os
import re
import time

import pytest

from utils.helpers import MODEM_UUID_LOG_RE, assert_dut_modem_uuid
from utils.logger import get_logger
from utils.modem_logs import send_at
from utils.nrf_cloud_device import device_exists, onboard_modem_with_jwt

logger = get_logger()

NETWORK_CONNECTED_LOG = "Network connected"
UNAUTHORIZED_LOG = "Failed to request location, unauthorized"
REJECTED_LOG = "Failed to request location, request rejected"
SYNC_COMMAND = "button 1"
REGJWT_RE = re.compile(r"%REGJWT:\s*([A-Za-z0-9_\-]+\.[A-Za-z0-9_\-]+\.[A-Za-z0-9_\-]+)")
LOCATION_FIX_RE = re.compile(r"Location: [-\d.]+,[-\d.]+ Uncertainty: \d+m Method: .+")

NETWORK_CONNECT_TIMEOUT = 300.0
BOOT_TIMEOUT = 60.0
# The boot sync runs CONFIG_APP_SYNC_BOOT_DELAY_SECONDS (5 s) after connect, and its
# location request is refused with CoAP 4.01 while the modem is not onboarded.
UNAUTHORIZED_TIMEOUT = 120.0
REGJWT_TIMEOUT = 120.0
REGJWT_ATTEMPT_TIMEOUT = 15.0
DEVICE_LISTED_TIMEOUT = 60.0
# A sync that nRF Cloud still refuses is retried, since the onboarding can take a
# moment to reach the CoAP service.
LOCATION_TIMEOUT = 300.0
LOCATION_ATTEMPT_TIMEOUT = 90.0
LOCATION_RETRY_DELAY = 15.0


def _wait_for_log_match(uart, pattern: re.Pattern, *, timeout: float) -> str:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        match = pattern.search(uart.snapshot_log())
        if match:
            return match.group(1)
        time.sleep(1)
    raise TimeoutError(f"Timed out after {timeout:.0f}s waiting for {pattern.pattern!r}")


def _wait_for_unauthorized(uart, modem_uuid: str) -> None:
    """Wait for nRF Cloud to refuse the boot sync, which shows the modem is not onboarded.

    A location fix instead means the modem is still onboarded in another account,
    which the delete cannot reach, so fail at once rather than waiting out the timeout.
    """
    deadline = time.monotonic() + UNAUTHORIZED_TIMEOUT
    while time.monotonic() < deadline:
        captured = uart.snapshot_log()
        if UNAUTHORIZED_LOG in captured:
            return
        if LOCATION_FIX_RE.search(captured):
            pytest.fail(
                f"DUT {modem_uuid} got a location fix before onboarding. It is still "
                "onboarded in an nRF Cloud project other than MEMFAULT_PROJECT, so the "
                "test cannot remove it. Delete the device in that project "
                "(Devices > Delete Device) and rerun."
            )
        time.sleep(1)
    raise TimeoutError(
        f"Timed out after {UNAUTHORIZED_TIMEOUT:.0f}s waiting for {UNAUTHORIZED_LOG!r}"
    )


def _tenant_id(test_config: dict) -> str:
    """Return the nRF Cloud tenant ID, the identifier a registration JWT binds to.

    The Organization Auth Token cannot read it from nRF Cloud, so CI provides it.
    """
    name = test_config.get("nrf_cloud", {}).get("tenant_id_var", "NRF_CLOUD_TENANT_ID")
    tenant_id = os.environ.get(name)
    if not tenant_id:
        raise RuntimeError(f"{name} must be set to the nRF Cloud tenant ID")
    return tenant_id


def _registration_jwt(dut, tenant_id: str) -> str:
    """Read a registration JWT for *tenant_id*, retrying while the AT pipe is busy."""
    command = f'AT%REGJWT="{tenant_id}"'
    deadline = time.monotonic() + REGJWT_TIMEOUT
    while time.monotonic() < deadline:
        response = send_at(dut, command, timeout=REGJWT_ATTEMPT_TIMEOUT)
        if response is not None:
            match = REGJWT_RE.search(response)
            if not match:
                raise ValueError(f"No JWT in the %REGJWT response: {response!r}")
            return match.group(1)
        time.sleep(2)
    raise TimeoutError(f"Timed out after {REGJWT_TIMEOUT:.0f}s running {command}")


def _jwt_claims(token: str) -> dict:
    payload = token.split(".")[1]
    return json.loads(base64.urlsafe_b64decode(payload + "=" * (-len(payload) % 4)))


def _wait_for_location_fix(uart) -> str:
    """Start syncs until one returns a location fix, which needs an authorized modem."""
    deadline = time.monotonic() + LOCATION_TIMEOUT
    while time.monotonic() < deadline:
        offset = len(uart.snapshot_log())
        uart.write_line(SYNC_COMMAND)

        attempt_deadline = min(deadline, time.monotonic() + LOCATION_ATTEMPT_TIMEOUT)
        refused = None
        while time.monotonic() < attempt_deadline:
            tail = uart.snapshot_log()[offset:]
            match = LOCATION_FIX_RE.search(tail)
            if match:
                return match.group(0)
            refused = next((log for log in (UNAUTHORIZED_LOG, REJECTED_LOG) if log in tail), None)
            if refused:
                break
            time.sleep(1)

        logger.info("No location fix yet (%s), retrying the sync", refused or "no response")
        time.sleep(LOCATION_RETRY_DELAY)

    raise TimeoutError(f"Timed out after {LOCATION_TIMEOUT:.0f}s waiting for a location fix")


@pytest.mark.slow
def test_at_regjwt_provision(at_provision_dut, test_config: dict) -> None:
    """Onboard the removed DUT modem with a registration JWT and verify a cloud round trip."""
    dut = at_provision_dut
    tenant_id = _tenant_id(test_config)

    logger.info("Wait for LTE and the modem UUID")
    dut.uart.wait_for_substring(NETWORK_CONNECTED_LOG, timeout=NETWORK_CONNECT_TIMEOUT)
    modem_uuid = _wait_for_log_match(dut.uart, MODEM_UUID_LOG_RE, timeout=BOOT_TIMEOUT)
    assert_dut_modem_uuid(modem_uuid, dut.expected_uuid)

    logger.info("Check that nRF Cloud refuses the modem before onboarding")
    _wait_for_unauthorized(dut.uart, modem_uuid)

    registration_jwt = _registration_jwt(dut, tenant_id)
    claims = _jwt_claims(registration_jwt)
    subject = str(claims.get("sub", "")).lower()
    assert subject == dut.expected_uuid, f"JWT sub {subject!r} is not the DUT"
    assert claims.get("id") == tenant_id, f"JWT id {claims.get('id')!r} is not the tenant ID"

    onboard_modem_with_jwt(registration_jwt, modem_uuid, dut.expected_uuid)

    deadline = time.monotonic() + DEVICE_LISTED_TIMEOUT
    while not device_exists(dut.expected_uuid):
        if time.monotonic() > deadline:
            pytest.fail(f"Device {dut.expected_uuid} onboarded but is not listed in nRF Cloud")
        time.sleep(5)

    logger.info("Verify that the onboarded modem gets a location fix from nRF Cloud")
    fix = _wait_for_location_fix(dut.uart)
    logger.info("%s", fix)

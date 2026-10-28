# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
"""nRF Cloud device management for on-target provisioning tests.

Devices are created, looked up and deleted in the linked nRF Cloud project with
an Organization Auth Token, following the nRF Cloud device onboarding quickstart:
https://docs.nrfcloud.com/docs/best-practices/device-onboarding-quickstart
"""

from __future__ import annotations

import os
import subprocess
import tempfile
import time
import urllib.error
import urllib.request
from pathlib import Path

from utils.helpers import assert_dut_device_id, assert_dut_modem_uuid
from utils.logger import get_logger

logger = get_logger()

API_HOST = os.environ.get("NRF_CLOUD_API_HOST", "https://api.nrfcloud.com/v1")

# The nrfcloud CLI reads these too, through its MEMFAULT_ environment prefix.
ORG_ENV_VARS = ("MEMFAULT_ORG_TOKEN", "MEMFAULT_ORG", "MEMFAULT_PROJECT")

# Statuses that say "try again later" rather than "this request is wrong".
RETRY_STATUSES = frozenset({429, 500, 502, 503, 504})
RETRY_STATUS_ATTEMPTS = 4
RETRY_MAX_DELAY_SECONDS = 60.0


def _org_env() -> dict[str, str]:
    missing = [name for name in ORG_ENV_VARS if not os.environ.get(name)]
    if missing:
        raise RuntimeError(f"nRF Cloud organization variables must be set: {', '.join(missing)}")
    return {name: os.environ[name] for name in ORG_ENV_VARS}


def _device_url(device_id: str) -> str:
    env = _org_env()
    return (
        f"{API_HOST}/organizations/{env['MEMFAULT_ORG']}/projects/{env['MEMFAULT_PROJECT']}"
        f"/long-range/devices/{device_id}"
    )


def _retry_after(exc: urllib.error.HTTPError) -> float:
    """Seconds the server asked us to wait, 0 when it did not say or said a date."""
    try:
        return float(exc.headers.get("Retry-After", ""))
    except (AttributeError, TypeError, ValueError):
        return 0.0


def _request(method: str, url: str) -> int:
    """Send a request with the organization token and return its status, error or not."""
    headers = {"Authorization": f"Bearer {_org_env()['MEMFAULT_ORG_TOKEN']}"}
    request = urllib.request.Request(url, method=method, headers=headers)

    # A full suite run drives several DUTs through one nRF Cloud project, which
    # throttles the combined traffic with 429. Back off and retry rather than
    # failing a hardware test on another job's API calls.
    backoff = 5.0
    status = 0
    requested = 0.0

    for attempt in range(1, RETRY_STATUS_ATTEMPTS + 1):
        try:
            with urllib.request.urlopen(request) as response:
                return response.status
        except urllib.error.HTTPError as exc:
            status = exc.code
            requested = _retry_after(exc)

        if status not in RETRY_STATUSES or attempt == RETRY_STATUS_ATTEMPTS:
            break

        delay = min(max(requested, backoff), RETRY_MAX_DELAY_SECONDS)
        logger.info(
            "nRF Cloud %s %s returned HTTP %d, retrying in %.0fs (%d/%d)",
            method,
            url,
            status,
            delay,
            attempt,
            RETRY_STATUS_ATTEMPTS,
        )
        time.sleep(delay)
        backoff *= 2

    return status


def _delete(device_id: str) -> None:
    """Delete *device_id* from the project, which also releases its claim.

    nRF Cloud documents 202 whether or not the device exists; 404 is taken to mean
    the same.
    """
    logger.info("Deleting only the configured DUT %s from nRF Cloud", device_id)
    status = _request("DELETE", _device_url(device_id))
    if status not in {202, 204, 404}:
        raise RuntimeError(f"Failed to delete device {device_id}: HTTP {status}")
    logger.info("DUT %s deleted", device_id)


def _device_create(csv_path: Path) -> None:
    """Create the devices in *csv_path* with `nrfcloud device create`."""
    _org_env()
    command = ["nrfcloud", "device", "create", "--csv", str(csv_path)]
    result = subprocess.run(command, capture_output=True, text=True)
    output = "\n".join(part.strip() for part in (result.stdout, result.stderr) if part.strip())
    if output:
        logger.info("nrfcloud CLI output:\n%s", output)
    if result.returncode != 0:
        raise subprocess.CalledProcessError(
            result.returncode,
            command,
            output=result.stdout,
            stderr=result.stderr,
        )


def _create_one(device_id: str, credential_column: str, credential: str) -> None:
    with tempfile.TemporaryDirectory() as tmp:
        csv_path = Path(tmp) / "devices.csv"
        csv_path.write_text(f"deviceId,{credential_column}\n{device_id},{credential}\n")
        _device_create(csv_path)


def device_exists(device_id: str) -> bool:
    status = _request("GET", _device_url(device_id))
    if status == 200:
        return True
    if status == 404:
        return False
    raise RuntimeError(f"Unexpected status {status} checking device {device_id}")


def delete_if_exists(device_id: str, expected_device_id: str) -> None:
    _delete(assert_dut_device_id(device_id, expected_device_id))


def delete_modem_if_exists(modem_uuid: str, expected_uuid: str) -> None:
    """Delete and unclaim the configured DUT modem, so the next provisioning attempt is refused."""
    _delete(assert_dut_modem_uuid(modem_uuid, expected_uuid))


def claim_modem(attestation_token: str, modem_uuid: str, expected_uuid: str) -> None:
    """Claim the DUT modem with its attestation token and have nRF Cloud onboard it."""
    validated = assert_dut_modem_uuid(modem_uuid, expected_uuid)
    logger.info("Claiming DUT %s with its attestation token", validated)
    _create_one(validated, "attestationToken", attestation_token)
    logger.info("DUT %s claimed", validated)


def onboard_modem_with_jwt(registration_jwt: str, modem_uuid: str, expected_uuid: str) -> None:
    """Onboard the DUT modem with the registration JWT from AT%REGJWT."""
    validated = assert_dut_modem_uuid(modem_uuid, expected_uuid)
    logger.info("Onboarding DUT %s with its registration JWT", validated)
    _create_one(validated, "onboardingToken", registration_jwt)
    logger.info("DUT %s onboarded", validated)


def onboard(csv_path: str) -> None:
    """Onboard the DUT from the onboard.csv that device_credentials_installer writes."""
    csv = Path(csv_path)
    if not csv.is_file():
        raise FileNotFoundError(f"Onboarding CSV not found: {csv}")

    logger.info("Onboarding DUT from %s", csv)
    _device_create(csv)

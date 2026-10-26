# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
"""nRF Cloud device management for on-target provisioning tests."""

from __future__ import annotations

import json
import os
import subprocess
import time
import urllib.error
import urllib.request

from utils.helpers import assert_dut_device_id, assert_dut_modem_uuid
from utils.logger import get_logger

logger = get_logger()

API_HOST = os.environ.get("NRF_CLOUD_API_HOST", "https://api.nrfcloud.com/v1")
PROVISIONING_API_HOST = os.environ.get(
    "NRF_CLOUD_PROVISIONING_API_HOST", "https://api.provisioning.nrfcloud.com/v1"
)

# Provisioning tag that makes nRF Cloud onboard a device when it is claimed.
ONBOARDING_TAG = "nrf-cloud-onboarding"

# Statuses that say "try again later" rather than "this request is wrong".
RETRY_STATUSES = frozenset({429, 500, 502, 503, 504})
RETRY_STATUS_ATTEMPTS = 4
RETRY_MAX_DELAY_SECONDS = 60.0


def _device_url(device_id: str) -> str:
    return f"{API_HOST}/devices/{device_id}"


def _claimed_device_url(device_id: str = "") -> str:
    url = f"{PROVISIONING_API_HOST}/claimed-devices"
    return f"{url}/{device_id}" if device_id else url


def _api_key() -> str:
    try:
        return os.environ["NRF_CLOUD_API_KEY"]
    except KeyError as exc:
        raise RuntimeError("NRF_CLOUD_API_KEY is required") from exc


def _retry_after(exc: urllib.error.HTTPError) -> float:
    """Seconds the server asked us to wait, 0 when it did not say or said a date."""
    try:
        return float(exc.headers.get("Retry-After", ""))
    except (AttributeError, TypeError, ValueError):
        return 0.0


def _request(method: str, url: str, *, api_key: str, body: dict | None = None) -> int:
    headers = {"Authorization": f"Bearer {api_key}"}
    data = None
    if body is not None:
        headers["Content-Type"] = "application/json"
        data = json.dumps(body).encode("utf-8")
    request = urllib.request.Request(url, data=data, method=method, headers=headers)

    # A full suite run drives several DUTs through one nRF Cloud account, which
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


def device_exists(device_id: str) -> bool:
    api_key = _api_key()
    status = _request("GET", _device_url(device_id), api_key=api_key)
    if status == 200:
        return True
    if status == 404:
        return False
    raise RuntimeError(f"Unexpected status {status} checking device {device_id}")


def delete_if_exists(device_id: str, expected_device_id: str) -> None:
    validated = assert_dut_device_id(device_id, expected_device_id)
    if not device_exists(validated):
        logger.info("DUT %s is not registered in nRF Cloud; nothing to delete", validated)
        return

    logger.info("Deleting only the configured DUT %s from nRF Cloud", validated)
    status = _request("DELETE", _device_url(validated), api_key=_api_key())
    if status not in {200, 202, 204}:
        raise RuntimeError(f"Failed to delete device {validated}: HTTP {status}")
    logger.info("DUT %s deleted", validated)


def delete_modem_if_exists(modem_uuid: str, expected_uuid: str) -> None:
    """Delete the configured DUT modem from the nRF Cloud fleet."""
    validated = assert_dut_modem_uuid(modem_uuid, expected_uuid)
    if not device_exists(validated):
        logger.info("DUT %s is not registered in nRF Cloud; nothing to delete", validated)
        return

    logger.info("Deleting only the configured DUT %s from nRF Cloud", validated)
    status = _request("DELETE", _device_url(validated), api_key=_api_key())
    if status not in {200, 202, 204}:
        raise RuntimeError(f"Failed to delete device {validated}: HTTP {status}")
    logger.info("DUT %s deleted", validated)


def unclaim_modem_if_claimed(modem_uuid: str, expected_uuid: str) -> None:
    """Unclaim the configured DUT modem, so the next provisioning attempt is refused."""
    validated = assert_dut_modem_uuid(modem_uuid, expected_uuid)
    status = _request("GET", _claimed_device_url(validated), api_key=_api_key())
    if status == 404:
        logger.info("DUT %s is not claimed; nothing to unclaim", validated)
        return
    if status != 200:
        raise RuntimeError(f"Unexpected status {status} checking claim of {validated}")

    logger.info("Unclaiming only the configured DUT %s", validated)
    status = _request("DELETE", _claimed_device_url(validated), api_key=_api_key())
    if status not in {200, 202, 204}:
        raise RuntimeError(f"Failed to unclaim device {validated}: HTTP {status}")
    logger.info("DUT %s unclaimed", validated)


def claim_modem(attestation_token: str, modem_uuid: str, expected_uuid: str) -> None:
    """Claim the DUT modem with its attestation token and have nRF Cloud onboard it."""
    validated = assert_dut_modem_uuid(modem_uuid, expected_uuid)
    logger.info("Claiming DUT %s with tag %s", validated, ONBOARDING_TAG)
    status = _request(
        "POST",
        _claimed_device_url(),
        api_key=_api_key(),
        body={"claimToken": attestation_token, "tags": [ONBOARDING_TAG]},
    )
    if status not in {200, 201, 202}:
        raise RuntimeError(f"Failed to claim device {validated}: HTTP {status}")
    logger.info("DUT %s claimed", validated)


def onboard(csv_path: str) -> None:
    csv = os.fspath(csv_path)
    if not os.path.isfile(csv):
        raise FileNotFoundError(f"Onboarding CSV not found: {csv}")

    logger.info("Onboarding DUT from %s", csv)
    subprocess.run(
        ["nrf_cloud_onboard", "--api-key", _api_key(), "--csv", csv],
        check=True,
    )

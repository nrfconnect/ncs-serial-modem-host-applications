# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
"""nRF Cloud device management for on-target provisioning tests."""

from __future__ import annotations

import os
import subprocess
import urllib.error
import urllib.request

from utils.helpers import assert_dut_device_id
from utils.logger import get_logger

logger = get_logger()

API_HOST = os.environ.get("NRF_CLOUD_API_HOST", "https://api.nrfcloud.com/v1")


def _device_url(device_id: str) -> str:
    return f"{API_HOST}/devices/{device_id}"


def _api_key() -> str:
    try:
        return os.environ["NRF_CLOUD_API_KEY"]
    except KeyError as exc:
        raise RuntimeError("NRF_CLOUD_API_KEY is required") from exc


def _request(method: str, url: str, *, api_key: str) -> int:
    request = urllib.request.Request(
        url,
        method=method,
        headers={"Authorization": f"Bearer {api_key}"},
    )
    try:
        with urllib.request.urlopen(request) as response:
            return response.status
    except urllib.error.HTTPError as exc:
        return exc.code


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
    if status not in {200, 204}:
        raise RuntimeError(f"Failed to delete device {validated}: HTTP {status}")
    logger.info("DUT %s deleted", validated)


def onboard(csv_path: str) -> None:
    csv = os.fspath(csv_path)
    if not os.path.isfile(csv):
        raise FileNotFoundError(f"Onboarding CSV not found: {csv}")

    logger.info("Onboarding DUT from %s", csv)
    subprocess.run(
        ["nrf_cloud_onboard", "--api-key", _api_key(), "--csv", csv],
        check=True,
    )

# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
"""Verify Wi-Fi location data reaching nRF Cloud, read from the DUT serial log."""

from __future__ import annotations

import re
import time

from utils.logger import get_logger

logger = get_logger()

# Logged by the cloud module when it hands a Wi-Fi scan to nRF Cloud, and when
# the ground-fix request has been accepted and resolved.
LOCATION_REQUEST_LOG = "Requesting location from nRF Cloud using"
LOCATION_SENT_LOG = "Location request sent to nRF Cloud"
LOCATION_NOT_FOUND_LOG = "location coordinates not found"

AP_COUNT_RE = re.compile(rf"{LOCATION_REQUEST_LOG} (\d+) Wi-Fi access points")


def parse_access_point_count(line: str) -> int:
    match = AP_COUNT_RE.search(line)
    if not match:
        raise ValueError(
            f"Wi-Fi access point count not found in serial log line: {line!r}"
        )
    return int(match.group(1))


def _log_after(captured: str, marker: str) -> str:
    marker_index = captured.find(marker)
    if marker_index < 0:
        return ""
    return captured[marker_index + len(marker):]


def wait_for_wifi_location_sent(
    uart,
    *,
    after: str,
    timeout: float = 180.0,
    poll_interval: float = 1.0,
) -> int:
    """Wait for nRF Cloud to accept a Wi-Fi scan and return the access point count.

    Both the scan and the cloud response must appear after the *after* marker, so
    only location data sent by the current boot counts.
    """
    request_line = uart.wait_for_substring_after(
        LOCATION_REQUEST_LOG,
        after=after,
        timeout=timeout,
    )
    access_points = parse_access_point_count(request_line)
    logger.info("DUT sent %d Wi-Fi access points to nRF Cloud", access_points)

    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if LOCATION_SENT_LOG in _log_after(uart.snapshot_log(), after):
            return access_points
        time.sleep(poll_interval)

    if LOCATION_NOT_FOUND_LOG in _log_after(uart.snapshot_log(), after):
        raise TimeoutError(
            "nRF Cloud did not resolve any Wi-Fi scan to a position within "
            f"{timeout:.0f}s: the access points around the DUT are unknown to the "
            "location service"
        )
    raise TimeoutError(
        f"Timed out after {timeout:.0f}s waiting for nRF Cloud to accept Wi-Fi "
        "location data"
    )

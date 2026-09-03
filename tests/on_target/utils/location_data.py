# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
"""Verify Wi-Fi location data reaching nRF Cloud, read from the DUT serial log.

The DUT sends a Wi-Fi scan to nRF Cloud and requests the resolved position back
(``do_reply = true``). The cloud module logs the coordinates and a Google Maps
URL, both of which are parsed here.
"""

from __future__ import annotations

import re
import time

from utils.logger import get_logger

logger = get_logger()

# Logged by the cloud module when it hands a Wi-Fi scan to nRF Cloud, and when
# nRF Cloud resolves the scan into a position and returns it to the device.
LOCATION_REQUEST_LOG = "Requesting location from nRF Cloud using"
LOCATION_RESULT_LOG = "Google maps URL:"
LOCATION_NOT_FOUND_LOG = "location coordinates not found"

AP_COUNT_RE = re.compile(rf"{LOCATION_REQUEST_LOG} (\d+) Wi-Fi access points")
# Matches the firmware log line, e.g. "Google maps URL: https://maps.google.com/?q=63.42,10.43".
MAPS_URL_RE = re.compile(
    rf"{LOCATION_RESULT_LOG}\s*(https://maps\.google\.com/\?q=(-?\d+\.\d+),(-?\d+\.\d+))"
)


def parse_access_point_count(line: str) -> int:
    match = AP_COUNT_RE.search(line)
    if not match:
        raise ValueError(
            f"Wi-Fi access point count not found in serial log line: {line!r}"
        )
    return int(match.group(1))


def parse_resolved_location(line: str) -> dict:
    """Extract the coordinates and Google Maps URL from the firmware log line."""
    match = MAPS_URL_RE.search(line)
    if not match:
        raise ValueError(
            f"Resolved location not found in serial log line: {line!r}"
        )
    return {
        "maps_url": match.group(1),
        "lat": float(match.group(2)),
        "lon": float(match.group(3)),
    }


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
) -> int:
    """Wait for the DUT to hand a Wi-Fi scan to nRF Cloud and return the AP count.

    The scan must appear after the *after* marker, so only location data sent by
    the current boot counts.
    """
    request_line = uart.wait_for_substring_after(
        LOCATION_REQUEST_LOG,
        after=after,
        timeout=timeout,
    )
    access_points = parse_access_point_count(request_line)
    logger.info("DUT sent %d Wi-Fi access points to nRF Cloud", access_points)
    return access_points


def wait_for_resolved_location(
    uart,
    *,
    after: str,
    timeout: float = 180.0,
    poll_interval: float = 1.0,
) -> dict:
    """Wait for nRF Cloud to resolve the Wi-Fi scan and return it to the device.

    Returns the parsed coordinates and the Google Maps URL that the firmware
    logged. Only a result reported after the *after* marker counts.
    """
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        captured = _log_after(uart.snapshot_log(), after)
        match = MAPS_URL_RE.search(captured)
        if match:
            location = parse_resolved_location(match.group(0))
            logger.info(
                "nRF Cloud resolved location: %.6f, %.6f",
                location["lat"],
                location["lon"],
            )
            logger.info("Open in Google Maps: %s", location["maps_url"])
            return location
        if LOCATION_NOT_FOUND_LOG in captured:
            raise TimeoutError(
                "nRF Cloud did not resolve any Wi-Fi scan to a position: the "
                "access points around the DUT are unknown to the location service"
            )
        time.sleep(poll_interval)

    raise TimeoutError(
        f"Timed out after {timeout:.0f}s waiting for nRF Cloud to return a "
        "resolved Wi-Fi location to the device"
    )

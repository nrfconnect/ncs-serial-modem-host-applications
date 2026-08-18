# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
"""Provisioning mode helpers for on-target hardware tests."""

from __future__ import annotations


def flash_recover_enabled(test_config: dict) -> bool:
    """Return True when baseline flash should recover (wipe) the device."""
    return test_config.get("provisioning", {}).get("mode") != "preprovisioned"


def nrf_cloud_cleanup_enabled(test_config: dict) -> bool:
    """Return True when test teardown should remove the DUT from nRF Cloud."""
    provisioning = test_config.get("provisioning", {})
    if provisioning.get("mode") == "preprovisioned":
        return False
    return provisioning.get("cleanup", True)

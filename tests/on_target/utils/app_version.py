# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
"""Zephyr application VERSION file helpers."""

from __future__ import annotations

import re
from pathlib import Path

_SEMVER = re.compile(r"^(\d+)\.(\d+)\.(\d+)$")


def write_app_version(app_dir: Path, semver: str) -> None:
    """Write a Zephyr VERSION file for one application."""
    match = _SEMVER.match(semver)
    if not match:
        raise ValueError(f"Invalid semver: {semver!r} (expected MAJOR.MINOR.PATCH)")

    major, minor, patch = match.groups()
    content = (
        f"VERSION_MAJOR = {major}\n"
        f"VERSION_MINOR = {minor}\n"
        f"PATCHLEVEL = {patch}\n"
        f"VERSION_TWEAK = 0\n"
        f"EXTRAVERSION =\n"
    )
    (app_dir / "VERSION").write_text(content, encoding="utf-8")


def memfault_software_version(semver: str) -> str:
    """Expected Memfault version string for a VERSION file with VERSION_TWEAK = 0."""
    if not _SEMVER.match(semver):
        raise ValueError(f"Invalid semver: {semver!r} (expected MAJOR.MINOR.PATCH)")
    return f"{semver}+0"


def memfault_ota_query_version(semver: str) -> str:
    """Version string the device sends in Memfault OTA URL query parameters."""
    match = _SEMVER.match(semver)
    if not match:
        raise ValueError(f"Invalid semver: {semver!r} (expected MAJOR.MINOR.PATCH)")
    major, minor, _patch = match.groups()
    return f"{major}.{minor}"

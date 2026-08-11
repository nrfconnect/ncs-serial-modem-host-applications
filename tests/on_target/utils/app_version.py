# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
"""Zephyr application VERSION file helpers."""

from __future__ import annotations

import os
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


def parse_memfault_software_version(software_version: str) -> str:
    """Convert a Memfault software version string to MAJOR.MINOR.PATCH."""
    semver = software_version.split("+", 1)[0]
    if not _SEMVER.match(semver):
        raise ValueError(
            f"Invalid Memfault software version: {software_version!r} "
            "(expected MAJOR.MINOR.PATCH[+TWEAK])"
        )
    return semver


def bump_patch(semver: str) -> str:
    """Return the semver with the patch component incremented by one."""
    match = _SEMVER.match(semver)
    if not match:
        raise ValueError(f"Invalid semver: {semver!r} (expected MAJOR.MINOR.PATCH)")

    major, minor, patch = (int(value) for value in match.groups())
    return f"{major}.{minor}.{patch + 1}"


def resolve_fota_versions(
    *,
    test_config: dict,
    prebuilt_metadata: dict[str, str] | None = None,
) -> tuple[str, str]:
    """Return baseline and update semvers for a FOTA hardware test."""
    memfault = test_config.get("memfault", {})
    firmware_version_env = os.environ.get("FIRMWARE_VERSION", "").strip()

    if firmware_version_env:
        baseline = firmware_version_env
    elif prebuilt_metadata is not None:
        baseline = parse_memfault_software_version(prebuilt_metadata["software_version"])
    else:
        baseline = memfault.get("baseline_version", "0.1.0")

    if not _SEMVER.match(baseline):
        raise ValueError(f"Invalid FOTA baseline semver: {baseline!r}")

    return baseline, bump_patch(baseline)

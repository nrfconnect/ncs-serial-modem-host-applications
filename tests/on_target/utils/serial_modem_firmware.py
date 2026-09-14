# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
"""Download and flash Serial Modem firmware for 91m1 hardware tests."""

from __future__ import annotations

import json
import os
import zipfile
from pathlib import Path
from urllib.error import HTTPError
from urllib.request import Request, urlopen, urlretrieve

import yaml

from utils.flash_tools import FULL_FLASH_PROGRAM_OPTIONS, flash_firmware_hex
from utils.helpers import REPO_ROOT
from utils.logger import get_logger

logger = get_logger()

GITHUB_API = "https://api.github.com"

# A single test flashes the modem, reads the console baud rate, and may extract
# the bundle, each of which needs the resolved release. Without this the same
# lookup runs three or four times per test and burns the API quota.
_release_cache: dict[tuple[str, str, str | None], dict] = {}


def clear_serial_modem_release_cache() -> None:
    _release_cache.clear()


def load_serial_modem_static_config(root: Path | None = None) -> dict:
    repo_root = root or REPO_ROOT
    config_path = repo_root / "tests/on_target/ci/serial_modem_firmware.yml"
    return yaml.safe_load(config_path.read_text(encoding="utf-8"))


def _github_token() -> str | None:
    for name in ("GITHUB_TOKEN", "GH_TOKEN"):
        value = os.environ.get(name, "").strip()
        if value:
            return value
    return None


def _github_api_request(url: str) -> object:
    headers = {
        "Accept": "application/vnd.github+json",
        "X-GitHub-Api-Version": "2022-11-28",
    }
    token = _github_token()
    if token:
        headers["Authorization"] = f"Bearer {token}"

    request = Request(url, headers=headers)
    try:
        with urlopen(request) as response:
            return json.load(response)
    except HTTPError as exc:
        body = exc.read().decode("utf-8", errors="replace")
        raise RuntimeError(f"GitHub API request failed ({exc.code}): {body}") from exc


def _asset_suffixes(static: dict) -> list[str]:
    suffixes = static.get("asset_suffixes")
    if suffixes:
        return list(suffixes)
    legacy = static.get("asset_suffix")
    if legacy:
        return [legacy]
    raise RuntimeError("serial_modem_firmware.yml must set asset_suffixes")


def _release_config_from_payload(
    release: dict,
    *,
    asset_suffixes: list[str],
) -> dict:
    tag = release["tag_name"]
    for suffix in asset_suffixes:
        for asset in release.get("assets", []):
            name = asset["name"]
            if name.endswith(suffix):
                return {
                    "release": tag,
                    "bundle": name,
                    "hex": f"{Path(name).stem}.hex",
                    "download_url": asset["browser_download_url"],
                    "upstream_release_url": release["html_url"],
                }

    raise RuntimeError(
        f"Serial Modem release {tag!r} has no asset ending with any of "
        f"{asset_suffixes!r}"
    )


def resolve_serial_modem_release(
    tag: str | None = None,
    *,
    root: Path | None = None,
) -> dict:
    """Resolve a Serial Modem release from GitHub.

    When *tag* is set, fetch that release. Otherwise walk upstream releases
    newest-first and return the first that ships a recognised bundle.
    """
    static = load_serial_modem_static_config(root)
    repo = static["upstream_repo"]
    asset_suffixes = _asset_suffixes(static)

    cache_key = (repo, tuple(asset_suffixes), tag)
    if cache_key in _release_cache:
        return dict(_release_cache[cache_key])

    pinned_asset_suffix = None
    if tag and tag == _pinned_release_tag(static):
        yaml_suffix = static.get("pinned_asset_suffix")
        if yaml_suffix:
            pinned_asset_suffix = str(yaml_suffix).strip() or None

    resolved = _resolve_uncached(
        repo, asset_suffixes, tag, pinned_asset_suffix=pinned_asset_suffix
    )
    _release_cache[cache_key] = resolved
    return dict(resolved)


def _resolve_uncached(
    repo: str,
    asset_suffixes: list[str],
    tag: str | None,
    *,
    pinned_asset_suffix: str | None = None,
) -> dict:
    if tag:
        release = _github_api_request(f"{GITHUB_API}/repos/{repo}/releases/tags/{tag}")
        suffixes = [pinned_asset_suffix] if pinned_asset_suffix else asset_suffixes
        return _release_config_from_payload(release, asset_suffixes=suffixes)

    releases = _github_api_request(f"{GITHUB_API}/repos/{repo}/releases")
    if not isinstance(releases, list):
        raise RuntimeError(f"Unexpected GitHub API response for {repo} releases")

    for release in releases:
        try:
            return _release_config_from_payload(release, asset_suffixes=asset_suffixes)
        except RuntimeError:
            continue

    raise RuntimeError(
        f"No Serial Modem release in {repo!r} ships an asset ending with any of "
        f"{asset_suffixes!r}"
    )


def _pinned_release_tag(static: dict) -> str | None:
    pinned = os.environ.get("SERIAL_MODEM_RELEASE", "").strip()
    if pinned:
        return pinned
    yaml_pin = static.get("pinned_release")
    if yaml_pin:
        return str(yaml_pin).strip() or None
    return None


def load_serial_modem_firmware_config(root: Path | None = None) -> dict:
    static = load_serial_modem_static_config(root)
    resolved = resolve_serial_modem_release(_pinned_release_tag(static), root=root)
    return {**static, **resolved}


def serial_modem_console_baudrate(root: Path | None = None) -> int:
    """Baud rate of the Serial Modem console (uart1), not the usual 115200."""
    config = load_serial_modem_firmware_config(root)
    return int(config["console_baudrate"])


def serial_modem_cache_dir(config: dict, *, root: Path | None = None) -> Path:
    repo_root = root or REPO_ROOT
    return repo_root / "build" / "serial-modem-firmware" / config["release"]


def download_serial_modem_bundle(*, root: Path | None = None) -> Path:
    """Download the resolved Serial Modem release zip if needed, and return its path.

    Releases ship this archive as it comes from upstream; tests extract it.
    """
    repo_root = root or REPO_ROOT
    config = load_serial_modem_firmware_config(repo_root)
    cache_dir = serial_modem_cache_dir(config, root=repo_root)
    zip_path = cache_dir / config["bundle"]

    if not zip_path.is_file():
        cache_dir.mkdir(parents=True, exist_ok=True)
        logger.info(
            "Downloading Serial Modem firmware %s (%s)",
            config["release"],
            config["download_url"],
        )
        urlretrieve(config["download_url"], zip_path)

    return zip_path


def ensure_serial_modem_firmware(*, root: Path | None = None) -> Path:
    """Download and extract the resolved Serial Modem bundle if needed."""
    repo_root = root or REPO_ROOT
    config = load_serial_modem_firmware_config(repo_root)
    cache_dir = serial_modem_cache_dir(config, root=repo_root)
    hex_path = cache_dir / config["hex"]

    if hex_path.is_file():
        return hex_path

    zip_path = download_serial_modem_bundle(root=repo_root)
    with zipfile.ZipFile(zip_path) as archive:
        archive.extractall(cache_dir)

    if not hex_path.is_file():
        raise FileNotFoundError(
            f"Serial Modem hex not found after extracting {zip_path}: {hex_path}"
        )

    return hex_path


def flash_serial_modem_firmware(segger_sn: str, *, root: Path | None = None) -> None:
    """Program the resolved Serial Modem release on the nRF9151 / SMA DK."""
    config = load_serial_modem_firmware_config(root)
    hex_path = ensure_serial_modem_firmware(root=root)
    logger.info(
        "Flashing Serial Modem firmware %s (%s) to %s",
        config["release"],
        hex_path.name,
        segger_sn,
    )
    flash_firmware_hex(
        hex_path,
        segger_sn,
        recover=True,
        program_options=FULL_FLASH_PROGRAM_OPTIONS,
    )

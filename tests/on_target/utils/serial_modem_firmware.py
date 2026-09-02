# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
"""Download and flash pinned Serial Modem firmware for 91m1 hardware tests."""

from __future__ import annotations

import zipfile
from pathlib import Path
from urllib.request import urlretrieve

import yaml

from utils.flash_tools import FULL_FLASH_PROGRAM_OPTIONS, flash_firmware_hex
from utils.helpers import REPO_ROOT
from utils.logger import get_logger

logger = get_logger()


def load_serial_modem_firmware_config(root: Path | None = None) -> dict:
    repo_root = root or REPO_ROOT
    config_path = repo_root / "tests/on_target/ci/serial_modem_firmware.yml"
    return yaml.safe_load(config_path.read_text(encoding="utf-8"))


def serial_modem_console_baudrate(root: Path | None = None) -> int:
    """Baud rate of the pinned Serial Modem console (uart1), not the usual 115200."""
    config = load_serial_modem_firmware_config(root)
    return int(config["console_baudrate"])


def serial_modem_cache_dir(config: dict, *, root: Path | None = None) -> Path:
    repo_root = root or REPO_ROOT
    return repo_root / "build" / "serial-modem-firmware" / config["release"]


def download_serial_modem_bundle(*, root: Path | None = None) -> Path:
    """Download the pinned Serial Modem release zip if needed, and return its path.

    Releases ship this archive as it comes from upstream; tests extract it.
    """
    repo_root = root or REPO_ROOT
    config = load_serial_modem_firmware_config(repo_root)
    cache_dir = serial_modem_cache_dir(config, root=repo_root)
    zip_path = cache_dir / config["bundle"]

    if not zip_path.is_file():
        cache_dir.mkdir(parents=True, exist_ok=True)
        logger.info("Downloading Serial Modem firmware %s", config["download_url"])
        urlretrieve(config["download_url"], zip_path)

    return zip_path


def ensure_serial_modem_firmware(*, root: Path | None = None) -> Path:
    """Download and extract the pinned Serial Modem bundle if needed."""
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
    """Program the pinned Serial Modem release on the nRF9151 / SMA DK."""
    hex_path = ensure_serial_modem_firmware(root=root)
    logger.info("Flashing Serial Modem firmware %s to %s", hex_path.name, segger_sn)
    flash_firmware_hex(
        hex_path,
        segger_sn,
        recover=True,
        program_options=FULL_FLASH_PROGRAM_OPTIONS,
    )

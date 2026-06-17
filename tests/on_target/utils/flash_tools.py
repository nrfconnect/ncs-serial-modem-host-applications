# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
from __future__ import annotations

import os
import subprocess
from pathlib import Path

from utils.logger import get_logger

logger = get_logger()


def _run_nrfutil(args: list[str]) -> None:
    command = ["nrfutil", "device", *args]
    logger.info("Running: %s", " ".join(command))
    try:
        result = subprocess.run(command, check=True, text=True, capture_output=True)
    except subprocess.CalledProcessError as exc:
        logger.error("nrfutil failed: %s", exc.stderr or exc.stdout)
        raise
    if result.stdout.strip():
        logger.debug(result.stdout.strip())
    if result.stderr.strip():
        logger.debug(result.stderr.strip())


def reset_device(serial: str, reset_kind: str = "RESET_SYSTEM") -> None:
    logger.info("Resetting device %s", serial)
    _run_nrfutil(["reset", "--serial-number", serial, "--reset-kind", reset_kind])


def clear_application_firmware(
    serial: str,
    internal_erase_end: int,
    erase_external_slot: bool = True,
) -> None:
    """Erase application firmware without touching PSA / tfm-storage."""
    page_range = f"0x0-0x{internal_erase_end:x}"
    logger.info(
        "Clearing application firmware on %s (pages %s, PSA storage preserved)",
        serial,
        page_range,
    )
    _run_nrfutil(["erase", "--pages", page_range, "--serial-number", serial])

    if erase_external_slot:
        logger.info("Clearing external MCUboot secondary slot on %s", serial)
        try:
            _run_nrfutil(["erase", "--all-external", "--serial-number", serial])
        except subprocess.CalledProcessError:
            logger.warning(
                "External flash erase failed; continuing (baseline flash may still succeed)"
            )

    reset_device(serial, reset_kind="RESET_PIN")


def clear_credentials_storage(
    serial: str,
    *,
    start: int = 0x175000,
    size: int = 0x8000,
) -> None:
    """Erase TF-M protected storage (TLS credentials in PSA)."""
    end = start + size - 1
    page_range = f"0x{start:x}-0x{end:x}"
    logger.info(
        "Clearing TF-M credential storage on %s (pages %s)",
        serial,
        page_range,
    )
    _run_nrfutil(["erase", "--pages", page_range, "--serial-number", serial])
    reset_device(serial, reset_kind="RESET_PIN")


def west_build(app_dir: Path, board: str) -> None:
    command = ["west", "build", "-b", board, "-p", "--"]
    logger.info("Building in %s: %s", app_dir, " ".join(command))
    subprocess.run(command, cwd=app_dir, check=True)


def west_flash(app_dir: Path, serial: str) -> None:
    command = ["west", "flash", "--no-rebuild", "--dev-id", serial]
    logger.info("Flashing from %s: %s", app_dir, " ".join(command))
    subprocess.run(command, cwd=app_dir, check=True, env=os.environ.copy())

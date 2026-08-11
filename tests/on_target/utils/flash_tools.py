# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
from __future__ import annotations

import os
import subprocess
from pathlib import Path

from utils.logger import get_logger

logger = get_logger()


def west_build(
    app_dir: Path,
    board: str,
    *,
    cmake_args: list[str] | None = None,
    pristine: bool = True,
) -> None:
    command = ["west", "build", "-b", board]
    if pristine:
        command.append("-p")
    command.append("--")

    if cmake_args:
        command.extend(cmake_args)

    logger.info("Building in %s: %s", app_dir, " ".join(command))
    subprocess.run(command, cwd=app_dir, check=True)


def elf_image_path(app_dir: Path, app_name: str) -> Path:
    return app_dir / "build" / app_name / "zephyr" / "zephyr.elf"


def signed_image_path(app_dir: Path, app_name: str) -> Path:
    return app_dir / "build" / app_name / "zephyr" / "zephyr.signed.bin"


def should_skip_build(app_dir: Path, app_name: str) -> bool:
    """Return True when CI prebuilt firmware is present and ready to flash."""
    if os.environ.get("CI_USE_PREBUILT_FIRMWARE") != "1":
        return False
    return (
        signed_image_path(app_dir, app_name).is_file()
        and elf_image_path(app_dir, app_name).is_file()
    )


def west_flash(app_dir: Path, serial: str, *, recover: bool = False) -> None:
    command = ["west", "flash", "--no-rebuild", "--dev-id", serial]
    if recover:
        command.append("--recover")
    logger.info("Flashing from %s: %s", app_dir, " ".join(command))
    subprocess.run(command, cwd=app_dir, check=True, env=os.environ.copy())


def nrfutil_reset(serial: str) -> None:
    command = ["nrfutil", "device", "reset", "--serial-number", serial]
    logger.info("Resetting device %s: %s", serial, " ".join(command))
    subprocess.run(command, check=True, env=os.environ.copy())

# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
from __future__ import annotations

import os
import subprocess
from pathlib import Path

from utils.logger import get_logger

logger = get_logger()

MERGED_HEX_PROGRAM_OPTIONS = (
    "chip_erase_mode=ERASE_NONE,"
    "ext_mem_erase_mode=ERASE_RANGES_TOUCHED_BY_FIRMWARE,"
    "verify=VERIFY_READ"
)


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


def merged_hex_path(app_dir: Path) -> Path:
    return app_dir / "build" / "merged.hex"


def should_use_prebuilt_firmware(app_dir: Path, app_name: str) -> bool:
    """Return True when CI prebuilt firmware is complete enough to flash.

    Raises when CI asked for prebuilt firmware but the artifact is incomplete,
    rather than quietly rebuilding a binary that was never released.
    """
    if os.environ.get("CI_USE_PREBUILT_FIRMWARE") != "1":
        return False

    required = (
        merged_hex_path(app_dir),
        signed_image_path(app_dir, app_name),
        elf_image_path(app_dir, app_name),
    )
    missing = [str(path) for path in required if not path.is_file()]
    if missing:
        raise RuntimeError(
            "CI_USE_PREBUILT_FIRMWARE=1 but the downloaded Build artifact is "
            f"incomplete. Missing: {', '.join(missing)}."
        )
    return True


def west_flash(app_dir: Path, serial: str, *, recover: bool = False) -> None:
    command = ["west", "flash", "--no-rebuild", "--dev-id", serial]
    if recover:
        command.append("--recover")
    logger.info("Flashing from %s: %s", app_dir, " ".join(command))
    subprocess.run(command, cwd=app_dir, check=True, env=os.environ.copy())


def flash_merged_hex(merged_hex: Path, serial: str, *, recover: bool = True) -> None:
    """Flash a sysbuild merged.hex image with nrfutil."""
    if not merged_hex.is_file():
        raise FileNotFoundError(f"merged.hex not found: {merged_hex}")

    if recover:
        recover_cmd = ["nrfutil", "device", "recover", "--serial-number", serial]
        logger.info("Recovering device %s: %s", serial, " ".join(recover_cmd))
        subprocess.run(recover_cmd, check=True, env=os.environ.copy())

    program_cmd = [
        "nrfutil",
        "device",
        "program",
        "--firmware",
        str(merged_hex),
        "--serial-number",
        serial,
        "--options",
        MERGED_HEX_PROGRAM_OPTIONS,
    ]
    logger.info("Flashing merged.hex to %s: %s", serial, " ".join(program_cmd))
    subprocess.run(program_cmd, check=True, env=os.environ.copy())
    nrfutil_reset(serial)


def flash_baseline_firmware(
    app_dir: Path,
    app_name: str,
    serial: str,
    *,
    recover: bool = True,
) -> None:
    """Flash baseline firmware, using merged.hex in CI and west flash locally."""
    if should_use_prebuilt_firmware(app_dir, app_name):
        flash_merged_hex(merged_hex_path(app_dir), serial, recover=recover)
        return

    west_flash(app_dir, serial, recover=recover)


def nrfutil_reset(serial: str) -> None:
    command = ["nrfutil", "device", "reset", "--serial-number", serial]
    logger.info("Resetting device %s: %s", serial, " ".join(command))
    subprocess.run(command, check=True, env=os.environ.copy())

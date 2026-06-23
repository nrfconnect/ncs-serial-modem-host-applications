# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
from __future__ import annotations

import os
import subprocess
from pathlib import Path

from utils.logger import get_logger

logger = get_logger()


def west_build(app_dir: Path, board: str) -> None:
    command = ["west", "build", "-b", board, "-p", "--"]
    logger.info("Building in %s: %s", app_dir, " ".join(command))
    subprocess.run(command, cwd=app_dir, check=True)


def west_flash(app_dir: Path, serial: str, *, recover: bool = False) -> None:
    command = ["west", "flash", "--no-rebuild", "--dev-id", serial]
    if recover:
        command.append("--recover")
    logger.info("Flashing from %s: %s", app_dir, " ".join(command))
    subprocess.run(command, cwd=app_dir, check=True, env=os.environ.copy())

#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
"""Write Zephyr VERSION files from a semver string (MAJOR.MINOR.PATCH)."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tests" / "on_target"))

from utils.app_version import write_app_version  # noqa: E402


def main(argv: list[str] | None = None) -> None:
    parser = argparse.ArgumentParser(description=__doc__, allow_abbrev=False)
    parser.add_argument("semver", help="Release version, e.g. 1.2.3")
    parser.add_argument(
        "applications_dir",
        type=Path,
        help="Directory containing application subfolders",
    )
    args = parser.parse_args(argv)

    apps_dir = args.applications_dir.resolve()
    if not apps_dir.is_dir():
        raise SystemExit(f"Applications directory not found: {apps_dir}")

    updated = False
    for app_dir in sorted(apps_dir.iterdir()):
        if not app_dir.is_dir():
            continue
        if not (app_dir / "CMakeLists.txt").is_file():
            continue
        write_app_version(app_dir, args.semver)
        print(f"Wrote {app_dir / 'VERSION'} ({args.semver})")
        updated = True

    if not updated:
        raise SystemExit(f"No applications found under {apps_dir}")


if __name__ == "__main__":
    main(sys.argv[1:])

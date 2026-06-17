# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
"""Read the hardware test catalog from .github/test/tests.yml."""

from __future__ import annotations

import argparse
import json
import os
import sys
from pathlib import Path

import yaml


def _catalog_path(root: Path) -> Path:
    return root / ".github/test/tests.yml"


def load_catalog(root: Path) -> list[dict]:
    catalog = yaml.safe_load(_catalog_path(root).read_text(encoding="utf-8"))
    return [test for test in catalog.get("tests", []) if test.get("enabled", True)]


def cmd_matrix(root: Path, test_filter: str) -> None:
    tests = load_catalog(root)
    if test_filter != "all":
        tests = [test for test in tests if test.get("id") == test_filter]

    payload = json.dumps(tests)
    if github_output := os.environ.get("GITHUB_OUTPUT"):
        with open(github_output, "a", encoding="utf-8") as handle:
            handle.write(f"matrix={payload}\n")
    else:
        print(payload)


def cmd_load(root: Path, test_id: str) -> None:
    for test in load_catalog(root):
        if test.get("id") == test_id:
            print(json.dumps(test))
            return
    raise SystemExit(f"test id not found: {test_id}")


def main(argv: list[str] | None = None) -> None:
    parser = argparse.ArgumentParser(description=__doc__, allow_abbrev=False)
    parser.add_argument(
        "--root",
        type=Path,
        default=Path("."),
        help="Repository root containing .github/test/tests.yml",
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    matrix_parser = subparsers.add_parser(
        "matrix",
        help="Emit enabled tests as a JSON array",
        allow_abbrev=False,
    )
    matrix_parser.add_argument(
        "--filter",
        default="all",
        help="Test id to run, or 'all' for every enabled test",
    )

    load_parser = subparsers.add_parser(
        "load",
        help="Emit a single test entry as JSON",
        allow_abbrev=False,
    )
    load_parser.add_argument("test_id", help="Test id from tests.yml")

    args = parser.parse_args(argv)
    root = args.root.resolve()

    if args.command == "matrix":
        cmd_matrix(root, args.filter)
    elif args.command == "load":
        cmd_load(root, args.test_id)


if __name__ == "__main__":
    main(sys.argv[1:])

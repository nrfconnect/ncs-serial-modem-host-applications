# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
"""Read the hardware test catalog from .github/test/tests.yml."""

from __future__ import annotations

import argparse
import json
import os
import sys
from pathlib import Path

import yaml

PROVISION_TEST_ID = "91m1_ppp-provision-nrf54l15-nrf91"
PROVISION_NRF54LM20B_TEST_ID = "91m1_ppp-provision-nrf54lm20b-nrf91"
PROVISION_LOCATION_TEST_ID = "91m1_ppp-provision-location-nrf54lm20b-nrf91"
COREDUMP_TEST_ID = "91m1_ppp-memfault-coredump-nrf54l15-nrf91"
FOTA_TEST_ID = "91m1_ppp-application-fota-nrf54l15-nrf91"
FOTA_NRF54LM20B_TEST_ID = "91m1_ppp-application-fota-nrf54lm20b-nrf91"


def _catalog_path(root: Path) -> Path:
    return root / ".github/test/tests.yml"


def _read_catalog(root: Path) -> list[dict]:
    catalog = yaml.safe_load(_catalog_path(root).read_text(encoding="utf-8"))
    return catalog.get("tests", [])


def load_catalog(root: Path) -> list[dict]:
    return [test for test in _read_catalog(root) if test.get("enabled", True)]


def load_test_entry(root: Path, test_id: str) -> dict:
    for test in _read_catalog(root):
        if test.get("id") == test_id:
            return test
    raise SystemExit(f"test id not found: {test_id}")


def _filter_tests(tests: list[dict], test_filter: str) -> list[dict]:
    if test_filter == "all":
        return tests
    return [test for test in tests if test.get("id") == test_filter]


def _write_github_output(name: str, value: str) -> None:
    if github_output := os.environ.get("GITHUB_OUTPUT"):
        with open(github_output, "a", encoding="utf-8") as handle:
            handle.write(f"{name}={value}\n")
    else:
        print(f"{name}={value}")


def cmd_matrix(root: Path, test_filter: str) -> None:
    selected = {test["id"] for test in _filter_tests(load_catalog(root), test_filter)}

    for output_name, test_id in (
        ("run_provision", PROVISION_TEST_ID),
        ("run_provision_nrf54lm20b", PROVISION_NRF54LM20B_TEST_ID),
        ("run_provision_location", PROVISION_LOCATION_TEST_ID),
        ("run_coredump", COREDUMP_TEST_ID),
        ("run_fota", FOTA_TEST_ID),
        ("run_fota_nrf54lm20b", FOTA_NRF54LM20B_TEST_ID),
    ):
        _write_github_output(output_name, "true" if test_id in selected else "false")


def cmd_load(root: Path, test_id: str) -> None:
    print(json.dumps(load_test_entry(root, test_id)))


def cmd_list(root: Path) -> None:
    for test in load_catalog(root):
        print(test["id"])


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
        help="Emit CI run flags for provision, nRF54LM20B provision, location, coredump, and FOTA tests (L15 and LM20B)",
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

    subparsers.add_parser(
        "list",
        help="Print enabled test ids, one per line",
        allow_abbrev=False,
    )

    args = parser.parse_args(argv)
    root = args.root.resolve()

    if args.command == "matrix":
        cmd_matrix(root, args.filter)
    elif args.command == "load":
        cmd_load(root, args.test_id)
    elif args.command == "list":
        cmd_list(root)


if __name__ == "__main__":
    main(sys.argv[1:])

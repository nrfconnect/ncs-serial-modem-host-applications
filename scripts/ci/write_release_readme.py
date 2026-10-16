#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
"""Write the README shipped at the top level of a release bundle."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
REPO_URL = "https://github.com/nrfconnect/ncs-serial-modem-host-applications"

# Ordered as the table should read. Entries missing from a bundle are skipped,
# which is how 93m1_at (no signed image, no DFU package) and the 93m1 boards
# (integrated modem, so no modem zip) need no special casing here.
FILE_DESCRIPTIONS = (
    ("merged.hex", "Full flash image: bootloader, secure firmware, application. Flash this."),
    ("zephyr.elf", "Debug symbols. Upload to Memfault once per release so coredumps decode."),
    ("zephyr.signed.bin", "MCUboot-signed application image for Memfault FOTA."),
    ("dfu_application.zip", "DFU package for nRF Cloud FOTA jobs."),
    (".config", "Kconfig snapshot of this build."),
)

CONSOLE_PORTS = {
    "nrf54l15": "host DK VCOM1",
    "nrf54lm20b": "host DK VCOM0",
    "nrf54lm20b-location": "host DK VCOM0",
    "nrf93m1": "nRF93M1 DK VCOM0",
}


def _modem_config(app: str) -> dict | None:
    """Resolved Serial Modem release, for the 91m1 bundles that carry it."""
    if not app.startswith("91m1"):
        return None

    sys.path.insert(0, str(ROOT / "tests/on_target"))
    from utils.serial_modem_firmware import load_serial_modem_firmware_config

    return load_serial_modem_firmware_config(ROOT)


def _modem_entry(modem: dict) -> tuple[str, str]:
    description = (
        f"Serial Modem {modem['release']} for the companion nRF9151 / SMA DK, "
        "as published upstream. Extract it and flash its .hex there first."
    )
    return modem["bundle"], description


def _file_table(bundle_dir: Path, modem: dict | None) -> list[str]:
    entries = list(FILE_DESCRIPTIONS)
    if modem is not None:
        entries.append(_modem_entry(modem))

    rows = [
        f"| `{name}` | {description} |"
        for name, description in entries
        if (bundle_dir / name).exists()
    ]
    return ["| File | What it is |", "|------|------------|", *rows]


def _program_commands(firmware: str, serial: str) -> list[str]:
    return [
        f"nrfutil device recover --serial-number {serial}",
        f"nrfutil device program --firmware {firmware} --serial-number {serial}",
        f"nrfutil device reset --serial-number {serial}",
    ]


def _flash_section(modem: dict | None) -> list[str]:
    lines = [
        "## Flashing",
        "",
        "Find each DK's serial number with `nrfutil device list`.",
        "",
    ]
    if modem is not None:
        lines += [
            "Program the nRF9151 / SMA DK first, from the extracted Serial Modem archive:",
            "",
            "```shell",
            f"unzip {modem['bundle']}",
            *_program_commands(modem["hex"], "<modem_dk_serial_number>"),
            "```",
            "",
            "Then the host DK:",
            "",
        ]
    lines += [
        "```shell",
        *_program_commands("merged.hex", "<host_dk_serial_number>"),
        "```",
        "",
        "`nrfutil device recover` erases the whole chip, including TF-M credential storage.",
        "Skip it when re-flashing a device whose credentials you want to keep.",
    ]
    return lines


def render_readme(bundle_dir: Path, *, app: str, board_type: str, version: str) -> str:
    modem = _modem_config(app)
    console = CONSOLE_PORTS.get(board_type)

    lines = [
        f"# {app} {board_type} v{version}",
        "",
        f"Pre-built firmware from {REPO_URL}, built by CI at tag `v{version}`.",
        "",
        *_file_table(bundle_dir, modem),
        "",
        *_flash_section(modem),
        "",
    ]
    if console is not None:
        lines += [f"The application console is on {console} after flashing.", ""]

    lines += [
        "## More",
        "",
        f"- Getting started: {REPO_URL}/blob/v{version}/docs/getting-started/index.md",
        f"- Application documentation: {REPO_URL}/tree/v{version}/docs/applications/{app}",
        f"- Full artifact reference: {REPO_URL}/blob/v{version}/docs/release-artifacts.md",
    ]
    if modem is not None:
        lines.append(f"- Serial Modem release: {modem['upstream_release_url']}")
    lines.append("")
    return "\n".join(lines)


def main(argv: list[str] | None = None) -> None:
    parser = argparse.ArgumentParser(description=__doc__, allow_abbrev=False)
    parser.add_argument("bundle_dir", type=Path, help="Staged release bundle directory")
    parser.add_argument("--app", required=True, help="Application name, e.g. 91m1_ppp")
    parser.add_argument("--board-type", required=True, help="Board flavor, e.g. nrf54l15")
    parser.add_argument("--version", required=True, help="Release version without v prefix")
    args = parser.parse_args(argv)

    bundle_dir = args.bundle_dir.resolve()
    if not bundle_dir.is_dir():
        raise SystemExit(f"Bundle directory not found: {bundle_dir}")

    readme = bundle_dir / "README.md"
    readme.write_text(
        render_readme(
            bundle_dir,
            app=args.app,
            board_type=args.board_type,
            version=args.version,
        ),
        encoding="utf-8",
    )
    print(f"Wrote {readme}")


if __name__ == "__main__":
    main(sys.argv[1:])

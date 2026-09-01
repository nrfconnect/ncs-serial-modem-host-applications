#!/usr/bin/env bash
# Copyright (c) 2026 Nordic Semiconductor
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#
# Build a Serial Modem image with debug logging for on-target tests.
#
# The released image logs almost nothing once running: it suspends its log
# backend after init, and even with AT#XLOG=1 the default SM_LOG_LEVEL_INF
# emits next to no runtime detail. CONFIG_SM_LOG_LEVEL_DBG is build-time only,
# hence this build. Configuration matches the pinned release's extmcu variant
# (see applications/91m1_ppp/doc/hardware-setup.md).
#
# Usage: build_serial_modem.sh [output_dir]
#
# Writes merged.hex into output_dir (default: <repo>/build/serial-modem-dbg).

set -eu

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUTPUT_DIR="${1:-$REPO_ROOT/build/serial-modem-dbg}"

BOARD=nrf9151dk/nrf9151/ns
WEST_GROUP=serial-modem
PROJECT=serial-modem

# The manifest keeps this group disabled so the module stays out of application
# builds. Enabling it writes to .west/config, which persists on a self-hosted
# runner, so always put the previous value back.
restore_group_filter() {
	if [ -n "${PREVIOUS_GROUP_FILTER:-}" ]; then
		west config manifest.group-filter -- "$PREVIOUS_GROUP_FILTER"
	else
		west config -d manifest.group-filter 2>/dev/null || true
	fi
}

PREVIOUS_GROUP_FILTER="$(west config manifest.group-filter 2>/dev/null || true)"
trap restore_group_filter EXIT

expected_revision="$(python3 - "$REPO_ROOT" <<'PY'
import sys
from pathlib import Path

import yaml

config = Path(sys.argv[1]) / "tests/on_target/ci/serial_modem_firmware.yml"
print(yaml.safe_load(config.read_text(encoding="utf-8"))["release"])
PY
)"

west config manifest.group-filter -- "+$WEST_GROUP"

manifest_revision="$(west list --all -f '{revision}' "$PROJECT")"
if [ "$manifest_revision" != "$expected_revision" ]; then
	echo "west.yml pins $PROJECT at '$manifest_revision' but" \
		"tests/on_target/ci/serial_modem_firmware.yml pins release" \
		"'$expected_revision'. Bump both to the same tag." >&2
	exit 1
fi

echo "Fetching $PROJECT $manifest_revision"
west update --narrow -o=--depth=1 "$PROJECT"

app_dir="$(west topdir)/$(west list -f '{path}' "$PROJECT")/app"
build_dir="$REPO_ROOT/build/serial-modem-dbg-build"

echo "Building Serial Modem $manifest_revision for $BOARD with SM_LOG_LEVEL_DBG"
west build -p -b "$BOARD" -d "$build_dir" "$app_dir" -- \
	-DEXTRA_CONF_FILE="overlay-ppp.conf;overlay-cmux.conf" \
	-DEXTRA_DTC_OVERLAY_FILE="overlay-external-mcu.overlay" \
	-DCONFIG_SM_LOG_LEVEL_DBG=y

mkdir -p "$OUTPUT_DIR"
cp "$build_dir/merged.hex" "$OUTPUT_DIR/merged.hex"
printf '%s\n' "$manifest_revision" > "$OUTPUT_DIR/revision.txt"
echo "Wrote $OUTPUT_DIR/merged.hex ($manifest_revision, SM_LOG_LEVEL_DBG)"

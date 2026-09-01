#!/usr/bin/env bash
# Copyright (c) 2026 Nordic Semiconductor
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#
# Build a Serial Modem image with debug logging for on-target tests.
#
# The released image logs almost nothing once running: it suspends its log
# backend after init, and the levels below are build-time only, hence this build.
# Configuration matches the pinned release's extmcu variant (see
# applications/91m1_ppp/doc/hardware-setup.md).
#
# Verbosity is per layer, and SM_LOG_LEVEL only reaches the application's own
# sm_* modules, which log on state changes and so stay silent through a steady
# transfer. The host link and CMUX carry their own levels, and those are the ones
# that narrate a stall.
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

# Serial Modem dropped the `overlay-` prefix after v2.0.0-preview2, so accept
# either spelling and say which names were tried, rather than failing later as a
# CMake error about a missing file.
resolve_overlay() {
	local name="$1" candidate
	for candidate in "overlay-$name" "$name"; do
		if [ -f "$app_dir/$candidate" ]; then
			printf '%s' "$candidate"
			return 0
		fi
	done
	echo "Found neither 'overlay-$name' nor '$name' in $app_dir." \
		"The Serial Modem overlay may have been renamed again; update this script." >&2
	return 1
}

ppp_conf="$(resolve_overlay ppp.conf)"
cmux_conf="$(resolve_overlay cmux.conf)"
extmcu_overlay="$(resolve_overlay external-mcu.overlay)"

echo "Building Serial Modem $manifest_revision for $BOARD with debug logging"
west build -p -b "$BOARD" -d "$build_dir" "$app_dir" -- \
	-DEXTRA_CONF_FILE="$ppp_conf;$cmux_conf" \
	-DEXTRA_DTC_OVERLAY_FILE="$extmcu_overlay" \
	-DCONFIG_SM_LOG_LEVEL_DBG=y \
	-DCONFIG_DTR_UART_LOG_LEVEL_DBG=y \
	-DCONFIG_MODEM_MODULES_LOG_LEVEL_DBG=y \
	-DCONFIG_LOG_BUFFER_SIZE=16384

mkdir -p "$OUTPUT_DIR"
cp "$build_dir/merged.hex" "$OUTPUT_DIR/merged.hex"
printf '%s\n' "$manifest_revision" > "$OUTPUT_DIR/revision.txt"
echo "Wrote $OUTPUT_DIR/merged.hex ($manifest_revision, DBG for sm + dtr_uart + cmux)"

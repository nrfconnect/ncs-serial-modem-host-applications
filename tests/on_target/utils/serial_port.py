# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
"""Resolve the device serial console port."""

from __future__ import annotations

import json
import os
import platform
import subprocess
from pathlib import Path

from utils.logger import get_logger

logger = get_logger()


def _match_by_serial_by_id(segger_sn: str) -> str | None:
    if platform.system() == "Darwin":
        base = Path("/dev")
        candidates = sorted(p for p in base.glob("tty.*") if segger_sn in p.name)
    else:
        base = Path("/dev/serial/by-id")
        if not base.is_dir():
            return None
        candidates = sorted(p for p in base.iterdir() if segger_sn in p.name)

    if not candidates:
        return None

    logger.info("Resolved serial port via by-id: %s", candidates[0])
    return str(candidates[0])


def _match_via_nrfutil(segger_sn: str) -> str | None:
    try:
        result = subprocess.run(
            ["nrfutil", "device", "list", "--json"],
            check=True,
            text=True,
            capture_output=True,
        )
    except (subprocess.CalledProcessError, FileNotFoundError) as exc:
        logger.warning("nrfutil device list failed: %s", exc)
        return None

    try:
        payload = json.loads(result.stdout)
    except json.JSONDecodeError:
        logger.warning("Could not parse nrfutil device list JSON")
        return None

    devices = payload.get("devices", payload)
    if not isinstance(devices, list):
        return None

    for device in devices:
        if not isinstance(device, dict):
            continue
        serial_candidates = [
            device.get("serialNumber"),
            device.get("serial_number"),
            device.get("jlinkSerialNumber"),
        ]
        if segger_sn not in {str(value) for value in serial_candidates if value}:
            continue

        for key in ("serialPorts", "serial_ports", "vcomPorts", "vcom_ports"):
            ports = device.get(key)
            if not ports:
                continue
            if isinstance(ports, list) and ports:
                first = ports[0]
                if isinstance(first, dict):
                    port = first.get("comPort") or first.get("port") or first.get("path")
                else:
                    port = first
                if port:
                    logger.info("Resolved serial port via nrfutil: %s", port)
                    return str(port)

    return None


def resolve_serial_port() -> str:
    if explicit := os.environ.get("CI_NRF54L15_SERIAL_PORT"):
        if not Path(explicit).exists():
            raise RuntimeError(f"Configured serial port does not exist: {explicit}")
        return explicit

    segger_sn = os.environ.get("CI_NRF54L15_SEGGER_SN")
    if not segger_sn:
        raise RuntimeError("CI_NRF54L15_SEGGER_SN is not set")

    for resolver in (_match_by_serial_by_id, _match_via_nrfutil):
        if port := resolver(segger_sn):
            return port

    raise RuntimeError(
        f"Could not resolve serial port for SEGGER SN {segger_sn}. "
        "Set CI_NRF54L15_SERIAL_PORT explicitly."
    )

# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
"""Resolve the device serial console port."""

from __future__ import annotations

import json
import os
import platform
import re
import subprocess
import time
from pathlib import Path

from utils.logger import get_logger

logger = get_logger()

_CONSOLE_TTY_MARKERS = ("ttyACM", "tty.usbmodem", "cu.usbmodem")

HOST_RESOLVE_TIMEOUT = 10.0
MODEM_RESOLVE_TIMEOUT = 5.0

# Serial Modem logs to uart1 on the nRF9151 / SMA DK, exposed as VCOM1.
MODEM_CONSOLE_VCOM = 1


def _section_vars(
    section: dict,
    *,
    segger_default: str,
    serial_port_default: str,
    console_vcom_default: int | None = None,
) -> tuple[str, str, int | None]:
    segger_var = section.get("segger_sn_var", segger_default)
    serial_port_var = section.get("serial_port_var", serial_port_default)
    console_vcom = section.get("console_vcom", console_vcom_default)
    if console_vcom is not None:
        console_vcom = int(console_vcom)
    return segger_var, serial_port_var, console_vcom


def _hardware_vars(test_config: dict) -> tuple[str, str, int | None]:
    return _section_vars(
        test_config.get("hardware", {}),
        segger_default="CI_NRF54L15_SEGGER_SN",
        serial_port_default="CI_NRF54L15_SERIAL_PORT",
    )


def _normalize_serial_number(serial_number: str) -> str:
    serial_number = str(serial_number).strip()
    if serial_number.isdigit():
        return str(int(serial_number))
    return serial_number


def _serial_numbers_match(left: str, right: str) -> bool:
    return _normalize_serial_number(left) == _normalize_serial_number(right)


def _by_id_interface_index(path: str) -> int:
    match = re.search(r"-if(\d+)$", Path(path).name)
    return int(match.group(1)) if match else 0


def _port_path(port: dict | str) -> str | None:
    if isinstance(port, str):
        return port
    if not isinstance(port, dict):
        return None
    for key in ("path", "comPort", "port", "comName"):
        if value := port.get(key):
            return str(value)
    return None


def _select_console_port(ports: list, console_vcom: int | None) -> str | None:
    typed_ports = [port for port in ports if isinstance(port, dict)]
    if not typed_ports:
        first = _port_path(ports[0])
        return first

    if console_vcom is not None:
        for port in typed_ports:
            if port.get("vcom") == console_vcom:
                return _port_path(port)
        if len(typed_ports) == 1:
            return _port_path(typed_ports[0])

    if len(typed_ports) == 1:
        return _port_path(typed_ports[0])

    # No port advertised the requested vcom, so the choice below is a guess. Log
    # what was on offer; picking the wrong VCOM yields a silent, empty capture.
    logger.warning(
        "No port reported vcom=%s; choosing by highest vcom among %s",
        console_vcom,
        [{"path": _port_path(port), "vcom": port.get("vcom")} for port in typed_ports],
    )

    # VCOM0 is often routed to uart30 on CI DUTs with the modem link; prefer the
    # highest-index VCOM when multiple ports are exposed.
    best = max(typed_ports, key=lambda port: port.get("vcom", -1))
    return _port_path(best)


def _parse_nrfutil_devices(stdout: str) -> list[dict]:
    devices: list[dict] = []
    for line in stdout.splitlines():
        line = line.strip()
        if not line:
            continue
        try:
            payload = json.loads(line)
        except json.JSONDecodeError:
            continue

        if isinstance(payload.get("devices"), list):
            devices = payload["devices"]
            continue

        data = payload.get("data")
        if not isinstance(data, dict):
            continue

        if isinstance(data.get("devices"), list):
            devices = data["devices"]
            continue

        if data.get("type") == "devices" and isinstance(data.get("data"), dict):
            inner = data["data"].get("devices")
            if isinstance(inner, list):
                devices = inner

    if devices:
        return devices

    try:
        payload = json.loads(stdout)
    except json.JSONDecodeError:
        return devices

    if isinstance(payload.get("devices"), list):
        return payload["devices"]

    data = payload.get("data")
    if isinstance(data, dict) and isinstance(data.get("devices"), list):
        return data["devices"]

    return devices


def _match_by_serial_by_id(segger_sn: str, console_vcom: int | None) -> str | None:
    padded_sn = segger_sn.zfill(12)
    if platform.system() == "Darwin":
        base = Path("/dev")
        candidates = sorted(
            p for p in base.glob("tty.*") if segger_sn in p.name or padded_sn in p.name
        )
    else:
        base = Path("/dev/serial/by-id")
        if not base.is_dir():
            return None
        candidates = sorted(
            p for p in base.iterdir() if segger_sn in p.name or padded_sn in p.name
        )

    if not candidates:
        return None

    console_candidates: list[str] = []
    for candidate in candidates:
        try:
            target = os.path.realpath(candidate)
        except OSError:
            continue
        if any(marker in target for marker in _CONSOLE_TTY_MARKERS):
            console_candidates.append(str(candidate))

    if not console_candidates:
        logger.warning(
            "Found %d by-id paths for SEGGER SN %s, but none resolve to a console TTY",
            len(candidates),
            segger_sn,
        )
        return None

    console_candidates.sort(key=_by_id_interface_index)
    if len(console_candidates) > 1:
        logger.info(
            "Found %d console TTY paths for SEGGER SN %s: %s",
            len(console_candidates),
            segger_sn,
            ", ".join(console_candidates),
        )

    chosen = _select_by_id_port(console_candidates, console_vcom)
    logger.info("Resolved serial port via by-id: %s", chosen)
    return chosen


def _select_by_id_port(console_candidates: list[str], console_vcom: int | None) -> str:
    if console_vcom is not None and len(console_candidates) > 1:
        index = min(console_vcom, len(console_candidates) - 1)
        return console_candidates[index]

    if len(console_candidates) > 1:
        return console_candidates[-1]

    return console_candidates[0]


def _match_via_nrfutil(segger_sn: str, console_vcom: int | None) -> str | None:
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

    devices = _parse_nrfutil_devices(result.stdout)
    if not devices:
        logger.warning("Could not parse nrfutil device list output")
        return None

    matched_device = False
    for device in devices:
        if not isinstance(device, dict):
            continue
        serial_candidates = [
            device.get("serialNumber"),
            device.get("serial_number"),
            device.get("jlinkSerialNumber"),
        ]
        if not any(
            _serial_numbers_match(str(value), segger_sn)
            for value in serial_candidates
            if value
        ):
            continue

        matched_device = True
        for key in ("serialPorts", "serial_ports", "vcomPorts", "vcom_ports"):
            ports = device.get(key)
            if not isinstance(ports, list) or not ports:
                continue
            if port := _select_console_port(ports, console_vcom):
                logger.info("Resolved serial port via nrfutil: %s", port)
                return port

        logger.warning(
            "nrfutil found SEGGER SN %s but reported no serial ports (traits=%s)",
            segger_sn,
            device.get("traits"),
        )

    if not matched_device:
        logger.warning("nrfutil device list did not include SEGGER SN %s", segger_sn)

    return None


def _discover_serial_port(
    segger_sn: str,
    console_vcom: int | None,
    *,
    timeout: float,
) -> str | None:
    deadline = time.monotonic() + timeout
    while True:
        for resolver in (
            lambda: _match_via_nrfutil(segger_sn, console_vcom),
            lambda: _match_by_serial_by_id(segger_sn, console_vcom),
        ):
            if port := resolver():
                return port

        if time.monotonic() >= deadline:
            return None
        time.sleep(1.0)


def resolve_serial_port(test_config: dict) -> str:
    segger_var, serial_port_var, console_vcom = _hardware_vars(test_config)

    if explicit := os.environ.get(serial_port_var):
        if not Path(explicit).exists():
            raise RuntimeError(f"Configured serial port does not exist: {explicit}")
        return explicit

    try:
        segger_sn = os.environ[segger_var]
    except KeyError as exc:
        raise RuntimeError(f"{segger_var} is not set") from exc

    port = _discover_serial_port(segger_sn, console_vcom, timeout=HOST_RESOLVE_TIMEOUT)
    if port is not None:
        return port

    raise RuntimeError(
        f"Could not resolve serial port for SEGGER SN {segger_sn}. "
        f"Set {serial_port_var} explicitly."
    )


def resolve_modem_serial_port(test_config: dict) -> str | None:
    """Resolve the Serial Modem console port, or None if it cannot be found.

    Modem logs are diagnostic only, so every failure path warns and returns None
    instead of failing a test that would otherwise pass.
    """
    serial_modem = test_config.get("serial_modem")
    if not serial_modem:
        return None

    segger_var, serial_port_var, console_vcom = _section_vars(
        serial_modem,
        segger_default="CI_NRF54L15_SERIAL_MODEM_SEGGER_SN",
        serial_port_default="CI_NRF54L15_SERIAL_MODEM_SERIAL_PORT",
        console_vcom_default=MODEM_CONSOLE_VCOM,
    )

    if explicit := os.environ.get(serial_port_var):
        if Path(explicit).exists():
            return explicit
        logger.warning(
            "Configured Serial Modem console port does not exist: %s; "
            "skipping modem log capture",
            explicit,
        )
        return None

    segger_sn = os.environ.get(segger_var)
    if not segger_sn:
        logger.warning("%s is not set; skipping Serial Modem log capture", segger_var)
        return None

    port = _discover_serial_port(segger_sn, console_vcom, timeout=MODEM_RESOLVE_TIMEOUT)
    if port is None:
        logger.warning(
            "Could not resolve Serial Modem console port for SEGGER SN %s; skipping "
            "modem log capture. Enable VCOM1 on the nRF9151 / SMA DK or set %s.",
            segger_sn,
            serial_port_var,
        )
    return port

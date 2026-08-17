# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
"""Memfault REST/CLI helpers for on-target hardware tests."""

from __future__ import annotations

import json
import os
import re
import subprocess
import time
import urllib.error
import urllib.parse
import urllib.request
from datetime import datetime
from pathlib import Path

from utils.app_version import memfault_software_versions_match
from utils.helpers import assert_dut_device_id
from utils.logger import get_logger

logger = get_logger()

API_HOST = "https://api.memfault.com/api/v0"
CONFIG_RE = re.compile(r'^CONFIG_(?P<key>[A-Z0-9_]+)="(?P<value>.*)"$')


def _read_kconfig_values(config_path: Path, keys: set[str]) -> dict[str, str]:
    if not config_path.is_file():
        raise FileNotFoundError(f"Build configuration not found: {config_path}")

    values: dict[str, str] = {}
    for line in config_path.read_text(encoding="utf-8").splitlines():
        match = CONFIG_RE.match(line.strip())
        if not match or match.group("key") not in keys:
            continue
        values[match.group("key")] = match.group("value")
        if len(values) == len(keys):
            break

    missing = keys - values.keys()
    if missing:
        raise RuntimeError(
            f"Missing Kconfig values in {config_path}: {', '.join(sorted(missing))}"
        )
    return values


def read_build_metadata(app_dir: Path, app_name: str) -> dict[str, str]:
    """Return Memfault OTA metadata from a completed west build."""
    config_path = app_dir / "build" / app_name / "zephyr" / ".config"
    values = _read_kconfig_values(
        config_path,
        {"BOARD", "MEMFAULT_NCS_FW_TYPE", "MEMFAULT_NCS_HW_VERSION", "MEMFAULT_NCS_FW_VERSION"},
    )
    hardware_version = values.get("MEMFAULT_NCS_HW_VERSION") or values["BOARD"]
    return {
        "hardware_version": hardware_version,
        "software_type": values["MEMFAULT_NCS_FW_TYPE"],
        "software_version": values["MEMFAULT_NCS_FW_VERSION"],
        "board": values["BOARD"],
    }


def _load_memfault_credentials(test_config: dict) -> dict[str, str | int]:
    memfault = test_config.get("memfault", {})
    org_token_var = memfault.get("org_token_var", "MEMFAULT_ORG_TOKEN")
    org_var = memfault.get("org_var", "MEMFAULT_ORG")
    project_var = memfault.get("project_var", "MEMFAULT_PROJECT")
    fleet_sampling_var = memfault.get(
        "fleet_sampling_configuration_var",
        "MEMFAULT_FLEET_SAMPLING_CONFIGURATION",
    )

    try:
        org_token = os.environ[org_token_var]
        org = os.environ[org_var]
        project = os.environ[project_var]
    except KeyError as exc:
        raise RuntimeError(
            f"Memfault CI variables must be set ({org_token_var}, {org_var}, {project_var})"
        ) from exc

    credentials: dict[str, str | int] = {
        "org_token": org_token,
        "org": org,
        "project": project,
    }
    fleet_sampling_raw = memfault.get("fleet_sampling_configuration") or os.environ.get(
        fleet_sampling_var
    )
    if fleet_sampling_raw is not None:
        credentials["fleet_sampling_configuration"] = int(fleet_sampling_raw)
    return credentials


def load_memfault_credentials(test_config: dict) -> dict[str, str | int]:
    """Return Memfault org/project credentials for device cleanup and developer mode."""
    return _load_memfault_credentials(test_config)


def load_memfault_env(test_config: dict) -> dict[str, str]:
    memfault = test_config.get("memfault", {})
    cohort_var = memfault.get("cohort_var", "MEMFAULT_COHORT")

    env = _load_memfault_credentials(test_config)
    cohort = memfault.get("cohort") or os.environ.get(cohort_var) or "default"
    if cohort == "default":
        raise RuntimeError(
            "Hardware tests must configure a dedicated Memfault cohort in tests.yml "
            f"(or set {cohort_var}) so CI cannot affect production devices"
        )

    return {
        **env,
        "cohort": cohort,
        "cohort_name": memfault.get("cohort_name", cohort),
    }


def _memfault_cli_base(env: dict[str, str]) -> list[str]:
    return [
        "memfault",
        "--org-token",
        env["org_token"],
        "--org",
        env["org"],
        "--project",
        env["project"],
    ]


def upload_ota_payload(
    *,
    env: dict[str, str],
    binary: Path,
    metadata: dict[str, str],
    software_version: str,
) -> None:
    if not binary.is_file():
        raise FileNotFoundError(f"OTA binary not found: {binary}")

    command = [
        *_memfault_cli_base(env),
        "upload-ota-payload",
        "--hardware-version",
        metadata["hardware_version"],
        "--software-type",
        metadata["software_type"],
        "--software-version",
        software_version,
        str(binary),
    ]
    logger.info(
        "Uploading OTA payload %s (hw=%s, type=%s, version=%s)",
        binary,
        metadata["hardware_version"],
        metadata["software_type"],
        software_version,
    )
    subprocess.run(command, check=True)


def _cohort_url(env: dict[str, str], cohort: str | None = None) -> str:
    slug = cohort or env["cohort"]
    return (
        f"{API_HOST}/organizations/{env['org']}/projects/{env['project']}"
        f"/cohorts/{slug}"
    )


def upload_mcu_symbols(
    *,
    env: dict[str, str],
    elf: Path,
    metadata: dict[str, str],
    software_version: str,
) -> None:
    if not elf.is_file():
        raise FileNotFoundError(f"MCU symbols ELF not found: {elf}")

    command = [
        *_memfault_cli_base(env),
        "upload-mcu-symbols",
        "--software-type",
        metadata["software_type"],
        "--software-version",
        software_version,
        str(elf),
    ]
    logger.info(
        "Uploading MCU symbols %s (type=%s, version=%s)",
        elf,
        metadata["software_type"],
        software_version,
    )
    subprocess.run(command, check=True)


def deploy_release(*, env: dict[str, str], software_version: str) -> None:
    active_version = _get_active_cohort_release_version(env, software_version)
    if active_version == software_version:
        logger.info(
            "Memfault release %s is already active in cohort %s",
            software_version,
            env["cohort"],
        )
        return

    command = [
        *_memfault_cli_base(env),
        "deploy-release",
        "--release-version",
        software_version,
        "--cohort",
        env["cohort"],
    ]
    logger.info(
        "Deploying Memfault release %s to cohort %s",
        software_version,
        env["cohort"],
    )
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode == 0:
        return

    combined_output = f"{result.stdout}\n{result.stderr}"
    if "already active" in combined_output:
        logger.info(
            "Memfault release %s is already active in cohort %s",
            software_version,
            env["cohort"],
        )
        return

    raise subprocess.CalledProcessError(
        result.returncode,
        command,
        output=result.stdout,
        stderr=result.stderr,
    )


def deactivate_release(*, env: dict[str, str], software_version: str) -> None:
    command = [
        *_memfault_cli_base(env),
        "deploy-release",
        "--release-version",
        software_version,
        "--cohort",
        env["cohort"],
        "--deactivate",
    ]
    logger.info(
        "Deactivating Memfault release %s on cohort %s",
        software_version,
        env["cohort"],
    )
    subprocess.run(command, check=True)


def _auth_headers(env: dict[str, str]) -> dict[str, str]:
    return {
        "Authorization": f"Bearer {env['org_token']}",
        "Content-Type": "application/json",
    }


def _cohorts_url(env: dict[str, str]) -> str:
    return (
        f"{API_HOST}/organizations/{env['org']}/projects/{env['project']}/cohorts"
    )


def _deployments_url(env: dict[str, str]) -> str:
    return (
        f"{API_HOST}/organizations/{env['org']}/projects/{env['project']}/deployments"
    )


def _resource_from_payload(payload: dict, *resource_keys: str) -> dict:
    data = payload.get("data")
    if isinstance(data, dict):
        for key in resource_keys:
            nested = data.get(key)
            if isinstance(nested, dict):
                return nested
        return data
    return payload if isinstance(payload, dict) else {}


def _iter_deployments(payload: dict) -> list[dict]:
    data = payload.get("data")
    if isinstance(data, list):
        return [item for item in data if isinstance(item, dict)]

    resource = _resource_from_payload(payload, "cohort")
    deployments = resource.get("deployments")
    if isinstance(deployments, list):
        return [item for item in deployments if isinstance(item, dict)]
    return []


def _deployment_release_version(deployment: dict) -> str | None:
    release = deployment.get("release")
    if isinstance(release, dict):
        version = release.get("version")
        if isinstance(version, str):
            return version
    return None


def _is_active_deployment(deployment: dict) -> bool:
    return deployment.get("status") not in {"pulled", "aborted"}


def _active_release_from_deployments(deployments: list[dict]) -> str | None:
    for deployment in reversed(deployments):
        if not _is_active_deployment(deployment):
            continue
        version = _deployment_release_version(deployment)
        if version is not None:
            return version
    return None


def _list_cohort_deployments(env: dict[str, str], software_version: str) -> list[dict]:
    query = urllib.parse.urlencode(
        {
            "cohort": env["cohort"],
            "release": software_version,
            "sort": "-deployed_date",
            "per_page": "10",
        }
    )
    _, payload = _memfault_json_request(env, "GET", f"{_deployments_url(env)}?{query}")
    if payload is None:
        return []
    return _iter_deployments(payload)


def _get_active_cohort_release_version(
    env: dict[str, str],
    software_version: str,
) -> str | None:
    _, cohort_payload = _memfault_json_request(env, "GET", _cohort_url(env))
    if cohort_payload is not None:
        version = _active_release_from_deployments(_iter_deployments(cohort_payload))
        if version is not None:
            return version

    for deployment in _list_cohort_deployments(env, software_version):
        if not _is_active_deployment(deployment):
            continue
        version = _deployment_release_version(deployment)
        if version == software_version:
            return version
    return None


def wait_for_cohort_release_deployed(
    env: dict[str, str],
    software_version: str,
    *,
    timeout: float = 120.0,
    poll_interval: float = 5.0,
) -> str:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        active_version = _get_active_cohort_release_version(env, software_version)
        logger.info(
            "Memfault cohort %r active release=%r (waiting for %r)",
            env["cohort"],
            active_version,
            software_version,
        )
        if active_version == software_version:
            return active_version
        time.sleep(poll_interval)
    raise TimeoutError(
        f"Timed out after {timeout:.0f}s waiting for Memfault cohort {env['cohort']!r} "
        f"to activate release {software_version!r}"
    )


def set_device_release_override(
    env: dict[str, str],
    device_id: str,
    software_version: str,
) -> None:
    logger.info(
        "Setting Memfault release override %r on device %s",
        software_version,
        device_id,
    )
    _memfault_json_request(
        env,
        "PATCH",
        _device_url(env, device_id),
        body={"release": software_version},
    )


def clear_device_release_override(env: dict[str, str], device_id: str) -> None:
    logger.info("Clearing Memfault release override on device %s", device_id)
    _memfault_json_request(
        env,
        "PATCH",
        _device_url(env, device_id),
        body={"release": None},
        allowed_statuses={200, 400, 404},
    )


def _devices_url(env: dict[str, str]) -> str:
    return (
        f"{API_HOST}/organizations/{env['org']}/projects/{env['project']}/devices"
    )


def _device_url(env: dict[str, str], device_id: str) -> str:
    return (
        f"{API_HOST}/organizations/{env['org']}/projects/{env['project']}"
        f"/devices/{device_id}"
    )


def _device_from_payload(payload: dict) -> dict:
    data = payload.get("data")
    if isinstance(data, dict):
        device = data.get("device")
        if isinstance(device, dict):
            return device
        return data
    device = payload.get("device")
    if isinstance(device, dict):
        return device
    return payload


def _device_cohort_slug(device: dict) -> str | None:
    cohort = device.get("cohort")
    if isinstance(cohort, dict):
        slug = cohort.get("slug")
        if isinstance(slug, str):
            return slug
    return None


def _memfault_json_request(
    env: dict[str, str],
    method: str,
    url: str,
    *,
    body: dict | None = None,
    allowed_statuses: set[int] | None = None,
) -> tuple[int, dict | None]:
    data = None
    if body is not None:
        data = json.dumps(body).encode("utf-8")

    request = urllib.request.Request(
        url,
        data=data,
        method=method,
        headers=_auth_headers(env),
    )
    try:
        with urllib.request.urlopen(request) as response:
            raw = response.read().decode("utf-8")
            payload = json.loads(raw) if raw else None
            return response.status, payload
    except urllib.error.HTTPError as exc:
        if allowed_statuses and exc.code in allowed_statuses:
            raw = exc.read().decode("utf-8")
            payload = json.loads(raw) if raw else None
            return exc.code, payload
        raise RuntimeError(
            f"Memfault API {method} {url} failed with HTTP {exc.code}: "
            f"{exc.read().decode('utf-8')}"
        ) from exc


def ensure_cohort_exists(env: dict[str, str]) -> None:
    status, _ = _memfault_json_request(
        env,
        "POST",
        _cohorts_url(env),
        body={"name": env["cohort_name"], "slug": env["cohort"]},
        allowed_statuses={200, 409},
    )
    if status not in {200, 409}:
        raise RuntimeError(
            f"Unexpected status {status} ensuring Memfault cohort {env['cohort']!r}"
        )
    logger.info(
        "Memfault cohort %r is ready (name=%r)",
        env["cohort"],
        env["cohort_name"],
    )


def _device_server_side_developer_mode_enabled(device: dict) -> bool:
    return device.get("server_side_developer_mode") is True


def _fleet_sampling_configuration_id(device: dict) -> int | None:
    value = device.get("fleet_sampling_configuration")
    if isinstance(value, int):
        return value
    if isinstance(value, dict):
        config_id = value.get("id")
        if isinstance(config_id, int):
            return config_id
    return None


def _server_side_developer_mode_patch_body(
    env: dict[str, str | int],
    device: dict,
) -> dict[str, bool | int]:
    body: dict[str, bool | int] = {"server_side_developer_mode": True}
    fleet_sampling = env.get("fleet_sampling_configuration")
    if not isinstance(fleet_sampling, int):
        fleet_sampling = _fleet_sampling_configuration_id(device)
    if isinstance(fleet_sampling, int):
        body["fleet_sampling_configuration"] = fleet_sampling
    return body


def wait_for_device_registered(
    env: dict[str, str],
    device_id: str,
    *,
    timeout: float = 120.0,
    poll_interval: float = 5.0,
) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        status, _ = _memfault_json_request(
            env,
            "GET",
            _device_url(env, device_id),
            allowed_statuses={200, 404},
        )
        if status == 200:
            logger.info("Memfault device %s is registered", device_id)
            return
        time.sleep(poll_interval)
    raise TimeoutError(
        f"Timed out after {timeout:.0f}s waiting for Memfault device {device_id} to register"
    )


def enable_server_side_developer_mode(
    env: dict[str, str | int],
    device_id: str,
    *,
    wait_for_registration_timeout: float = 120.0,
) -> None:
    """Enable Memfault server-side developer mode to bypass device rate limits."""
    wait_for_device_registered(
        env,
        device_id,
        timeout=wait_for_registration_timeout,
    )

    status, payload = _memfault_json_request(
        env,
        "GET",
        _device_url(env, device_id),
        allowed_statuses={200},
    )
    if payload is None:
        raise RuntimeError(f"Memfault device lookup for {device_id} returned no payload")

    device = _device_from_payload(payload)
    if _device_server_side_developer_mode_enabled(device):
        logger.info(
            "Memfault device %s is already in server-side developer mode",
            device_id,
        )
        return

    body = _server_side_developer_mode_patch_body(env, device)
    logger.info(
        "Enabling Memfault server-side developer mode on device %s (payload=%r)",
        device_id,
        body,
    )
    status, payload = _memfault_json_request(
        env,
        "PATCH",
        _device_url(env, device_id),
        body=body,
    )
    if status != 200:
        raise RuntimeError(
            f"Failed to enable Memfault server-side developer mode on device {device_id}: "
            f"HTTP {status}"
        )

    if payload is not None:
        device = _device_from_payload(payload)
        if _device_server_side_developer_mode_enabled(device):
            logger.info(
                "Memfault device %s confirmed in server-side developer mode",
                device_id,
            )
            return

    logger.info(
        "Memfault server-side developer mode enabled on device %s",
        device_id,
    )


def delete_device_if_exists(
    env: dict[str, str],
    device_id: str,
    expected_device_id: str,
) -> None:
    validated = assert_dut_device_id(device_id, expected_device_id)
    status, _ = _memfault_json_request(
        env,
        "GET",
        _device_url(env, validated),
        allowed_statuses={200, 404},
    )
    if status == 404:
        logger.info("DUT %s is not registered in Memfault; nothing to delete", validated)
        return

    logger.info("Deleting only the configured DUT %s from Memfault", validated)
    status, _ = _memfault_json_request(
        env,
        "DELETE",
        _device_url(env, validated),
        allowed_statuses={204, 404},
    )
    if status not in {204, 404}:
        raise RuntimeError(f"Failed to delete Memfault device {validated}: HTTP {status}")
    logger.info("DUT %s deleted from Memfault", validated)


def ensure_device_in_cohort(
    env: dict[str, str],
    device_id: str,
    *,
    hardware_version: str,
) -> None:
    status, _ = _memfault_json_request(
        env,
        "GET",
        _device_url(env, device_id),
        allowed_statuses={200, 404},
    )
    if status == 404:
        logger.info(
            "Creating Memfault device %s before assigning cohort %r",
            device_id,
            env["cohort"],
        )
        create_status, _ = _memfault_json_request(
            env,
            "POST",
            _devices_url(env),
            body={
                "device_serial": device_id,
                "hardware_version": hardware_version,
            },
            allowed_statuses={200, 409},
        )
        if create_status not in {200, 409}:
            raise RuntimeError(
                f"Unexpected status {create_status} creating Memfault device {device_id}"
            )

    _, payload = _memfault_json_request(env, "GET", _device_url(env, device_id))
    if payload is None:
        raise RuntimeError(f"Memfault device lookup for {device_id} returned no payload")

    device = _device_from_payload(payload)
    current_cohort = _device_cohort_slug(device)
    if current_cohort == env["cohort"]:
        logger.info(
            "Memfault device %s is already in cohort %r",
            device_id,
            env["cohort"],
        )
        enable_server_side_developer_mode(env, device_id)
        return

    logger.info(
        "Moving Memfault device %s from cohort %r to %r",
        device_id,
        current_cohort,
        env["cohort"],
    )
    _memfault_json_request(
        env,
        "PATCH",
        _device_url(env, device_id),
        body={"cohort": env["cohort"]},
    )
    enable_server_side_developer_mode(env, device_id)


def _extract_software_version(payload: dict) -> str | None:
    device = _device_from_payload(payload)
    for key in (
        "last_seen_software_version",
        "software_version",
        "softwareVersion",
        "current_version",
    ):
        value = device.get(key)
        if isinstance(value, dict):
            version = value.get("version")
            if isinstance(version, str):
                return version
        elif isinstance(value, str):
            return value
    return None


def get_device_payload(env: dict[str, str], device_id: str) -> dict | None:
    status, payload = _memfault_json_request(
        env,
        "GET",
        _device_url(env, device_id),
        allowed_statuses={200, 404},
    )
    if status == 404 or payload is None:
        return None
    return payload


def get_device_software_version(env: dict[str, str], device_id: str) -> str | None:
    payload = get_device_payload(env, device_id)
    if payload is None:
        return None

    return _extract_software_version(payload)


def wait_for_device_version(
    env: dict[str, str],
    device_id: str,
    expected_version: str,
    *,
    timeout: float = 120.0,
    poll_interval: float = 5.0,
) -> str:
    deadline = time.monotonic() + timeout
    last_version: str | None = None
    while time.monotonic() < deadline:
        version = get_device_software_version(env, device_id)
        last_version = version
        logger.info(
            "Memfault device %s reported software_version=%r (waiting for %r)",
            device_id,
            version,
            expected_version,
        )
        if memfault_software_versions_match(expected_version, version):
            return version or expected_version
        time.sleep(poll_interval)

    payload = get_device_payload(env, device_id)
    if payload is not None:
        logger.error(
            "Memfault device %s final API payload while waiting for software_version %r: %s",
            device_id,
            expected_version,
            json.dumps(payload, indent=2, sort_keys=True),
        )
    raise TimeoutError(
        f"Timed out after {timeout:.0f}s waiting for Memfault device {device_id} "
        f"to report software_version {expected_version!r} (last seen: {last_version!r})"
    )


def _issues_url(env: dict[str, str]) -> str:
    return (
        f"{API_HOST}/organizations/{env['org']}/projects/{env['project']}/issues"
    )


def _iter_issues(payload: dict | None) -> list[dict]:
    if payload is None:
        return []

    data = payload.get("data")
    if isinstance(data, list):
        return [item for item in data if isinstance(item, dict)]

    if isinstance(data, dict):
        return [data]

    return []


def _trace_from_issue(issue: dict) -> dict | None:
    last_trace = issue.get("last_trace")
    if isinstance(last_trace, dict):
        return last_trace
    return None


def _trace_device_serial(trace: dict) -> str | None:
    device = trace.get("device")
    if isinstance(device, dict):
        serial = device.get("device_serial")
        if isinstance(serial, str):
            return serial.upper()
    return None


def _trace_reason(trace: dict) -> str | None:
    reason = trace.get("reason")
    if isinstance(reason, str):
        return reason
    return None


def _normalize_reason(reason: str) -> str:
    """Fold reason spelling variants, e.g. 'Bus Fault' and 'BusFault'."""
    return "".join(char for char in reason.lower() if char.isalnum())


def wait_for_device_crash_trace(
    env: dict[str, str],
    device_id: str,
    *,
    reason: str = "BusFault",
    since: datetime,
    timeout: float = 180.0,
    poll_interval: float = 5.0,
) -> dict:
    """Poll Memfault Issues until a trace for *device_id* with *reason* appears."""
    expected_device_id = device_id.strip().upper()
    expected_reason = _normalize_reason(reason)
    since_param = since.astimezone().isoformat()

    deadline = time.monotonic() + timeout
    last_issue_count = 0
    while time.monotonic() < deadline:
        query = urllib.parse.urlencode(
            {
                "last_seen_since": since_param,
                "per_page": "25",
                "sort": "-last_seen",
            }
        )
        _, payload = _memfault_json_request(
            env,
            "GET",
            f"{_issues_url(env)}?{query}",
            allowed_statuses={200},
        )
        issues = _iter_issues(payload)
        last_issue_count = len(issues)
        logger.info(
            "Memfault returned %d issue(s) since %s while looking for reason %r",
            last_issue_count,
            since_param,
            reason,
        )

        for issue in issues:
            trace = _trace_from_issue(issue)
            if trace is None:
                continue

            trace_serial = _trace_device_serial(trace)
            trace_reason = _trace_reason(trace)
            logger.info(
                "Memfault issue %r last_trace device=%r reason=%r",
                issue.get("title"),
                trace_serial,
                trace_reason,
            )
            if trace_serial != expected_device_id:
                continue
            if trace_reason is None or _normalize_reason(trace_reason) != expected_reason:
                continue

            logger.info(
                "Memfault confirmed %r trace for device %s",
                reason,
                expected_device_id,
            )
            return trace

        time.sleep(poll_interval)

    logger.error(
        "Memfault Issues API returned %d matching issue(s) before timeout; "
        "expected device %s reason %r since %s",
        last_issue_count,
        expected_device_id,
        reason,
        since_param,
    )
    raise TimeoutError(
        f"Timed out after {timeout:.0f}s waiting for Memfault {reason!r} trace "
        f"from device {expected_device_id}"
    )

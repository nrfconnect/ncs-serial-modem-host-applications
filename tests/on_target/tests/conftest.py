# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause

from __future__ import annotations

import os
import tempfile
import time
import types
from pathlib import Path

import pytest

from utils.app_version import (
    memfault_software_version,
    resolve_baseline_version,
    resolve_fota_versions,
    write_app_version,
)
from utils.serial_modem_firmware import (
    flash_serial_modem_firmware,
    serial_modem_console_baudrate,
)
from utils.flash_tools import (
    flash_baseline_firmware,
    nrfutil_reset,
    should_use_prebuilt_firmware,
    west_build,
)
from utils.dut_lifecycle import CloudDutSession
from utils.memfault_ota import read_build_metadata
from utils.helpers import MODEM_SERIAL_LOG, REPO_ROOT, SERIAL_LOG, load_test_config
from utils.logger import get_logger
from utils.provisioning_config import flash_recover_enabled, nrf_cloud_cleanup_enabled
from utils.serial_port import resolve_modem_serial_port, resolve_serial_port
from utils.uart import Uart

logger = get_logger()


def _hardware_context(test_config: dict) -> tuple[str, str, Path]:
    hardware = test_config.get("hardware", {})
    segger_var = hardware.get("segger_sn_var", "CI_NRF54L15_SEGGER_SN")
    segger_sn = os.environ[segger_var]

    app = test_config["app"]
    board = test_config["board"]
    app_dir = REPO_ROOT / "applications" / app
    return segger_sn, board, app_dir


def _prepare_serial_log() -> None:
    for log_path in (SERIAL_LOG, MODEM_SERIAL_LOG):
        log_path.parent.mkdir(parents=True, exist_ok=True)
        log_path.write_text("", encoding="utf-8")


def _flash_serial_modem_if_configured(test_config: dict) -> None:
    serial_modem = test_config.get("serial_modem")
    if not serial_modem:
        return

    segger_var = serial_modem["segger_sn_var"]
    segger_sn = os.environ[segger_var]
    logger.info("Step 0/4 - Flash Serial Modem firmware on nRF9151 DK (%s)", segger_sn)
    flash_serial_modem_firmware(segger_sn)
    time.sleep(2)


def _prepare_baseline_firmware(
    test_config: dict,
    *,
    recover: bool,
) -> types.SimpleNamespace:
    segger_sn, board, app_dir = _hardware_context(test_config)
    _prepare_serial_log()
    _flash_serial_modem_if_configured(test_config)

    app_name = test_config["app"]
    prebuilt_metadata = None
    if should_use_prebuilt_firmware(app_dir, app_name):
        prebuilt_metadata = read_build_metadata(app_dir, app_name)

    baseline_semver = resolve_baseline_version(
        test_config=test_config,
        prebuilt_metadata=prebuilt_metadata,
    )
    expected_baseline = memfault_software_version(baseline_semver)

    if should_use_prebuilt_firmware(app_dir, app_name):
        metadata = prebuilt_metadata or read_build_metadata(app_dir, app_name)
        if metadata["software_version"] != expected_baseline:
            raise RuntimeError(
                "Prebuilt firmware version "
                f"{metadata['software_version']!r} != expected baseline "
                f"{expected_baseline!r}. Ensure Build and Test use the same "
                "FIRMWARE_VERSION."
            )
        logger.info(
            "Step 1/4 - Using prebuilt CI baseline firmware %s (merged.hex)",
            expected_baseline,
        )
    else:
        logger.info("Step 1/4 - Build baseline firmware %s", baseline_semver)
        write_app_version(app_dir, baseline_semver)
        west_build(
            app_dir,
            board,
            cmake_args=test_config.get("build", {}).get("cmake_args"),
        )

    if recover:
        logger.info(
            "Step 2/4 - Recover and flash firmware (clears all flash including TF-M storage)"
        )
    else:
        logger.info(
            "Step 2/4 - Flash baseline firmware without recover (preserves credentials)"
        )
    flash_baseline_firmware(app_dir, app_name, segger_sn, recover=recover)

    logger.info("Step 3/4 - Start serial capture and reset device")
    time.sleep(2)
    serial_port = resolve_serial_port(test_config)
    uart = Uart(serial_port, log_path=SERIAL_LOG)

    # The host pulses modem nRESET on boot, so starting modem capture before the
    # host reset also captures the Serial Modem reboot.
    modem_uart = None
    modem_serial_port = resolve_modem_serial_port(test_config)
    if modem_serial_port:
        modem_baudrate = serial_modem_console_baudrate()
        logger.info(
            "Capturing Serial Modem logs from %s at %d baud",
            modem_serial_port,
            modem_baudrate,
        )
        modem_uart = Uart(
            modem_serial_port,
            log_path=MODEM_SERIAL_LOG,
            baudrate=modem_baudrate,
        )

    nrfutil_reset(segger_sn)

    return types.SimpleNamespace(
        uart=uart,
        modem_uart=modem_uart,
        segger_sn=segger_sn,
        serial_port=serial_port,
        modem_serial_port=modem_serial_port,
        app_dir=app_dir,
        board=board,
        serial_log=SERIAL_LOG,
        modem_serial_log=MODEM_SERIAL_LOG,
        baseline_version=baseline_semver,
    )


def _stop_capture(request: pytest.FixtureRequest, dut: types.SimpleNamespace) -> None:
    dut.uart.stop()
    request.node.user_properties.append(("serial_log", str(SERIAL_LOG)))
    if dut.modem_uart is None:
        return

    dut.modem_uart.stop()
    request.node.user_properties.append(("modem_serial_log", str(MODEM_SERIAL_LOG)))
    if not MODEM_SERIAL_LOG.stat().st_size:
        # Passing tests would otherwise hide a broken capture: a silent port
        # (wrong VCOM, or VCOM1 disabled in Board Configurator) reads as success.
        logger.warning(
            "Serial Modem capture on %s produced no output; check that VCOM1 is "
            "enabled on the nRF9151 / SMA DK and that the console baud rate in "
            "tests/on_target/ci/serial_modem_firmware.yml matches the release",
            dut.modem_serial_port,
        )


@pytest.fixture(scope="session")
def test_config() -> dict:
    return load_test_config()


@pytest.fixture(scope="function")
def provision_dut(request: pytest.FixtureRequest, test_config: dict) -> types.SimpleNamespace:
    """Prepare the provisioning DUT with recover flash."""
    dut = _prepare_baseline_firmware(test_config, recover=True)

    yield dut

    _stop_capture(request, dut)


@pytest.fixture(scope="function")
def fota_dut(request: pytest.FixtureRequest, test_config: dict) -> types.SimpleNamespace:
    """Prepare device with baseline FOTA firmware for application FOTA test."""
    recover = flash_recover_enabled(test_config)
    dut = _prepare_baseline_firmware(test_config, recover=recover)

    app_name = test_config["app"]
    prebuilt_metadata = None
    if should_use_prebuilt_firmware(dut.app_dir, app_name):
        prebuilt_metadata = read_build_metadata(dut.app_dir, app_name)

    _, update_semver = resolve_fota_versions(
        test_config=test_config,
        prebuilt_metadata=prebuilt_metadata,
    )
    dut.update_version = update_semver

    yield dut

    _stop_capture(request, dut)


@pytest.fixture(scope="function")
def coredump_dut(request: pytest.FixtureRequest, test_config: dict) -> types.SimpleNamespace:
    """Prepare device with baseline firmware for Memfault coredump test."""
    recover = flash_recover_enabled(test_config)
    dut = _prepare_baseline_firmware(test_config, recover=recover)

    yield dut

    _stop_capture(request, dut)


@pytest.fixture(scope="function")
def cloud_dut_session(nrf_cloud_env: dict, test_config: dict):
    """Factory fixture for shared nRF Cloud / Memfault provisioning and cleanup."""
    sessions: list[CloudDutSession] = []

    def create(dut: types.SimpleNamespace) -> CloudDutSession:
        session = CloudDutSession(dut, nrf_cloud_env, test_config)
        sessions.append(session)
        return session

    yield create

    if nrf_cloud_cleanup_enabled(test_config):
        for session in sessions:
            session.cleanup_nrf_cloud()


@pytest.fixture(scope="function")
def nrf_cloud_env(test_config: dict) -> dict:
    nrf_cloud = test_config.get("nrf_cloud", {})
    ca_cert_var = nrf_cloud.get("ca_cert_var", "NRF_CLOUD_CA_CERT")
    ca_key_var = nrf_cloud.get("ca_key_var", "NRF_CLOUD_CA_KEY")

    work_dir = Path(tempfile.mkdtemp(prefix="cloud-provision-", dir=REPO_ROOT / "build"))
    ca_cert = work_dir / "ca.pem"
    ca_key = work_dir / "ca-key.pem"
    ca_cert.write_text(os.environ[ca_cert_var], encoding="utf-8")
    ca_key.write_text(os.environ[ca_key_var], encoding="utf-8")
    ca_key.chmod(0o600)

    return {
        "work_dir": work_dir,
        "ca_cert": ca_cert,
        "ca_key": ca_key,
    }


def _log_serial_tail(log_path: Path, label: str, *, lines: int = 20) -> None:
    if not log_path.is_file():
        return
    tail = log_path.read_text(encoding="utf-8", errors="replace").splitlines()[-lines:]
    if not tail:
        return
    logger.error("Last %d %s log lines (%s):", len(tail), label, log_path)
    for line in tail:
        logger.error("%s", line)


@pytest.hookimpl(tryfirst=True, hookwrapper=True)
def pytest_runtest_makereport(item, call):
    outcome = yield
    report = outcome.get_result()
    if report.when == "call" and report.failed:
        _log_serial_tail(SERIAL_LOG, "host serial")
        _log_serial_tail(MODEM_SERIAL_LOG, "Serial Modem")

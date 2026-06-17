# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause

from __future__ import annotations

import os
import tempfile
import types
from pathlib import Path

import pytest

from utils.flash_tools import (
    clear_application_firmware,
    clear_credentials_storage,
    west_build,
    west_flash,
)
from utils.helpers import (
    REPO_ROOT,
    SERIAL_LOG,
    SUMMARY_FILE,
    load_expected_device_id,
    load_test_config,
)
from utils.logger import get_logger
from utils.serial_port import resolve_serial_port
from utils.uart import Uart

logger = get_logger()


def _hardware_context(test_config: dict) -> tuple[str, str, Path, int, bool, bool]:
    hardware = test_config.get("hardware", {})
    segger_var = hardware.get("segger_sn_var", "CI_NRF54L15_SEGGER_SN")
    segger_sn = os.environ[segger_var]
    internal_erase_end = int(hardware.get("internal_erase_end", "0x16b000"), 0)
    erase_external = hardware.get("erase_external_slot", True)
    erase_credentials = hardware.get("erase_credentials", False)

    app = test_config["app"]
    board = test_config["board"]
    app_dir = REPO_ROOT / "applications" / app
    return segger_sn, board, app_dir, internal_erase_end, erase_external, erase_credentials


def _prepare_serial_logs() -> None:
    SUMMARY_FILE.parent.mkdir(parents=True, exist_ok=True)
    SUMMARY_FILE.write_text("", encoding="utf-8")
    SERIAL_LOG.write_text("", encoding="utf-8")


@pytest.fixture(scope="session")
def test_config() -> dict:
    return load_test_config()


@pytest.fixture(scope="function")
def cloud_connect_dut(request: pytest.FixtureRequest, test_config: dict) -> types.SimpleNamespace:
    """Prepare device without credentials for nRF Cloud provisioning test."""
    segger_sn, board, app_dir, internal_erase_end, erase_external, erase_credentials = (
        _hardware_context(test_config)
    )
    _prepare_serial_logs()

    logger.info("Step 1/4 - Clear application firmware")
    clear_application_firmware(
        segger_sn,
        internal_erase_end=internal_erase_end,
        erase_external_slot=erase_external,
    )

    if erase_credentials:
        logger.info("Step 2/4 - Clear TF-M credential storage")
        clear_credentials_storage(segger_sn)
    else:
        logger.info("Step 2/4 - Skipping credential erase (erase_credentials=false)")

    logger.info("Step 3/4 - Start serial capture")
    serial_port = resolve_serial_port()
    uart = Uart(serial_port, log_path=SERIAL_LOG)

    logger.info("Step 4/4 - Build and flash firmware")
    west_build(app_dir, board)
    west_flash(app_dir, segger_sn)

    dut = types.SimpleNamespace(
        uart=uart,
        segger_sn=segger_sn,
        serial_port=serial_port,
        app_dir=app_dir,
        board=board,
        serial_log=SERIAL_LOG,
        summary_file=SUMMARY_FILE,
    )

    yield dut

    dut.uart.stop()
    request.node.user_properties.append(("serial_log", str(SERIAL_LOG)))


@pytest.fixture(scope="function")
def nrf_cloud_env(test_config: dict) -> dict:
    nrf_cloud = test_config.get("nrf_cloud", {})
    api_key_var = nrf_cloud.get("api_key_var", "NRF_CLOUD_API_KEY")
    ca_cert_var = nrf_cloud.get("ca_cert_var", "NRF_CLOUD_CA_CERT")
    ca_key_var = nrf_cloud.get("ca_key_var", "NRF_CLOUD_CA_KEY")

    os.environ["NRF_CLOUD_API_KEY"] = os.environ[api_key_var]
    os.environ["CI_NRF54L15_DEVICE_ID"] = load_expected_device_id(test_config)

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


@pytest.hookimpl(tryfirst=True, hookwrapper=True)
def pytest_runtest_makereport(item, call):
    outcome = yield
    report = outcome.get_result()
    if report.when == "call" and report.failed and SERIAL_LOG.is_file():
        logger.error("Test failed; serial log saved to %s", SERIAL_LOG)

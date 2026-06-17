# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause

from __future__ import annotations

from utils.helpers import (
    assert_dut_device_id,
    load_expected_device_id,
    parse_device_id,
    run_nrf_cloud_device,
    summary_line,
)
from utils.logger import get_logger
from utils.nrf_cloud_provision import install_device_credentials
from utils.uart import Uart

logger = get_logger()

CLOUD_CONNECTED_LOG = "Cloud connected"
MISSING_CREDENTIALS_LOG = "Missing nRF Cloud credentials"
SHELL_PROMPT = "uart:~$"


def test_cloud_connect_after_provisioning(
    cloud_connect_dut,
    nrf_cloud_env: dict,
    test_config: dict,
) -> None:
    """Provision credentials on a fresh device, onboard to nRF Cloud, and connect."""
    dut = cloud_connect_dut
    expected_device_id = load_expected_device_id(test_config)
    summary_line(f"EXPECTED_DEVICE_ID={expected_device_id}")
    summary_line(f"SERIAL_PORT={dut.serial_port}")
    summary_line(f"SERIAL_LOG={dut.serial_log}")

    logger.info("Phase 1/5 - Wait for shell and read device ID from boot log")
    dut.uart.wait_for_substring(SHELL_PROMPT, timeout=120)
    device_id = assert_dut_device_id(
        parse_device_id(dut.uart.whole_log),
        expected_device_id,
    )
    summary_line(f"DEVICE_ID={device_id}")

    logger.info("Phase 2/5 - Confirm device boots without nRF Cloud credentials")
    dut.uart.wait_for_substring(MISSING_CREDENTIALS_LOG, timeout=300)

    logger.info(
        "Phase 3/5 - Remove only the configured DUT from nRF Cloud if already registered"
    )
    run_nrf_cloud_device("delete-if-exists", device_id)

    logger.info("Phase 4/5 - Install credentials and onboard using onboard.csv")
    dut.uart.stop()
    try:
        onboard_csv = install_device_credentials(
            work_dir=nrf_cloud_env["work_dir"],
            device_id=device_id,
            serial_port=dut.serial_port,
            ca_cert=nrf_cloud_env["ca_cert"],
            ca_key=nrf_cloud_env["ca_key"],
        )
        summary_line(f"ONBOARD_CSV={onboard_csv}")
        run_nrf_cloud_device("onboard", str(onboard_csv))
    finally:
        dut.uart = Uart(dut.serial_port, log_path=dut.serial_log)

    logger.info("Phase 5/5 - Wait for nRF Cloud connection in serial log")
    line = dut.uart.wait_for_substring(CLOUD_CONNECTED_LOG, timeout=900)
    summary_line(f"CLOUD_CONNECTED_LINE={line}")
    summary_line("RESULT=pass")

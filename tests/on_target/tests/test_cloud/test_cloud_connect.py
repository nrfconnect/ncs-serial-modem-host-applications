# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause

from __future__ import annotations

from utils.flash_tools import nrfutil_reset
from utils.helpers import (
    assert_dut_device_id,
    load_expected_device_id,
    wait_for_device_id,
)
from utils.logger import get_logger
from utils.memfault_ota import (
    delete_device_if_exists,
    enable_server_side_developer_mode,
    load_memfault_credentials,
)
from utils.nrf_cloud_device import delete_if_exists, onboard
from utils.nrf_cloud_provision import install_device_credentials
from utils.uart import Uart

logger = get_logger()

CLOUD_CONNECTED_LOG = "Cloud connected"
MISSING_CREDENTIALS_LOG = "Missing nRF Cloud credentials"


def test_cloud_connect_after_provisioning(
    cloud_connect_dut,
    nrf_cloud_env: dict,
    test_config: dict,
) -> None:
    """Provision credentials on a fresh device, onboard to nRF Cloud, and connect."""
    dut = cloud_connect_dut
    expected_device_id = load_expected_device_id(test_config)
    memfault_env = load_memfault_credentials(test_config)
    device_id: str | None = None

    try:
        logger.info("Phase 1/5 - Wait for device ID in boot log")
        device_id = assert_dut_device_id(
            wait_for_device_id(dut.uart, timeout=30),
            expected_device_id,
        )

        logger.info("Phase 2/5 - Confirm device boots without nRF Cloud credentials")
        dut.uart.wait_for_substring(MISSING_CREDENTIALS_LOG, timeout=60)

        logger.info(
            "Phase 3/5 - Remove only the configured DUT from nRF Cloud if already registered"
        )
        delete_if_exists(device_id, expected_device_id)

        logger.info("Phase 4/5 - Install credentials and onboard using onboard.csv")
        dut.uart.stop()
        try:
            onboard_csv = install_device_credentials(
                work_dir=nrf_cloud_env["work_dir"],
                device_id=device_id,
                serial_port=dut.serial_port,
                ca_cert=nrf_cloud_env["ca_cert"],
                ca_key=nrf_cloud_env["ca_key"],
                segger_sn=dut.segger_sn,
            )
            onboard(str(onboard_csv))
        finally:
            dut.uart = Uart(dut.serial_port, log_path=dut.serial_log)
            nrfutil_reset(dut.segger_sn)

        logger.info("Phase 5/5 - Wait for nRF Cloud connection in serial log")
        dut.uart.wait_for_substring(CLOUD_CONNECTED_LOG, timeout=120)
        enable_server_side_developer_mode(memfault_env, device_id)
    finally:
        if device_id is not None:
            try:
                delete_if_exists(device_id, expected_device_id)
            except RuntimeError as exc:
                logger.warning(
                    "Failed to delete DUT from nRF Cloud during cleanup: %s",
                    exc,
                )
            try:
                delete_device_if_exists(memfault_env, device_id, expected_device_id)
            except RuntimeError as exc:
                logger.warning(
                    "Failed to delete DUT from Memfault during cleanup: %s",
                    exc,
                )

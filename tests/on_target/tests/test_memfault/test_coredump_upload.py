# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause

from __future__ import annotations

from datetime import datetime, timezone

import pytest

from utils.helpers import (
    assert_dut_device_id,
    load_expected_device_id,
    wait_for_device_id,
)
from utils.logger import get_logger
from utils.memfault_ota import (
    delete_device_if_exists,
    ensure_cohort_exists,
    ensure_device_in_cohort,
    load_memfault_env,
    read_build_metadata,
    upload_mcu_symbols,
    wait_for_device_crash_trace,
)
from utils.nrf_cloud_device import delete_if_exists, onboard
from utils.nrf_cloud_provision import install_device_credentials
from utils.flash_tools import elf_image_path, nrfutil_reset
from utils.shell import send_shell_command
from utils.uart import Uart

logger = get_logger()

CLOUD_CONNECTED_LOG = "Cloud connected"
MISSING_CREDENTIALS_LOG = "Missing nRF Cloud credentials"
MEMFAULT_DATA_POSTED_LOG = "Memfault data posted"
BOOT_BANNER_LOG = "Booting Serial Modem Host"

# TF-M traps HardFaults before Memfault's handler runs and halts the core, so the
# fault has to be one that CONFIG_TFM_ALLOW_NON_SECURE_FAULT_HANDLING forwards.
FAULT_SHELL_COMMAND = "mflt test busfault"
FAULT_REASON = "BusFault"

CLOUD_CONNECT_TIMEOUT = 120.0
REBOOT_TIMEOUT = 60.0
SHELL_COMMAND_TIMEOUT = 60.0
MEMFAULT_TRACE_TIMEOUT = 180.0


@pytest.mark.slow
def test_memfault_coredump_upload_via_cloud_sync(
    coredump_dut,
    nrf_cloud_env: dict,
    test_config: dict,
) -> None:
    """Provision a device, trigger a test BusFault, and verify the coredump in Memfault."""
    dut = coredump_dut
    memfault_env = load_memfault_env(test_config)
    expected_device_id = load_expected_device_id(test_config)
    app_name = test_config["app"]
    build_metadata = read_build_metadata(dut.app_dir, app_name)
    device_id: str | None = None

    try:
        logger.info("Phase 1/7 - Wait for device ID in boot log")
        device_id = assert_dut_device_id(
            wait_for_device_id(dut.uart, timeout=60),
            expected_device_id,
        )

        logger.info("Phase 2/7 - Confirm device boots without nRF Cloud credentials")
        dut.uart.wait_for_substring(MISSING_CREDENTIALS_LOG, timeout=60)

        logger.info("Phase 3/7 - Remove only the configured DUT from nRF Cloud if registered")
        delete_if_exists(device_id, expected_device_id)
        delete_device_if_exists(memfault_env, device_id, expected_device_id)

        logger.info("Phase 4/7 - Upload MCU symbols for baseline firmware")
        upload_mcu_symbols(
            env=memfault_env,
            elf=elf_image_path(dut.app_dir, app_name),
            metadata=build_metadata,
            software_version=build_metadata["software_version"],
        )

        logger.info("Phase 5/7 - Ensure Memfault CI cohort and assign DUT")
        ensure_cohort_exists(memfault_env)
        ensure_device_in_cohort(
            memfault_env,
            device_id,
            hardware_version=build_metadata["hardware_version"],
        )

        logger.info("Phase 6/7 - Install credentials and onboard using onboard.csv")
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

        logger.info("Phase 7/7 - Trigger a fault and verify coredump upload")
        dut.uart.wait_for_substring(CLOUD_CONNECTED_LOG, timeout=CLOUD_CONNECT_TIMEOUT)

        test_start = datetime.now(timezone.utc)
        dut.uart.stop()
        try:
            send_shell_command(
                dut.serial_port,
                FAULT_SHELL_COMMAND,
                timeout=SHELL_COMMAND_TIMEOUT,
                wait_for_completion=False,
            )
        finally:
            # Capture restarts empty here, so every wait below refers to the
            # post-crash boot.
            dut.uart = Uart(dut.serial_port, log_path=dut.serial_log)

        dut.uart.wait_for_substring(BOOT_BANNER_LOG, timeout=REBOOT_TIMEOUT)
        dut.uart.wait_for_substring_after(
            CLOUD_CONNECTED_LOG,
            after=BOOT_BANNER_LOG,
            timeout=CLOUD_CONNECT_TIMEOUT,
        )
        dut.uart.wait_for_substring_after(
            MEMFAULT_DATA_POSTED_LOG,
            after=CLOUD_CONNECTED_LOG,
            timeout=CLOUD_CONNECT_TIMEOUT,
        )

        wait_for_device_crash_trace(
            memfault_env,
            device_id,
            reason=FAULT_REASON,
            since=test_start,
            timeout=MEMFAULT_TRACE_TIMEOUT,
        )
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

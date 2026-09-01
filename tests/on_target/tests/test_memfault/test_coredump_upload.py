# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause

from __future__ import annotations

import pytest

from utils.logger import get_logger
from utils.memfault_ota import (
    latest_device_coredump,
    read_build_metadata,
    upload_mcu_symbols,
    wait_for_new_device_coredump,
)
from utils.flash_tools import elf_image_path
from utils.modem_logs import enable_modem_application_logs
from utils.shell import send_shell_command
from utils.uart import Uart

logger = get_logger()

CLOUD_CONNECTED_LOG = "Cloud connected"
MEMFAULT_DATA_POSTED_LOG = "Memfault data posted"
BOOT_BANNER_LOG = "Booting Serial Modem Host"

# TF-M traps HardFaults before Memfault's handler runs and halts the core, so the
# fault has to be one that CONFIG_TFM_ALLOW_NON_SECURE_FAULT_HANDLING forwards.
FAULT_SHELL_COMMAND = "mflt test busfault"
FAULT_REASON = "BusFault"

CLOUD_CONNECT_TIMEOUT = 120.0
REBOOT_TIMEOUT = 60.0
SHELL_COMMAND_TIMEOUT = 60.0
MEMFAULT_TRACE_TIMEOUT = 300.0


@pytest.mark.slow
def test_memfault_coredump_upload_via_cloud_sync(
    coredump_dut,
    cloud_dut_session,
    test_config: dict,
) -> None:
    """Trigger a BusFault on the DUT and verify the coredump in Memfault."""
    dut = coredump_dut
    session = cloud_dut_session(dut)
    app_name = test_config["app"]
    build_metadata = read_build_metadata(dut.app_dir, app_name)

    session.ensure_provisioned(
        hardware_version=build_metadata["hardware_version"],
    )

    logger.info("Upload MCU symbols for baseline firmware")
    upload_mcu_symbols(
        env=session.memfault_env,
        elf=elf_image_path(dut.app_dir, app_name),
        metadata=build_metadata,
        software_version=build_metadata["software_version"],
    )

    logger.info("Trigger a fault and verify coredump upload")
    dut.uart.wait_for_substring(CLOUD_CONNECTED_LOG, timeout=CLOUD_CONNECT_TIMEOUT)

    baseline_coredump = latest_device_coredump(session.memfault_env, session.device_id)
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
    enable_modem_application_logs(dut)
    dut.uart.wait_for_substring_after(
        MEMFAULT_DATA_POSTED_LOG,
        after=CLOUD_CONNECTED_LOG,
        timeout=CLOUD_CONNECT_TIMEOUT,
    )

    wait_for_new_device_coredump(
        session.memfault_env,
        session.device_id,
        reason=FAULT_REASON,
        baseline=baseline_coredump,
        timeout=MEMFAULT_TRACE_TIMEOUT,
    )

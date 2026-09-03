# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause

from __future__ import annotations

import re

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
from utils.shell import send_shell_command, send_shell_command_until
from utils.uart import Uart

logger = get_logger()

CLOUD_CONNECTED_LOG = "Cloud connected"
MEMFAULT_DATA_POSTED_LOG = "Memfault data posted"
BOOT_BANNER_LOG = "Booting Serial Modem Host"

# TF-M traps HardFaults before Memfault's handler runs and halts the core, so the
# fault has to be one that CONFIG_TFM_ALLOW_NON_SECURE_FAULT_HANDLING forwards.
FAULT_SHELL_COMMAND = "mflt test busfault"
FAULT_REASON = "BusFault"

# The main thread feeds its task watchdog every loop. Suspending it from the kernel shell
# stops the feed and trips the watchdog, whose callback captures a Memfault coredump tagged
# "Software Watchdog".
WATCHDOG_THREAD_NAME = "main"
WATCHDOG_FAULT_REASON = "Software Watchdog"
# `kernel thread list` prints each thread header as "%s%p %-10s", e.g. " 0x20002abc main".
# Match a single "0x<addr> <name>" line; [ \t]+ keeps the match on one line.
THREAD_LINE_PATTERN = r"(0x[0-9a-fA-F]+)[ \t]+" + re.escape(WATCHDOG_THREAD_NAME) + r"\b"
THREAD_LINE_RE = re.compile(THREAD_LINE_PATTERN, re.MULTILINE)

CLOUD_CONNECT_TIMEOUT = 120.0
REBOOT_TIMEOUT = 60.0
SHELL_COMMAND_TIMEOUT = 60.0
MEMFAULT_TRACE_TIMEOUT = 300.0
# The main watchdog timeout (CONFIG_APP_MAIN_WATCHDOG_TIMEOUT_SECONDS) is 120s; allow the
# watchdog to trip and the device to reboot within a generous margin.
WATCHDOG_TRIP_TIMEOUT = 180.0


def _resolve_thread_id(serial_port: str) -> str:
    """Return the kernel thread id (hex) of the watchdog-monitored main thread."""
    output = send_shell_command_until(
        serial_port,
        "kernel thread list",
        THREAD_LINE_PATTERN,
        timeout=SHELL_COMMAND_TIMEOUT,
    )
    match = THREAD_LINE_RE.search(output)
    if not match:
        raise AssertionError(
            f"Could not find thread {WATCHDOG_THREAD_NAME!r} in 'kernel thread list' "
            f"output:\n{output}"
        )
    thread_id = match.group(1)
    logger.info("Resolved %s thread id: %s", WATCHDOG_THREAD_NAME, thread_id)
    return thread_id


@pytest.mark.slow
def test_memfault_coredump_upload_via_cloud_sync(
    coredump_dut,
    cloud_dut_session,
    test_config: dict,
) -> None:
    """Verify Memfault coredump upload for both a BusFault and a software watchdog.

    Both phases run against the same flashed firmware so the uploaded symbols (one file
    per software version) match every coredump's GNU build ID.
    """
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

    # --- Software watchdog phase -------------------------------------------------
    # Reuse the same firmware and uploaded symbols: suspend the watchdog-monitored
    # main thread so it stops feeding its task watchdog, then confirm the resulting
    # coredump is tagged "Software Watchdog".
    logger.info("Trip the task watchdog and verify software-watchdog coredump upload")
    watchdog_baseline = latest_device_coredump(session.memfault_env, session.device_id)
    dut.uart.stop()
    try:
        thread_id = _resolve_thread_id(dut.serial_port)
        # Suspending the thread stops it feeding its watchdog; the watchdog trips ~120s
        # later. The command need not be awaited, only delivered.
        send_shell_command(
            dut.serial_port,
            f"kernel thread suspend {thread_id}",
            timeout=SHELL_COMMAND_TIMEOUT,
            wait_for_completion=False,
        )
    finally:
        # Capture restarts empty here, so every wait below refers to the
        # post-watchdog boot.
        dut.uart = Uart(dut.serial_port, log_path=dut.serial_log)

    dut.uart.wait_for_substring(BOOT_BANNER_LOG, timeout=WATCHDOG_TRIP_TIMEOUT)
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
        reason=WATCHDOG_FAULT_REASON,
        baseline=watchdog_baseline,
        timeout=MEMFAULT_TRACE_TIMEOUT,
    )

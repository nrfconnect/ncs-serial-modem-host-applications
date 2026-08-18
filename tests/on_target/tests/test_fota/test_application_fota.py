# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause

from __future__ import annotations

import subprocess
import time

import pytest

from utils.app_version import write_app_version
from utils.flash_tools import (
    elf_image_path,
    signed_image_path,
    west_build,
)
from utils.logger import get_logger
from utils.memfault_ota import (
    clear_device_release_override,
    deactivate_release,
    deploy_release,
    ensure_cohort_exists,
    read_build_metadata,
    set_device_release_override,
    upload_mcu_symbols,
    upload_ota_payload,
    wait_for_cohort_release_deployed,
    wait_for_device_version,
)
from utils.uart import Uart

logger = get_logger()

CLOUD_CONNECTED_LOG = "Cloud connected"
FOTA_DOWNLOAD_STARTING_LOG = "FOTA download starting"
FOTA_REBOOT_LOG = "FOTA successful, rebooting to apply the update"

# Serial-log waits (seconds). Cloud connect and first FOTA poll are usually well under a minute.
CLOUD_CONNECT_TIMEOUT = 120.0
FOTA_START_TIMEOUT = 60.0
# ~540 KiB OTA over cellular PPP typically needs 4-5 minutes on CI hardware.
FOTA_DOWNLOAD_TIMEOUT = 360.0
POST_REBOOT_CONNECT_TIMEOUT = 120.0
MEMFAULT_CLOUD_VERSION_TIMEOUT = 120.0

# Memfault deploy + nRF Cloud auto-forwarding can lag behind the CLI returning.
MEMFAULT_RELEASE_DEPLOY_TIMEOUT = 60.0
NRF_CLOUD_OTA_PROPAGATION_DELAY = 30.0

FOTA_DIAGNOSTIC_MARKERS = (
    "No FOTA update available",
    "nrf_cloud_fota_poll_process",
    "Memfault data posted",
)


def _log_fota_timeout_diagnostics(uart: Uart, needle: str) -> None:
    """Log serial-log hints when a FOTA wait times out."""
    logger.error("Timed out waiting for serial log line containing %r", needle)
    captured = uart.snapshot_log()
    for marker in FOTA_DIAGNOSTIC_MARKERS:
        if marker in captured:
            logger.error("Serial log contains diagnostic marker: %r", marker)
    tail = captured.splitlines()[-20:]
    if tail:
        logger.error("Last %d serial log lines before timeout:", len(tail))
        for line in tail:
            logger.error("%s", line)


def _assert_no_fota_redownload(uart: Uart) -> None:
    """Fail if a second FOTA download starts after the post-update reboot."""
    captured = uart.snapshot_log()
    reboot_index = captured.find(FOTA_REBOOT_LOG)
    if reboot_index < 0:
        return

    tail = captured[reboot_index + len(FOTA_REBOOT_LOG) :]
    if FOTA_DOWNLOAD_STARTING_LOG in tail:
        raise AssertionError(
            f"Serial log contains {FOTA_REBOOT_LOG!r} followed by another "
            f"{FOTA_DOWNLOAD_STARTING_LOG!r}; release override may still be active"
        )


def _wait_for_fota_log_after_cloud_connect(
    uart: Uart,
    needle: str,
    *,
    timeout: float,
) -> str:
    try:
        return uart.wait_for_substring_after(
            needle,
            after=CLOUD_CONNECTED_LOG,
            timeout=timeout,
        )
    except TimeoutError:
        _log_fota_timeout_diagnostics(uart, needle)
        raise


@pytest.mark.slow
def test_application_fota_via_cloud_sync(
    fota_dut,
    cloud_dut_session,
    test_config: dict,
) -> None:
    """Deploy a Memfault OTA release on a pre-provisioned DUT and verify automatic FOTA."""
    dut = fota_dut
    session = cloud_dut_session(dut)
    app_name = test_config["app"]
    update_semver = dut.update_version
    baseline_metadata = read_build_metadata(dut.app_dir, app_name)
    release_deployed = False
    release_override_set = False

    try:
        session.wait_for_provisioned_boot()

        logger.info("Build update firmware %s", update_semver)
        write_app_version(dut.app_dir, update_semver)
        west_build(dut.app_dir, dut.board)
        update_binary = signed_image_path(dut.app_dir, app_name)
        update_metadata = read_build_metadata(dut.app_dir, app_name)
        update_version = update_metadata["software_version"]

        ensure_cohort_exists(session.memfault_env)

        logger.info("Upload and deploy Memfault OTA release before cloud connect")
        upload_mcu_symbols(
            env=session.memfault_env,
            elf=elf_image_path(dut.app_dir, app_name),
            metadata=update_metadata,
            software_version=update_version,
        )
        upload_ota_payload(
            env=session.memfault_env,
            binary=update_binary,
            metadata=baseline_metadata,
            software_version=update_version,
        )
        deploy_release(env=session.memfault_env, software_version=update_version)
        release_deployed = True
        wait_for_cohort_release_deployed(
            session.memfault_env,
            update_version,
            timeout=MEMFAULT_RELEASE_DEPLOY_TIMEOUT,
        )
        set_device_release_override(session.memfault_env, session.device_id, update_version)
        release_override_set = True
        logger.info(
            "Waiting %.0fs for nRF Cloud OTA propagation before device connect",
            NRF_CLOUD_OTA_PROPAGATION_DELAY,
        )
        time.sleep(NRF_CLOUD_OTA_PROPAGATION_DELAY)

        logger.info("Wait for cloud connect and automatic FOTA via cloud sync")
        dut.uart.wait_for_substring(CLOUD_CONNECTED_LOG, timeout=CLOUD_CONNECT_TIMEOUT)
        _wait_for_fota_log_after_cloud_connect(
            dut.uart,
            FOTA_DOWNLOAD_STARTING_LOG,
            timeout=FOTA_START_TIMEOUT,
        )
        _wait_for_fota_log_after_cloud_connect(
            dut.uart,
            FOTA_REBOOT_LOG,
            timeout=FOTA_DOWNLOAD_TIMEOUT,
        )

        logger.info(
            "Clearing Memfault release override and deactivating cohort release "
            "before post-update verification"
        )
        clear_device_release_override(session.memfault_env, session.device_id)
        release_override_set = False
        deactivate_release(env=session.memfault_env, software_version=update_version)
        release_deployed = False

        logger.info("Verify post-update cloud connect and firmware version")
        dut.uart.wait_for_substring_after(
            CLOUD_CONNECTED_LOG,
            after=FOTA_REBOOT_LOG,
            timeout=POST_REBOOT_CONNECT_TIMEOUT,
        )
        _assert_no_fota_redownload(dut.uart)
        wait_for_device_version(
            session.memfault_env,
            session.device_id,
            update_version,
            timeout=MEMFAULT_CLOUD_VERSION_TIMEOUT,
        )
    finally:
        if release_override_set:
            try:
                clear_device_release_override(session.memfault_env, session.device_id)
            except RuntimeError as exc:
                logger.warning(
                    "Failed to clear Memfault release override during cleanup: %s",
                    exc,
                )
        if release_deployed:
            try:
                deactivate_release(env=session.memfault_env, software_version=update_version)
            except subprocess.CalledProcessError as exc:
                logger.warning("Failed to deactivate Memfault release during cleanup: %s", exc)

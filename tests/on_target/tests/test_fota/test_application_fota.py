# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause

from __future__ import annotations

import subprocess
import time

import pytest

from utils.helpers import (
    assert_dut_device_id,
    load_expected_device_id,
    wait_for_device_id,
)
from utils.logger import get_logger
from utils.memfault_ota import (
    clear_device_release_override,
    deactivate_release,
    delete_device_if_exists,
    deploy_release,
    ensure_cohort_exists,
    ensure_device_in_cohort,
    load_memfault_env,
    read_build_metadata,
    set_device_release_override,
    upload_mcu_symbols,
    upload_ota_payload,
    wait_for_cohort_release_deployed,
    wait_for_device_version,
)
from utils.nrf_cloud_device import delete_if_exists, onboard
from utils.nrf_cloud_provision import install_device_credentials
from utils.app_version import write_app_version
from utils.flash_tools import (
    elf_image_path,
    nrfutil_reset,
    signed_image_path,
    west_build,
)
from utils.uart import Uart

logger = get_logger()

CLOUD_CONNECTED_LOG = "Cloud connected"
MISSING_CREDENTIALS_LOG = "Missing nRF Cloud credentials"
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
    nrf_cloud_env: dict,
    test_config: dict,
) -> None:
    """Provision a device, deploy a Memfault OTA release, and verify automatic FOTA."""
    dut = fota_dut
    memfault_env = load_memfault_env(test_config)
    expected_device_id = load_expected_device_id(test_config)
    app_name = test_config["app"]
    update_semver = dut.update_version
    baseline_metadata = read_build_metadata(dut.app_dir, app_name)
    release_deployed = False
    release_override_set = False
    device_id: str | None = None

    try:
        logger.info("Phase 1/9 - Wait for device ID in boot log")
        device_id = assert_dut_device_id(
            wait_for_device_id(dut.uart, timeout=60),
            expected_device_id,
        )

        logger.info("Phase 2/9 - Confirm device boots without nRF Cloud credentials")
        dut.uart.wait_for_substring(MISSING_CREDENTIALS_LOG, timeout=60)

        logger.info("Phase 3/9 - Remove only the configured DUT from nRF Cloud if registered")
        delete_if_exists(device_id, expected_device_id)

        logger.info("Phase 4/9 - Build update firmware %s", update_semver)
        write_app_version(dut.app_dir, update_semver)
        west_build(dut.app_dir, dut.board)
        update_binary = signed_image_path(dut.app_dir, app_name)
        update_metadata = read_build_metadata(dut.app_dir, app_name)
        update_version = update_metadata["software_version"]

        logger.info("Phase 5/9 - Ensure Memfault CI cohort and assign DUT")
        ensure_cohort_exists(memfault_env)
        ensure_device_in_cohort(
            memfault_env,
            device_id,
            hardware_version=baseline_metadata["hardware_version"],
        )

        logger.info("Phase 6/9 - Upload and deploy Memfault OTA release before cloud connect")
        upload_mcu_symbols(
            env=memfault_env,
            elf=elf_image_path(dut.app_dir, app_name),
            metadata=update_metadata,
            software_version=update_version,
        )
        upload_ota_payload(
            env=memfault_env,
            binary=update_binary,
            metadata=baseline_metadata,
            software_version=update_version,
        )
        deploy_release(env=memfault_env, software_version=update_version)
        release_deployed = True
        wait_for_cohort_release_deployed(
            memfault_env,
            update_version,
            timeout=MEMFAULT_RELEASE_DEPLOY_TIMEOUT,
        )
        set_device_release_override(memfault_env, device_id, update_version)
        release_override_set = True
        logger.info(
            "Waiting %.0fs for nRF Cloud OTA propagation before device connect",
            NRF_CLOUD_OTA_PROPAGATION_DELAY,
        )
        time.sleep(NRF_CLOUD_OTA_PROPAGATION_DELAY)

        logger.info("Phase 7/9 - Install credentials and onboard using onboard.csv")
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

        logger.info("Phase 8/9 - Wait for cloud connect and automatic FOTA via cloud sync")
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
            "Clearing Memfault release override before post-update verification"
        )
        clear_device_release_override(memfault_env, device_id)
        release_override_set = False

        logger.info("Phase 9/9 - Verify post-update cloud connect and firmware version")
        dut.uart.wait_for_substring_after(
            CLOUD_CONNECTED_LOG,
            after=FOTA_REBOOT_LOG,
            timeout=POST_REBOOT_CONNECT_TIMEOUT,
        )
        _assert_no_fota_redownload(dut.uart)
        wait_for_device_version(
            memfault_env,
            device_id,
            update_version,
            timeout=MEMFAULT_CLOUD_VERSION_TIMEOUT,
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
        if release_override_set:
            try:
                clear_device_release_override(memfault_env, device_id)
            except RuntimeError as exc:
                logger.warning(
                    "Failed to clear Memfault release override during cleanup: %s",
                    exc,
                )
        if release_deployed:
            try:
                deactivate_release(env=memfault_env, software_version=update_version)
            except subprocess.CalledProcessError as exc:
                logger.warning("Failed to deactivate Memfault release during cleanup: %s", exc)

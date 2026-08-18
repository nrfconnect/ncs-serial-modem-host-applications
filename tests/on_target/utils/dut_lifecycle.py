# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
"""Shared nRF Cloud and Memfault provisioning/cleanup for on-target tests."""

from __future__ import annotations

import types

from utils.flash_tools import nrfutil_reset
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
)
from utils.nrf_cloud_device import delete_if_exists, onboard
from utils.nrf_cloud_provision import install_device_credentials
from utils.uart import Uart

logger = get_logger()

MISSING_CREDENTIALS_LOG = "Missing nRF Cloud credentials"


class CloudDutSession:
    """Provision and clean up a DUT in nRF Cloud and Memfault."""

    def __init__(
        self,
        dut: types.SimpleNamespace,
        nrf_cloud_env: dict,
        test_config: dict,
    ) -> None:
        self.dut = dut
        self.nrf_cloud_env = nrf_cloud_env
        self.memfault_env = load_memfault_env(test_config)
        self.expected_device_id = load_expected_device_id(test_config)
        self.device_id: str | None = None

    def wait_for_unprovisioned_boot(self, *, device_id_timeout: float = 60.0) -> str:
        """Read the DUT device ID and confirm it has no nRF Cloud credentials yet."""
        logger.info("Wait for device ID in boot log")
        self.device_id = assert_dut_device_id(
            wait_for_device_id(self.dut.uart, timeout=device_id_timeout),
            self.expected_device_id,
        )

        logger.info("Confirm device boots without nRF Cloud credentials")
        self.dut.uart.wait_for_substring(MISSING_CREDENTIALS_LOG, timeout=60.0)
        return self.device_id

    def remove_prior_registrations(self) -> None:
        """Remove a stale nRF Cloud registration before provisioning."""
        if self.device_id is None:
            raise RuntimeError(
                "device_id is not set; call wait_for_unprovisioned_boot() first"
            )

        logger.info("Remove only the configured DUT from nRF Cloud if registered")
        delete_if_exists(self.device_id, self.expected_device_id)

    def ensure_memfault_device(self, *, hardware_version: str) -> None:
        """Ensure the CI Memfault cohort exists and assign the DUT to it."""
        if self.device_id is None:
            raise RuntimeError(
                "device_id is not set; call wait_for_unprovisioned_boot() first"
            )

        logger.info("Ensure Memfault CI cohort and assign DUT")
        ensure_cohort_exists(self.memfault_env)
        ensure_device_in_cohort(
            self.memfault_env,
            self.device_id,
            hardware_version=hardware_version,
        )

    def onboard_to_cloud(self) -> None:
        """Install credentials on the DUT and register it in nRF Cloud."""
        if self.device_id is None:
            raise RuntimeError(
                "device_id is not set; call wait_for_unprovisioned_boot() first"
            )

        logger.info("Install credentials and onboard using onboard.csv")
        self.dut.uart.stop()
        try:
            onboard_csv = install_device_credentials(
                work_dir=self.nrf_cloud_env["work_dir"],
                device_id=self.device_id,
                serial_port=self.dut.serial_port,
                ca_cert=self.nrf_cloud_env["ca_cert"],
                ca_key=self.nrf_cloud_env["ca_key"],
                segger_sn=self.dut.segger_sn,
            )
            onboard(str(onboard_csv))
        finally:
            self.dut.uart = Uart(self.dut.serial_port, log_path=self.dut.serial_log)
            nrfutil_reset(self.dut.segger_sn)

    def cleanup_nrf_cloud(self) -> None:
        """Remove the DUT from nRF Cloud. Safe to call from test teardown."""
        if self.device_id is None:
            return

        try:
            delete_if_exists(self.device_id, self.expected_device_id)
        except RuntimeError as exc:
            logger.warning(
                "Failed to delete DUT from nRF Cloud during cleanup: %s",
                exc,
            )

    def cleanup_memfault(self) -> None:
        """Remove the DUT from Memfault after verification completes."""
        if self.device_id is None:
            return

        logger.info("Removing DUT from Memfault after verification")
        try:
            delete_device_if_exists(
                self.memfault_env,
                self.device_id,
                self.expected_device_id,
            )
        except RuntimeError as exc:
            logger.warning(
                "Failed to delete DUT from Memfault after verification: %s",
                exc,
            )

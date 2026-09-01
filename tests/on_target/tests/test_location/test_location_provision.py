# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause

from __future__ import annotations

import pytest

from utils.location_data import wait_for_wifi_location_sent
from utils.logger import get_logger
from utils.memfault_ota import read_build_metadata
from utils.modem_logs import enable_modem_application_logs

logger = get_logger()

CLOUD_CONNECTED_LOG = "Cloud connected"
CLOUD_CONNECT_TIMEOUT = 120.0

# A location search runs on every cloud synchronization
# (CONFIG_APP_MAIN_CLOUD_SYNCHRONIZATION_PERIOD_SECONDS), so allow a few periods
# for the Wi-Fi scan and the ground-fix round trip.
LOCATION_TIMEOUT = 180.0


@pytest.mark.slow
def test_location_wifi_data_after_provisioning(
    provision_dut,
    cloud_dut_session,
    test_config: dict,
) -> None:
    """Provision the Wi-Fi location DUT and verify it sends Wi-Fi data to nRF Cloud."""
    dut = provision_dut
    session = cloud_dut_session(dut)
    app_name = test_config["app"]
    build_metadata = read_build_metadata(dut.app_dir, app_name)

    session.wait_for_unprovisioned_boot()
    session.remove_prior_registrations()
    session.ensure_memfault_device(hardware_version=build_metadata["hardware_version"])
    session.onboard_to_cloud()

    logger.info("Verify cloud connect after provisioning")
    dut.uart.wait_for_substring(CLOUD_CONNECTED_LOG, timeout=CLOUD_CONNECT_TIMEOUT)
    enable_modem_application_logs(dut)

    logger.info("Verify Wi-Fi location data is sent on cloud synchronization")
    access_points = wait_for_wifi_location_sent(
        dut.uart,
        after=CLOUD_CONNECTED_LOG,
        timeout=LOCATION_TIMEOUT,
    )

    assert access_points >= 1, "Wi-Fi location data sent without any access points"

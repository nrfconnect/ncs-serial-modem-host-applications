# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause

from __future__ import annotations

import pytest

from utils.logger import get_logger
from utils.memfault_ota import read_build_metadata
from utils.modem_logs import enable_modem_application_logs

logger = get_logger()

CLOUD_CONNECTED_LOG = "Cloud connected"
CLOUD_CONNECT_TIMEOUT = 120.0


@pytest.mark.slow
def test_cloud_provision(
    provision_dut,
    cloud_dut_session,
    test_config: dict,
) -> None:
    """Recover flash, provision nRF Cloud + Memfault, and verify cloud connect."""
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

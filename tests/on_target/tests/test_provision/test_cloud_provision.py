# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause

from __future__ import annotations

import time

import pytest

from utils.logger import get_logger
from utils.memfault_ota import read_build_metadata
from utils.modem_logs import enable_modem_application_logs

logger = get_logger()

CLOUD_CONNECTED_LOG = "Cloud connected"
CLOUD_CONNECT_TIMEOUT = 120.0

# The main module reruns a cloud synchronization every
# CONFIG_APP_MAIN_CLOUD_SYNCHRONIZATION_PERIOD_SECONDS. Each cycle republishes to
# fota_chan via fota_poll_request(), the exact path that asserts on a publish
# timeout. The initial cloud connect logs one poll result; keep this in sync with
# the Kconfig default so the soak below spans at least a couple of timer-driven
# cycles.
CLOUD_SYNC_PERIOD_SECONDS = 30.0

# The FOTA module logs this once per poll that finds no pending job, at debug level
# (CONFIG_APP_FOTA_LOG_LEVEL_DBG). Seeing it repeat proves the recurring
# fota_poll_request() path ran without crashing.
FOTA_POLL_DONE_LOG = "No FOTA job available"

# Require the connect-time poll plus at least two timer-driven cycles, so the device
# has to survive the window where the fota_poll_request assert was captured.
REQUIRED_FOTA_POLLS = 3

# Substrings that only appear when a thread hit a fatal condition (a failed zbus
# publish falling into FATAL_ERROR(), a tripped watchdog, a Zephyr fatal, or an
# unexpected reboot). Any of these after cloud connect fails the soak. Most
# modules log a publish failure without naming the channel; the FOTA module and
# the main module's sync timer name theirs.
FATAL_LOG_MARKERS = (
    "zbus_chan_pub, error:",
    "zbus_chan_pub fota_chan, error:",
    "zbus_chan_pub priv_fota_chan, error:",
    "zbus_chan_pub priv_cloud_chan, error:",
    "zbus_chan_pub main_priv_chan, error:",
    "FATAL_ERROR() macro called",
    "watchdog expired",
    "ASSERTION FAIL",
    "ZEPHYR FATAL ERROR",
    "Booting Serial Modem Host",
)


def _assert_survives_cloud_sync(dut) -> None:
    """Soak past several cloud-sync cycles, failing if the device asserts.

    fota_poll_request() reruns on every synchronization, and a transient zbus
    publish timeout there ends in FATAL_ERROR()/coredump. The provisioning happy
    path only captures the first ~10s of uptime, well short of the 30s timer, so
    the regression it guards against lands off-camera. Live through enough cycles
    to exercise the recurring publish and treat any fatal marker as a failure.
    """
    # The connect banner appears once, after provisioning; scope every check to the
    # log that follows it so a fatal marker (including a reboot banner) is caught.
    connect_seen = dut.uart.wait_for_substring(
        CLOUD_CONNECTED_LOG, timeout=CLOUD_CONNECT_TIMEOUT
    )
    logger.info("Cloud connected, soaking through cloud-sync cycles: %s", connect_seen)

    # Enough headroom for REQUIRED_FOTA_POLLS timer cycles plus poll/network jitter.
    timeout = CLOUD_SYNC_PERIOD_SECONDS * (REQUIRED_FOTA_POLLS + 1) + 60.0
    deadline = time.monotonic() + timeout
    poll_interval = 2.0

    while time.monotonic() < deadline:
        captured = dut.uart.snapshot_log()
        tail = captured.partition(CLOUD_CONNECTED_LOG)[2]

        for marker in FATAL_LOG_MARKERS:
            if marker in tail:
                offending = next(
                    (line for line in tail.splitlines() if marker in line), marker
                )
                raise AssertionError(
                    "Device reported a fatal condition during cloud sync "
                    f"(marker {marker!r}): {offending!r}"
                )

        if tail.count(FOTA_POLL_DONE_LOG) >= REQUIRED_FOTA_POLLS:
            logger.info(
                "Observed %d FOTA polls across cloud-sync cycles with no fatal errors",
                REQUIRED_FOTA_POLLS,
            )
            return

        time.sleep(poll_interval)

    raise TimeoutError(
        f"Timed out after {timeout:.0f}s waiting for {REQUIRED_FOTA_POLLS} "
        f"'{FOTA_POLL_DONE_LOG}' cycles after cloud connect"
    )


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

    # Keep capturing past the first cloud-sync cycles: the recurring
    # fota_poll_request() path asserts on a zbus publish timeout, and that window
    # is not covered by the connect check above.
    _assert_survives_cloud_sync(dut)

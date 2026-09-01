# CI and contribution

## Continuous integration

A nightly schedule on `main` runs the [Build, Test, and Release workflow](../.github/workflows/ci.yml) at 03:00 UTC (05:00 GMT+2 during CEST):

1. Resolve the next semver from commit history since the last tag
2. Build all applications and upload firmware artifacts
3. Run on-target hardware tests against the prebuilt firmware
4. Create a GitHub Release when commits since the last tag warrant a version bump (`feat`, `fix`, or `BREAKING CHANGE`)

The full pipeline can also be triggered manually from the Actions tab (`workflow_dispatch`). Individual workflows (`Build`, `Test`, `Release`) remain independently triggerable.

Pull requests run build, compliance, SonarCloud, and Markdown link checks. Releases are created from the nightly run (or manual dispatch) only when releasable commits exist; `chore`, `docs`, `ci`, and similar commits are ignored.

## Releases

Releases are tagged `vX.Y.Z` and publish one zip per CI build flavor (`{app}-{board_type}-v{version}.zip`), plus the pinned Serial Modem bundle `serial_modem_v2.0.0-preview2_nrf9151dk_extmcu.zip` for 91m1 host + nRF9151 DK setups. See [Release artifacts](release-artifacts.md) for bundle names, file descriptions, and flashing instructions.

The version is derived from conventional commit prefixes in merged commits:

| Commit prefix | Version bump |
|---------------|--------------|
| `feat:` | Minor |
| `fix:` | Patch |
| `BREAKING CHANGE` or `type!:` | Major |
| `chore:`, `docs:`, `ci:`, etc. | No release |

Firmware built for a release embeds that version via each application's [`VERSION`](../applications/91m1_ppp/VERSION) file. The CI build step overwrites `VERSION` from the resolved semver before compiling (without committing the change); Memfault reads it through NCS (`CONFIG_MEMFAULT_NCS_FW_VERSION_STATIC` defaults to `APP_VERSION_TWEAK_STRING`). The `VERSION` files checked into the repository are for local development only.

FOTA hardware tests on `main` use the same release semver as the baseline: CI passes `FIRMWARE_VERSION` to the Test workflow, flashes the Build artifact's `merged.hex` without rebuilding, then builds and deploys a patch-bumped update image (e.g. `1.2.3` → `1.2.4`) for OTA verification. Local runs fall back to `baseline_version` in [`.github/test/tests.yml`](../.github/test/tests.yml) and flash with `west flash --recover`.

Hardware tests use three rigs on two self-hosted runners (see [`.github/test/tests.yml`](../.github/test/tests.yml)):

| CI job | Runner | DUT | Every CI run |
|--------|--------|-----|--------------|
| `91m1_ppp-provision-nrf54l15-nrf91` | `self-hosted-provisioning` | Provisioning (`CI_NRF54L15_PROVISION_*`) | Recover flash, full nRF Cloud + Memfault onboard |
| `91m1_ppp-provision-nrf54lm20b-nrf91` | `self-hosted-provisioning` | Location (`CI_NRF54LM20B_PROVISION_*`) | Recover flash, full nRF Cloud + Memfault onboard (plain build) |
| `91m1_ppp-provision-location-nrf54lm20b-nrf91` | `self-hosted-provisioning` | Location (`CI_NRF54LM20B_PROVISION_*`) | Recover flash, full onboard, then Wi-Fi location data |
| `91m1_ppp-memfault-coredump-nrf54l15-nrf91` | `self-hosted-test` | Test (`CI_NRF54L15_*`) | Coredump (no re-provision) |
| `91m1_ppp-application-fota-nrf54l15-nrf91` | `self-hosted-test` | Test (`CI_NRF54L15_*`) | FOTA (queued with coredump on same runner) |
| `91m1_ppp-application-fota-nrf54lm20b-nrf91` | `self-hosted-provisioning` | Location (`CI_NRF54LM20B_PROVISION_*`) | FOTA (queued with provision jobs on same runner) |

Provisioning runs in parallel with the first queued test job on the separate runners. Jobs sharing a runner run one at a time; GitHub queues whichever job does not get the runner first (coredump and FOTA on the test runner; the three provisioning jobs plus LM20B FOTA on the provisioning runner).

DUT 3 is an nRF54LM20B DK with an [nRF7002-EB2](../applications/91m1_ppp/doc/hardware-setup.md) shield, wired to an nRF91 Serial Modem. Both lm20b tests use host console **VCOM0** (uart30): `91m1_ppp-provision-nrf54lm20b-nrf91` flashes the plain `nrf54lm20b` build and runs `test_cloud_provision`; `91m1_ppp-provision-location-nrf54lm20b-nrf91` flashes the Wi-Fi location build and verifies Wi-Fi scan plus nRF Cloud ground-fix. Local run for plain provisioning:

```shell
export REPO_ROOT=$PWD
export TEST_JSON="$(PYTHONPATH=tests/on_target python3 -m ci.catalog load 91m1_ppp-provision-nrf54lm20b-nrf91)"
PYTHONPATH=tests/on_target pytest tests/on_target/tests/test_provision/ -c tests/on_target/tests/pytest.ini -v
```

Local run for the location test:

```shell
export REPO_ROOT=$PWD
export TEST_JSON="$(PYTHONPATH=tests/on_target python3 -m ci.catalog load 91m1_ppp-provision-location-nrf54lm20b-nrf91)"
PYTHONPATH=tests/on_target pytest tests/on_target/tests/test_location/ -c tests/on_target/tests/pytest.ini -v
```

Build, compliance, and SonarCloud jobs use `self-hosted-build` (three runners for parallel matrix builds). Hardware tests and the test plan job use `self-hosted-test` on the DUT 2 rig.

### Self-hosted runner setup

Register runners with dedicated labels only. GitHub adds the default `self-hosted` label unless you override it during registration; remove that label from the provisioning and test runners so they never pick up build or cross-rig jobs.

| Runner | Labels | Purpose |
|--------|--------|---------|
| `*-build-A/B/C` | `self-hosted-build` | Firmware builds, compliance, SonarCloud |
| `*-host` | `self-hosted-test` | Coredump and FOTA on DUT 2 |
| `*-prov` | `self-hosted-provisioning` | Provisioning on DUT 1 and DUT 3, FOTA on DUT 3 |

Example registration for the provisioning rig:

```shell
# On the provisioning rig — use a new runner name, e.g. smha-provisioning
mkdir actions-runner-provisioning && cd actions-runner-provisioning
curl -o actions-runner-linux-x64-2.XXX.0.tar.gz -L https://github.com/actions/runner/releases/download/v2.XXX.0/actions-runner-linux-x64-2.XXX.0.tar.gz
tar xzf ./actions-runner-linux-x64-*.tar.gz
./config.sh --url https://github.com/<org>/<repo> --token <registration-token> \
  --name smha-provisioning --labels self-hosted-provisioning,Linux,X64 --unattended
sudo ./svc.sh install && sudo ./svc.sh start
```

Example for a build runner:

```shell
./config.sh --url https://github.com/<org>/<repo> --token <registration-token> \
  --name smha-build-a --labels self-hosted-build,Linux,X64 --unattended
```

Example for the test rig (DUT 2):

```shell
./config.sh --url https://github.com/<org>/<repo> --token <registration-token> \
  --name smha-test --labels self-hosted-test,Linux,X64 --unattended
```

Both runners need Docker and USB access to their DKs (`--privileged -v /dev:/dev` in the test workflow containers). Set the GitHub repository variables for each rig on the same repo (`CI_NRF54L15_PROVISION_*` and `CI_NRF54L15_PROVISION_SERIAL_MODEM_*` for DUT 1, `CI_NRF54L15_*` and `CI_NRF54L15_SERIAL_MODEM_*` for DUT 2, `CI_NRF54LM20B_PROVISION_*` and `CI_NRF54LM20B_PROVISION_SERIAL_MODEM_*` for DUT 3). The provisioning runner needs access to both DUT 1 and DUT 3; tests select their board by SEGGER serial number, so both DKs can share one runner host.

91m1 on-target tests flash the pinned Serial Modem release (`serial_modem_v2.0.0-preview2_nrf9151dk_extmcu.hex`) on the nRF9151 DK before programming the host.

The test DUT must be provisioned once (manually or by running the provisioning flow locally against it). CI flashes baseline firmware without recover so TF-M credentials persist. FOTA and coredump do not remove the device from nRF Cloud or Memfault after each run.

First-time setup for the test DUT (`CI_NRF54L15_*`): follow [91m1_ppp cloud provisioning](../applications/91m1_ppp/doc/README.md) steps 3–6 on that board, or run the provisioning test locally with `TEST_JSON` from `91m1_ppp-provision-nrf54l15-nrf91` while pointing the `CI_NRF54L15_PROVISION_*` variables at the test board (once only). Register the device in the shared Memfault cohort `ci-91m1-test-nrf54l15-nrf91` (used by both coredump and FOTA tests).

The nRF54LM20B FOTA test reuses DUT 3 (`CI_NRF54LM20B_PROVISION_*`) on the provisioning runner. The device must reach cloud connect before CI; the test calls `ensure_provisioned()` on first run if credentials are missing, and assigns the DUT to Memfault cohort `ci-91m1-test-nrf54lm20b-nrf91` (separate from the provision cohort). When provision and FOTA run in the same CI batch, the FOTA test moves the device into the test cohort automatically.

The nRF54LM20B build reports Memfault hardware version `smha-nrf54lm20dk` instead of the NCS default board name (see [`boards/nrf54lm20dk_nrf54lm20b_cpuapp_ns.conf`](../applications/91m1_ppp/boards/nrf54lm20dk_nrf54lm20b_cpuapp_ns.conf)). The shared Memfault project binds `nrf54lm20dk` to another software type, so OTA payloads for `smha-91m1` are rejected under that name.

Memfault coredump tests connect to nRF Cloud, trigger `mflt test busfault` over the shell, and verify a new bus fault coredump for the device appears in the Memfault Traces REST API. "New" means newer than the device's newest coredump recorded before the fault, so the check does not depend on the device clock agreeing with the runner. A bus fault is used because TF-M traps HardFaults before Memfault's handler runs. Local run:

```shell
export REPO_ROOT=$PWD
export TEST_JSON="$(PYTHONPATH=tests/on_target python3 -m ci.catalog load 91m1_ppp-memfault-coredump-nrf54l15-nrf91)"
PYTHONPATH=tests/on_target pytest tests/on_target/tests/test_memfault/ -c tests/on_target/tests/pytest.ini -v
```

Run coredump then FOTA locally (same order as CI):

```shell
export REPO_ROOT=$PWD
export TEST_JSON="$(PYTHONPATH=tests/on_target python3 -m ci.catalog load 91m1_ppp-memfault-coredump-nrf54l15-nrf91)"
PYTHONPATH=tests/on_target pytest tests/on_target/tests/test_memfault/ -c tests/on_target/tests/pytest.ini -v
export TEST_JSON="$(PYTHONPATH=tests/on_target python3 -m ci.catalog load 91m1_ppp-application-fota-nrf54l15-nrf91)"
PYTHONPATH=tests/on_target pytest tests/on_target/tests/test_fota/ -c tests/on_target/tests/pytest.ini -v
```

Local run for nRF54LM20B FOTA (plain `nrf54lm20b` build, same DUT as provision tests):

```shell
export REPO_ROOT=$PWD
export TEST_JSON="$(PYTHONPATH=tests/on_target python3 -m ci.catalog load 91m1_ppp-application-fota-nrf54lm20b-nrf91)"
PYTHONPATH=tests/on_target pytest tests/on_target/tests/test_fota/ -c tests/on_target/tests/pytest.ini -v
```

Set `NRF_CLOUD_*`, `MEMFAULT_*`, and the host plus Serial Modem `CI_NRF54L15_*` / `CI_NRF54L15_PROVISION_*` / `CI_NRF54LM20B_PROVISION_*` variables/secrets documented in [`.github/workflows/test.yml`](../.github/workflows/test.yml).

### Serial logs

Every hardware test captures two consoles in parallel and uploads both in the `hardware-serial-log-{test_id}-{run_id}` artifact:

| File | Source |
|------|--------|
| `hardware-serial.log` | Host DK console (VCOM1 on nRF54L15, VCOM0 on nRF54LM20B) |
| `modem-serial.log` | Serial Modem console on the nRF9151 / SMA DK (uart1, **VCOM1**, **1000000 baud**) |

When a test fails, the last 200 lines of both logs are also printed into the job log, so triage needs no artifact download.

The Serial Modem console runs at **1000000 baud**, not the usual 115200, and `uart0` (VCOM0) is disabled by the external-MCU overlay so VCOM0 stays silent. Both facts come from the pinned bundle's `.dts` and are recorded in [`tests/on_target/ci/serial_modem_firmware.yml`](../tests/on_target/ci/serial_modem_firmware.yml) — re-check them when bumping the pinned release, because a mismatch produces an empty log rather than an error.

Modem capture also requires **VCOM1 enabled** in Board Configurator on the nRF9151 / SMA DK, which is the documented [hardware setup](../applications/91m1_ppp/doc/hardware-setup.md). The port is resolved from the rig's `CI_*_SERIAL_MODEM_SEGGER_SN`; set the matching `CI_*_SERIAL_MODEM_SERIAL_PORT` variable to pin it explicitly. Capture is best-effort and never fails a test: if the port cannot be resolved, or resolves but stays silent, the run logs a warning and continues with host logs only.

## Commit messages

We use a title format that combines [Conventional Commits](https://www.conventionalcommits.org/) semver types with [Zephyr-style](https://docs.zephyrproject.org/latest/contribute/guidelines.html#commit-guidelines) subsystem prefixes:

```
<type>[!]: <subsystem>: [<component>:] <description>
```

Examples:

```
feat: applications: 91m1_ppp: improve FOTA and cloud sync support
fix: tests: on_target: reset dut after onboarding
ci: .github: workflows: add push orchestrator
```

Allowed types: `feat`, `fix`, `docs`, `style`, `refactor`, `perf`, `test`, `build`, `ci`, `chore`.

The commit body must include at least one line of description (max 72 characters per line) and a `Signed-off-by: Full Name <email>` footer. Use `BREAKING CHANGE:` in the body when the title includes `!`.

Pull requests enforce these rules via gitlint. All rules live in [`scripts/gitlint/commit_rules.py`](../scripts/gitlint/commit_rules.py) with configuration in [`.gitlint`](../.gitlint).

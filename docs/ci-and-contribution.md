# CI and contribution

This document describes how the repository is built, tested, and released: the nightly and pull request pipelines, how release versions are derived, the self-hosted runners and hardware rigs that run the on-target tests, and the commit message rules contributions must follow.

## Continuous integration

A nightly schedule on `main` runs the [Build, Test, and Release workflow](https://github.com/nrfconnect/ncs-serial-modem-host-applications/blob/main/.github/workflows/ci.yml) at 03:00 UTC (05:00 GMT+2 during CEST):

1. Resolve the next semver from the commit history since the last tag.
1. Resolve the newest upstream Serial Modem release that ships the external-MCU zip.
1. Build all applications and upload firmware artifacts.
1. Run on-target hardware tests against the prebuilt firmware and locked Serial Modem tag.
1. Create a GitHub Release when commits since the last tag warrant a version bump (`feat`, `fix`, or `BREAKING CHANGE`).

You can trigger the full pipeline manually from the Actions tab (`workflow_dispatch`). Individual workflows (`Build`, `Test`, `Release`) remain independently triggerable.

Pull requests run build, compliance, SonarCloud, and Markdown link checks. Releases are created from the nightly run (or manual dispatch) only when releasable commits exist; `chore`, `docs`, `ci`, and similar commits are ignored.

## Releases

Releases are tagged `vX.Y.Z` and publish one zip per CI build flavor (`{app}-{board_type}-v{version}.zip`). Each directory has a flat structure and contains the images you need, a generated `README.md` describing the content, and, for nRF91M1, the Serial Modem archive CI tested that night. See [Release artifacts](release-artifacts.md) for bundle names, file descriptions, and flashing instructions.

The version is derived from conventional commit prefixes in merged commits:

| Commit prefix | Version bump |
|---------------|--------------|
| `feat:` | Minor |
| `fix:` | Patch |
| `BREAKING CHANGE` or `type!:` | Major |
| `chore:`, `docs:`, `ci:`, and so on | No release |

Firmware built for a release embeds that version through each application's [`VERSION`](https://github.com/nrfconnect/ncs-serial-modem-host-applications/blob/main/applications/91m1_ppp/VERSION) file. The CI build step overwrites `VERSION` from the resolved semver before compiling (without committing the change). Memfault reads it through nRF Connect SDK (`CONFIG_MEMFAULT_NCS_FW_VERSION_STATIC` defaults to `APP_VERSION_TWEAK_STRING`). The `VERSION` files checked into the repository are for local development only.

FOTA hardware tests on `main` use the same release semver as the baseline: CI passes `FIRMWARE_VERSION` to the Test workflow, flashes the Build artifact's `merged.hex` without rebuilding, then builds and deploys a patch-bumped update image (for example, `1.2.3` → `1.2.4`) for OTA verification. Local runs fall back to `baseline_version` in [`.github/test/tests.yml`](https://github.com/nrfconnect/ncs-serial-modem-host-applications/blob/main/.github/test/tests.yml) and flash with `west flash --recover`.

Hardware tests use five rigs on two self-hosted runners (see [`.github/test/tests.yml`](https://github.com/nrfconnect/ncs-serial-modem-host-applications/blob/main/.github/test/tests.yml)):

| CI job | Runner | DUT | Every CI run |
|--------|--------|-----|--------------|
| `91m1_ppp-provision-nrf54l15-nrf91` | `self-hosted-provisioning` | Provisioning (`CI_NRF54L15_PROVISION_*`) | Recover flash, full nRF Cloud + Memfault onboard |
| `91m1_ppp-provision-nrf54lm20b-nrf91` | `self-hosted-provisioning` | Location (`CI_NRF54LM20B_PROVISION_*`) | Recover flash, full nRF Cloud + Memfault onboard (plain build) |
| `91m1_ppp-provision-location-nrf54lm20b-nrf91` | `self-hosted-provisioning` | Location (`CI_NRF54LM20B_PROVISION_*`) | Recover flash, full onboard, then Wi-Fi location data |
| `93m1_ppp-provision-nrf93m1` | `self-hosted-provisioning` | nRF93M1 DK (`CI_NRF93M1_PROVISION_*`) | Recover flash, full nRF Cloud + Memfault onboard |
| `91m1_ppp-memfault-coredump-nrf54l15-nrf91` | `self-hosted-test` | Test (`CI_NRF54L15_*`) | Coredump (no re-provision) |
| `91m1_ppp-application-fota-nrf54l15-nrf91` | `self-hosted-test` | Test (`CI_NRF54L15_*`) | FOTA (queued with coredump on same runner) |
| `91m1_ppp-application-fota-nrf54lm20b-nrf91` | `self-hosted-provisioning` | Location (`CI_NRF54LM20B_PROVISION_*`) | FOTA (queued with provision jobs on same runner) |
| `93m1_ppp-application-fota-nrf93m1` | `self-hosted-test` | nRF93M1 test DK (`CI_NRF93M1_*`) | FOTA (queued with coredump and FOTA on same runner) |

Provisioning runs in parallel with the first queued test job on the separate runners. Jobs sharing a runner run one at a time; GitHub queues whichever job does not get the runner first (coredump and both FOTA jobs on the test runner, the four provisioning jobs plus LM20B FOTA on the provisioning runner).

Once the release is deployed, FOTA tests run `fota poll` on the DUT shell instead of waiting up to a full sync interval for the application to poll on its own. A poll that reports no update is retried, since nRF Cloud can lag behind the Memfault deployment.

DUT 3 is an nRF54LM20B DK with an [nRF7002-EB II](applications/91m1_ppp/hardware-setup.md) shield, wired to an nRF91 Serial Modem. Both lm20b tests use host console VCOM0 (UART30): `91m1_ppp-provision-nrf54lm20b-nrf91` flashes the plain `nrf54lm20b` build and runs `test_cloud_provision`; `91m1_ppp-provision-location-nrf54lm20b-nrf91` flashes the Wi-Fi location build and verifies Wi-Fi scan plus nRF Cloud ground-fix. Local run for plain provisioning:

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

DUT 4 is an nRF93M1 DK running `93m1_ppp`, with the host console on UART20. The on-board nRF93M1 keeps the Serial Modem firmware it already has, so the test flashes only the host and captures no modem log. Local run:

```shell
export REPO_ROOT=$PWD
export TEST_JSON="$(PYTHONPATH=tests/on_target python3 -m ci.catalog load 93m1_ppp-provision-nrf93m1)"
PYTHONPATH=tests/on_target pytest tests/on_target/tests/test_provision/ -c tests/on_target/tests/pytest.ini -v
```

Build, compliance, and SonarCloud jobs use `self-hosted-build` (three runners for parallel matrix builds). Hardware tests and the test plan job use `self-hosted-test` on the DUT 2 rig.

### Self-hosted runner setup

Register runners with dedicated labels only. GitHub adds the default `self-hosted` label unless you override it during registration; remove that label from the provisioning and test runners so they never pick up build or cross-rig jobs.

| Runner | Labels | Purpose |
|--------|--------|---------|
| `*-build-A/B/C` | `self-hosted-build` | Firmware builds, compliance, SonarCloud |
| `*-host` | `self-hosted-test` | Coredump and FOTA on DUT 2, FOTA on DUT 5 |
| `*-prov` | `self-hosted-provisioning` | Provisioning on DUT 1, DUT 3 and DUT 4, FOTA on DUT 3 |

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

Both runners need Docker and USB access to their DKs (`--privileged -v /dev:/dev` in the test workflow containers). Set the GitHub repository variables for each rig on the same repo (`CI_NRF54L15_PROVISION_*` and `CI_NRF54L15_PROVISION_SERIAL_MODEM_*` for DUT 1, `CI_NRF54L15_*` and `CI_NRF54L15_SERIAL_MODEM_*` for DUT 2, `CI_NRF54LM20B_PROVISION_*` and `CI_NRF54LM20B_PROVISION_SERIAL_MODEM_*` for DUT 3, `CI_NRF93M1_PROVISION_*` for DUT 4, `CI_NRF93M1_*` for DUT 5). The provisioning runner needs access to DUT 1, DUT 3 and DUT 4, and the test runner to DUT 2 and DUT 5; tests select their board by SEGGER serial number, so the DKs can share one runner host.

91m1 on-target tests flash the newest upstream Serial Modem release that ships the external-MCU zip on the nRF9151 DK before programming the host.

The test DUT must be provisioned once (manually or by running the provisioning flow locally against it). CI flashes baseline firmware without recovery, so TF-M credentials persist. FOTA and coredump do not remove the device from nRF Cloud or Memfault after each run.

For the first-time setup for the test DUT (`CI_NRF54L15_*`): follow [91m1_ppp cloud provisioning](applications/91m1_ppp/README.md) steps 3–6 on that board, or run the provisioning test locally with `TEST_JSON` from `91m1_ppp-provision-nrf54l15-nrf91` while pointing the `CI_NRF54L15_PROVISION_*` variables at the test board (once only). Register the device in the shared Memfault cohort `ci-91m1-test-nrf54l15-nrf91` (used by both coredump and FOTA tests).

The nRF54LM20B FOTA test reuses DUT 3 (`CI_NRF54LM20B_PROVISION_*`) on the provisioning runner. The device must reach cloud connect before CI; the test calls `ensure_provisioned()` on the first run if credentials are missing and assigns the DUT to the Memfault cohort `ci-91m1-test-nrf54lm20b-nrf91` (separate from the provision cohort). When provision and FOTA run in the same CI batch, the FOTA test moves the device into the test cohort automatically.

The nRF54LM20B build reports Memfault hardware version `smha-nrf54lm20dk` instead of the NCS default board name (see [`boards/nrf54lm20dk_nrf54lm20b_cpuapp_ns.conf`](https://github.com/nrfconnect/ncs-serial-modem-host-applications/blob/main/applications/91m1_ppp/boards/nrf54lm20dk_nrf54lm20b_cpuapp_ns.conf)). The shared Memfault project binds `nrf54lm20dk` to another software type, so OTA payloads for `smha-91m1` are rejected under that name. The nRF93M1 DK build reports `smha-nrf93m1dk` for the same reason (see [`boards/nrf93m1dk_nrf54l15_cpuapp_ns.conf`](https://github.com/nrfconnect/ncs-serial-modem-host-applications/blob/main/applications/93m1_ppp/boards/nrf93m1dk_nrf54l15_cpuapp_ns.conf)): `nrf93m1dk` is bound to another software type, so OTA payloads for `smha-93m1` are rejected under that name.

Memfault coredump tests connect to nRF Cloud, trigger `mflt test busfault` over the shell, and verify a new bus fault coredump for the device appears in the Memfault Traces REST API. "New" means newer than the device's newest coredump recorded before the fault, so the check does not depend on the device clock agreeing with the runner. A bus fault is used because TF-M traps hard Faults before the Memfault's handler runs. Local run:

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

The nRF93M1 FOTA test runs on its own nRF93M1 DK (DUT 5, `CI_NRF93M1_*`, host console on VCOM0) in Memfault cohort `ci-93m1-test-nrf93m1`. Like the nRF54LM20B FOTA test, it provisions the board through `ensure_provisioned()` on the first run. Local run:

```shell
export REPO_ROOT=$PWD
export TEST_JSON="$(PYTHONPATH=tests/on_target python3 -m ci.catalog load 93m1_ppp-application-fota-nrf93m1)"
PYTHONPATH=tests/on_target pytest tests/on_target/tests/test_fota/ -c tests/on_target/tests/pytest.ini -v
```

Set `NRF_CLOUD_*`, `MEMFAULT_*`, and the host plus Serial Modem `CI_NRF54L15_*` / `CI_NRF54L15_PROVISION_*` / `CI_NRF54LM20B_PROVISION_*` / `CI_NRF93M1_PROVISION_*` / `CI_NRF93M1_*` variables/secrets documented in [`.github/workflows/test.yml`](https://github.com/nrfconnect/ncs-serial-modem-host-applications/blob/main/.github/workflows/test.yml).

### Serial logs

Every hardware test captures two consoles in parallel and uploads both in the `hardware-serial-log-{test_id}-{run_id}` artifact:

| File | Source |
|------|--------|
| `hardware-serial.log` | Host DK console (VCOM1 on nRF54L15, VCOM0 on nRF54LM20B) |
| `modem-serial.log` | Serial Modem console on the nRF9151 or nRF9151 SMA DK (UART1, VCOM1, 1000000 baud) |

When a test fails, the last 200 lines of both logs are also printed into the job log, so triage needs no artifact download.

#### Why the modem console needs `AT#XLOG=1`

Serial Modem prints its bootloader and application banners over `uart1` and then suspends the log backend and the UART to avoid the roughly 700 uA overhead of keeping it active. Without further action the modem log therefore stops after about 0.65 s of uptime and contains nothing from the phase a test actually exercises.

`AT#XLOG=1` resumes it, and the tests issue that command through the host's `modem at` shell once the host reports `Cloud connected`. That is all the released image needs: its default `INF` levels carry the PPP, PDN, and AT events triage relies on.

Three further consequences worth knowing:

- The command needs the CMUX AT pipe, which only exists after the modem attaches and which CMUX runtime power save closes again after its idle timeout. `enable_modem_application_logs()` therefore retries and confirms the modem replied `OK`, warning if it never succeeds.
- Anything that reboots the host also pulses modem nRESET, which resets the modem and turns logging back off, so the command is reissued after each reboot (for example, after a FOTA update is applied).
- Reissuing it only works if the image now running has the command. FOTA tests update to a *released* build, and one produced before `CONFIG_MODEM_AT_SHELL` was enabled answers `modem: command not found`, so those runs log a warning and their modem console holds nothing after the update. That resolves itself once a release containing the option exists.

#### Requirements and failure modes

The Serial Modem console runs at 1000000 baud, not the usual 115200. It is sized for modem traces. `console_baudrate` is recorded in [`tests/on_target/ci/serial_modem_firmware.yml`](https://github.com/nrfconnect/ncs-serial-modem-host-applications/blob/main/tests/on_target/ci/serial_modem_firmware.yml), re-check it when upstream changes the overlay, because a baud mismatch produces an empty log rather than an error.

Modem capture also requires VCOM1 enabled in Board Configurator on the nRF9151 or nRF9151 SMA DK, which is the documented [hardware setup](applications/91m1_ppp/hardware-setup.md).
The same setup disconnects VCOM0 and VCOM0 HWFC, because the nRF91M1 build puts the host AT link on `uart0`, the pins VCOM0 otherwise occupies. A rig that skips either switch fails with `init_chat_script: timed out`. The port is resolved from the rig's `CI_*_SERIAL_MODEM_SEGGER_SN`; set the matching `CI_*_SERIAL_MODEM_SERIAL_PORT` variable to pin it explicitly.

Capture starts immediately after the modem is programmed, so the boot that programming triggers is always recorded even on rigs where the host nRESET line is not wired. Everything here is best-effort and never fails a test: if the port cannot be resolved, stays silent, or the modem never accepts `AT#XLOG=1`, the run warns and continues with host logs only. Use those warnings to tell the cases apart, a completely empty `modem-serial.log` points at VCOM1 or the baud rate, while a log holding only boot output means either `AT#XLOG=1` never got through or the captured board is not the one the host is talking to, which the next section covers.

#### The wrong nRF91 DK

Several nRF91 DKs share a runner, so a rig whose `CI_*_SERIAL_MODEM_SEGGER_SN` names the wrong board is the hardest failure to spot, nothing errors. That board is programmed and captured while the host talks to a different one, so the log holds the boot that programming triggered and then nothing. `AT#XLOG=1` is still acknowledged, and `AT#XLOG?` still reads back `1`, because both travel over the host's CMUX pipe to the board actually attached, and the test passes because that board does the work. Two nRF54L15 rigs had their SEGGER SNs swapped this way, which is why they never logged past boot while the nRF54LM20 rig did.

Identities are therefore compared rather than assumed. `AT+CGSN=1` gives the IMEI of the modem the host is attached to, Serial Modem prints its own IMEI during boot before suspending the console, and a mismatch logs an error naming both IMEIs and the variable to correct. When that fires, find the right serial number by matching the IMEI the host reports against the boot output each candidate DK produces.

#### Serial Modem release

The modem image is never built from source here. Nightly CI resolves the newest published [Serial Modem](https://github.com/nrfconnect/ncs-serial-modem/releases) release that ships the nRF91M1 bundle (`*_nrf9151dk_nrf91m1.zip`, including prereleases), locks that tag for the whole pipeline run, and passes it to every nRF91M1 hardware test and to the Release workflow. Tests download the archive, cache it under `build/serial-modem-firmware/<tag>/`, and flash the `.hex` from it. Release bundles copy the same archive unextracted into every `91m1_ppp` zip, so what ships is what CI tested that night.

The Serial Modem uses the nRF91M1 UART pinout. The bench is wired for it. See [Hardware Setup](applications/91m1_ppp/hardware-setup.md).

Set `SERIAL_MODEM_RELEASE` to pin a specific upstream tag when running tests or the release workflow locally. Static settings such as `console_baudrate` live in [`tests/on_target/ci/serial_modem_firmware.yml`](https://github.com/nrfconnect/ncs-serial-modem-host-applications/blob/main/tests/on_target/ci/serial_modem_firmware.yml).

Full LTE and IP-level modem traces (`AT#XTRACE=1`) are a further step still, and they need a trace database to decode. Note that the UART trace backend shares `uart1` with the log backend, and the two are mutually exclusive, so capturing traces that way costs the application log; `overlay-trace-backend-cmux.conf` routes traces over a dedicated CMUX channel instead and leaves `AT#XLOG=1` usable.

## Documentation diagrams

The diagrams in the application documentation are PlantUML sources kept next to their rendered SVGs in `docs/applications/*/diagrams/`, sharing the `style.puml` skin in the same folder. Edit the `.puml`, re-render, and commit both files:

```shell
scripts/render_diagrams.py
```

Rendering uses a PlantUML server, so you do not need a local Java installation, and `!include` lines are resolved before the source is sent. Pass paths to render only some diagrams, and `--server` to use a local server instead of the public one:

```shell
docker run -d -p 8080:8080 plantuml/plantuml-server:jetty
scripts/render_diagrams.py --server http://localhost:8080
```

A source that does not parse fails with the error and line number the server reports. The notation the diagrams use is documented in [Diagram notation](applications/91m1_ppp/architecture.md#diagram-notation).

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

Pull requests enforce these rules via gitlint. All rules live in [`scripts/gitlint/commit_rules.py`](https://github.com/nrfconnect/ncs-serial-modem-host-applications/blob/main/scripts/gitlint/commit_rules.py) with configuration in [`.gitlint`](https://github.com/nrfconnect/ncs-serial-modem-host-applications/blob/main/.gitlint).

# CI and contribution

## Continuous integration

Pushes to `main` run the [CI workflow](../.github/workflows/ci.yml):

1. Resolve the next semver from commit history since the last tag
2. Build all applications and upload firmware artifacts
3. Run on-target hardware tests against the prebuilt firmware
4. Create a GitHub Release when commits warrant a version bump (`feat`, `fix`, or `BREAKING CHANGE`)

Individual workflows (`Build`, `Test`, `Release`) can also be triggered manually from the Actions tab.

Pull requests run build, compliance, SonarCloud, and Markdown link checks. Releases are created only from pushes to `main`.

## Releases

Releases are tagged `vX.Y.Z` and include signed firmware binaries (`.signed.bin`) and ELF files (`.elf`) for each application. The version is derived from conventional commit prefixes in merged commits:

| Commit prefix | Version bump |
|---------------|--------------|
| `feat:` | Minor |
| `fix:` | Patch |
| `BREAKING CHANGE` or `type!:` | Major |
| `chore:`, `docs:`, `ci:`, etc. | No release |

Firmware built for a release embeds that version via each application's [`VERSION`](../applications/91m1_ppp/VERSION) file. The CI build step overwrites `VERSION` from the resolved semver before compiling (without committing the change); Memfault reads it through NCS (`CONFIG_MEMFAULT_NCS_FW_VERSION_STATIC` defaults to `APP_VERSION_TWEAK_STRING`). The `VERSION` files checked into the repository are for local development only.

Note that a device therefore reports its version as `1.2.3+0`, while Memfault stores it as `1.2.3 0`: the `+` becomes a space somewhere in the nRF Cloud forwarding path, so the version we upload symbol files and OTA releases under never matches the version recorded on the device. Build IDs, not versions, are what Memfault uses to match a coredump to its symbol file, so this is cosmetic today.

FOTA hardware tests on `main` use the same release semver as the baseline: CI passes `FIRMWARE_VERSION` to the Test workflow, flashes the Build artifact's `merged.hex` without rebuilding, then builds and deploys a patch-bumped update image (e.g. `1.2.3` → `1.2.4`) for OTA verification. Local runs fall back to `baseline_version` in [`.github/test/tests.yml`](../.github/test/tests.yml) and flash with `west flash --recover`.

Hardware tests use two nRF54L15 + nRF91 rigs on two self-hosted runners (see [`.github/test/tests.yml`](../.github/test/tests.yml)):

| CI job | Runner | DUT | Every CI run |
|--------|--------|-----|--------------|
| `91m1_ppp-provision-nrf54l15-nrf91` | `self-hosted-provisioning` | Provisioning (`CI_NRF54L15_PROVISION_*`) | Recover flash, full nRF Cloud + Memfault onboard |
| `91m1_ppp-memfault-coredump-nrf54l15-nrf91` | `self-hosted-test` | Test (`CI_NRF54L15_*`) | Coredump (no re-provision) |
| `91m1_ppp-application-fota-nrf54l15-nrf91` | `self-hosted-test` | Test (`CI_NRF54L15_*`) | FOTA (queued with coredump on same runner) |

Provisioning runs in parallel with the first queued test job on the separate runners. Coredump and FOTA share the test runner and run one at a time; GitHub queues whichever job does not get the runner first.

Build, compliance, and SonarCloud jobs use `self-hosted-build` (three runners for parallel matrix builds). Hardware tests and the test plan job use `self-hosted-test` on the DUT 2 rig.

### Self-hosted runner setup

Register runners with dedicated labels only. GitHub adds the default `self-hosted` label unless you override it during registration; remove that label from the provisioning and test runners so they never pick up build or cross-rig jobs.

| Runner | Labels | Purpose |
|--------|--------|---------|
| `*-build-A/B/C` | `self-hosted-build` | Firmware builds, compliance, SonarCloud |
| `*-host` | `self-hosted-test` | Coredump and FOTA on DUT 2 |
| `*-prov` | `self-hosted-provisioning` | Provisioning test on DUT 1 |

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

Both runners need Docker and USB access to their DK (`--privileged -v /dev:/dev` in the test workflow containers). Set the GitHub repository variables for each rig on the same repo (`CI_NRF54L15_PROVISION_*` for DUT 1, `CI_NRF54L15_*` for DUT 2).

The test DUT must be provisioned once (manually or by running the provisioning flow locally against it). CI flashes baseline firmware without recover so TF-M credentials persist. FOTA and coredump do not remove the device from nRF Cloud or Memfault after each run.

First-time setup for the test DUT (`CI_NRF54L15_*`): follow [91m1_ppp cloud provisioning](../applications/91m1_ppp/doc/README.md) steps 3–6 on that board, or run the provisioning test locally with `TEST_JSON` from `91m1_ppp-provision-nrf54l15-nrf91` while pointing the `CI_NRF54L15_PROVISION_*` variables at the test board (once only). Register the device in the shared Memfault cohort `ci-91m1-test-nrf54l15-nrf91` (used by both coredump and FOTA tests).

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

Set `NRF_CLOUD_*`, `MEMFAULT_*`, and the `CI_NRF54L15_*` / `CI_NRF54L15_PROVISION_*` variables/secrets documented in [`.github/workflows/test.yml`](../.github/workflows/test.yml).

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

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

FOTA hardware tests on `main` use the same release semver as the baseline: CI passes `FIRMWARE_VERSION` to the Test workflow, flashes the Build artifact's `merged.hex` without rebuilding, then builds and deploys a patch-bumped update image (e.g. `1.2.3` → `1.2.4`) for OTA verification. Local runs fall back to `baseline_version` in [`.github/test/tests.yml`](../.github/test/tests.yml) and flash with `west flash --recover`.

Memfault coredump hardware tests provision the DUT, connect to nRF Cloud, trigger `mflt test hardfault` over the shell, and verify a new HardFault trace for the device appears in Memfault via the REST API. Coredumps are stored in a 32 KiB RRAM partition on the nRF54L15. The test uses a dedicated Memfault cohort (`ci-91m1-coredump-nrf54l15`) defined in [`.github/test/tests.yml`](../.github/test/tests.yml). Local run:

```shell
export REPO_ROOT=$PWD
export TEST_JSON="$(PYTHONPATH=tests/on_target python3 -m ci.catalog load 91m1_ppp-memfault-coredump-nrf54l15)"
PYTHONPATH=tests/on_target pytest tests/on_target/tests/test_memfault/ -c tests/on_target/tests/pytest.ini -v
```

Set the same `NRF_CLOUD_*`, `MEMFAULT_*`, and `CI_NRF54L15_*` variables/secrets as the FOTA test.

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

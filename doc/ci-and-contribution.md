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

Firmware built for a release embeds that version via each application's [`VERSION`](../applications/91m1_ppp/VERSION) file. The CI build step writes `VERSION` from the resolved semver before compiling; Memfault reads it through NCS (`CONFIG_MEMFAULT_NCS_FW_VERSION_STATIC` defaults to `APP_VERSION_TWEAK_STRING`). On release, the workflow commits the updated `VERSION` files back to `main`.

FOTA hardware tests use the same mechanism: baseline and update images are built after writing `VERSION` with the semver values from [`.github/test/tests.yml`](../.github/test/tests.yml).

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

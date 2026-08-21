# Release artifacts

This document describes the pre-built firmware published on each GitHub release. Each zip bundles one application and board-type combination built by CI from the matching NCS version pinned in [`west.yml`](../west.yml).

## Downloading releases

Pre-built firmware and release notes are published here:

[https://github.com/nrfconnect/ncs-serial-modem-host-applications/releases](https://github.com/nrfconnect/ncs-serial-modem-host-applications/releases)

Download the zip that matches your application and hardware setup, extract it, and flash `merged.hex` with a J-Link debugger or `nrfutil device program`. See [Flashing a release](#flashing-a-release) below.

Release version `vX.Y.Z` matches the semver embedded in the firmware (`X.Y.Z+0` in the device boot log).

## Release bundles

Each release contains one zip per CI build flavor:

| **Release zip** | **Application** | **Hardware** | **Description** |
|-----------------|-----------------|--------------|-----------------|
| `91m1_ppp-nrf54l15-v{VERSION}.zip` | [91m1_ppp](../applications/91m1_ppp/) | nRF54L15 DK + nRF9151 Serial Modem | Standard PPP host; nRF Cloud CoAP/DTLS on the host |
| `91m1_ppp-nrf54lm20b-v{VERSION}.zip` | [91m1_ppp](../applications/91m1_ppp/) | nRF54LM20B DK + nRF9151 Serial Modem | Same feature set on nRF54LM20B |
| `91m1_ppp-nrf54lm20b-location-v{VERSION}.zip` | [91m1_ppp](../applications/91m1_ppp/) | nRF54LM20B DK + nRF7002-EB2 + Serial Modem | Wi-Fi scan for cloud-assisted location |
| `93m1_ppp-nrf93m1-v{VERSION}.zip` | [93m1_ppp](../applications/93m1_ppp/) | nRF93M1 DK | PPP host for the integrated nRF93M1 modem |
| `93m1_at-nrf93m1-v{VERSION}.zip` | [93m1_at](../applications/93m1_at/) | nRF93M1 DK | AT host; modem terminates the cloud connection |

Replace `{VERSION}` with the release tag without the `v` prefix (for example `1.2.3` for tag `v1.2.3`).

91m1 host setups also require Serial Modem firmware on the nRF9151 / SMA DK — see [Serial Modem firmware (nRF9151 DK)](#serial-modem-firmware-nrf9151-dk) below.

## Files inside each zip

Every bundle contains the same logical file set. File names are fixed so scripts and documentation stay valid after extraction.

| **File** | **Description** | **Use case** |
|----------|-----------------|--------------|
| `merged.hex` | Full sysbuild image (TF-M, MCUboot where applicable, signed application, partition layout). Same contents as `build/merged.hex` from a local sysbuild. | Programming the device with a J-Link debugger or `nrfutil` |
| `zephyr.elf` | ELF file with debug symbols | Debugging, coredump analysis, Memfault symbol upload, `addr2line` |
| `.config` | Kconfig snapshot from the release build | Inspecting which options were enabled without rebuilding |
| `zephyr.signed.bin` | MCUboot-signed application image | FOTA through Memfault / nRF Cloud (not included in `93m1_at` bundles) |

### `merged.hex`

Use this for first-time programming or when you need to replace the full flash contents, including bootloader and secure partitions. Host applications (`91m1_ppp`, `93m1_ppp`) are built with sysbuild, TF-M, and MCUboot; the merged image reflects that layout.

After flashing, follow the application guide for hardware setup and cloud onboarding:

- [91m1_ppp documentation](../applications/91m1_ppp/doc/README.md)
- [93m1_ppp documentation](../applications/93m1_ppp/doc/README.md)
- [93m1_at documentation](../applications/93m1_at/doc/README.md)

### `zephyr.elf`

Upload this to Memfault **once per release build** so coredumps decode correctly. The GNU build ID logged at boot must match the symbol file. See [Memfault remote debugging](../applications/91m1_ppp/doc/memfault.md) for the 91m1_ppp flow.

### `.config`

Useful when comparing behavior between releases or confirming that a feature (location, Memfault, FOTA, and so on) was enabled in the build you downloaded.

### `zephyr.signed.bin`

Signed application-only payload for over-the-air updates. CI FOTA tests build a patch-bumped copy locally; production OTA is typically managed through Memfault releases linked to your nRF Cloud project. Not produced for `93m1_at`, which does not ship a signed update image in CI.

## Serial Modem firmware (nRF9151 DK)

91m1 host applications (`91m1_ppp` on nRF54L15 or nRF54LM20B) need a separate Serial Modem image on the wired nRF9151 / SMA DK. Each SMHA release includes the pinned upstream bundle:

| **Release asset** | **Hardware** | **Description** |
|-------------------|--------------|-----------------|
| [`serial_modem_v2.0.0-preview2_nrf9151dk_extmcu.zip`](https://github.com/nrfconnect/ncs-serial-modem/releases/download/v2.0.0-preview2/serial_modem_v2.0.0-preview2_nrf9151dk_extmcu.zip) | nRF9151 / SMA DK | PPP + CMUX on uart2 for an external host MCU |

Source release: [ncs-serial-modem v2.0.0-preview2](https://github.com/nrfconnect/ncs-serial-modem/releases/tag/v2.0.0-preview2). The pin is recorded in [`tests/on_target/ci/serial_modem_firmware.yml`](../tests/on_target/ci/serial_modem_firmware.yml).

### Files inside the Serial Modem zip

| **File** | **Description** | **Use case** |
|----------|-----------------|--------------|
| `serial_modem_v2.0.0-preview2_nrf9151dk_extmcu.hex` | Full flash image for the external-MCU variant | Program the nRF9151 / SMA DK before bringing up the host |
| `serial_modem_v2.0.0-preview2_nrf9151dk_extmcu.elf` | ELF with debug symbols | Modem-side debugging and trace decode |
| `serial_modem_v2.0.0-preview2_nrf9151dk_extmcu.config` | Kconfig snapshot | Inspect modem build options |
| `serial_modem_v2.0.0-preview2_nrf9151dk_extmcu.signed.bin` | Signed application image | MCUboot application updates on the modem |
| `serial_modem_v2.0.0-preview2_nrf9151dk_extmcu_mcuboot_s0.signed.bin`, `..._s1.signed.bin` | MCUboot slot images | Bootloader maintenance |
| `serial_modem_v2.0.0-preview2_nrf9151dk_extmcu_dfu_*.zip` | DFU packages | USB serial DFU without a debugger |
| `serial_modem_v2.0.0-preview2_nrf9151dk_extmcu.dts` | Devicetree snapshot | Reference for pin and partition layout |

Flash the `.hex` on the **Serial Modem DK** first, then flash the host bundle (`91m1_ppp-*`) on the **host DK**:

```shell
nrfutil device program --firmware serial_modem_v2.0.0-preview2_nrf9151dk_extmcu.hex --recover
```

See [91m1_ppp hardware setup](../applications/91m1_ppp/doc/hardware-setup.md#serial-modem-firmware) for wiring and Board Configurator settings.

## Flashing a release

Extract the zip, then flash `merged.hex` from the extracted directory.

**West** (from an initialized NCS workspace with the SEGGER J-Link connected):

```shell
west flash --hex-file merged.hex --recover
```

**nrfutil**:

```shell
nrfutil device program --firmware merged.hex --recover
```

Use `--recover` on first flash or when TF-M / credential storage must be reset. For routine re-flash during development, omit `--recover` if you want to keep TF-M Protected Storage credentials.

For 91m1 two-board setups, flash the Serial Modem `.hex` on the nRF9151 DK before programming the host `merged.hex`.

### Console port after flashing

| Bundle | Typical console |
|--------|-----------------|
| `91m1_ppp-nrf54l15-*` | Host DK VCOM1 |
| `91m1_ppp-nrf54lm20b-*` | nRF54LM20B DK VCOM0 |
| `91m1_ppp-nrf54lm20b-location-*` | nRF54LM20B DK VCOM0 |
| `93m1_ppp-nrf93m1-*`, `93m1_at-nrf93m1-*` | nRF93M1 DK default console |

See each application's [hardware setup](../applications/91m1_ppp/doc/hardware-setup.md) guide for wiring and serial port details.

## Building locally instead

Release zips are convenience binaries. To modify firmware or match an unreleased commit, build from source in an NCS workspace:

```shell
cd applications/<app>
west build -b <board> -p
west flash --recover
```

Board identifiers match CI; see [CI and contribution](ci-and-contribution.md) and each application's documentation for build arguments (for example Wi-Fi location overlays on `91m1_ppp`).

## Related documentation

- [CI and contribution](ci-and-contribution.md) — how releases are versioned and published
- [91m1_ppp Memfault](../applications/91m1_ppp/doc/memfault.md) — symbol upload and coredump workflow

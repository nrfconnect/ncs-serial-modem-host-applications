# Release artifacts

This document describes the pre-built firmware published on each GitHub release. Each zip file bundles one application and board-type combination built by CI from the matching nRF Connect SDK version pinned in [`west.yml`](https://github.com/nrfconnect/ncs-serial-modem-host-applications/blob/main/west.yml).

## Downloading releases

Pre-built firmware and release notes are published on the [releases page](https://github.com/nrfconnect/ncs-serial-modem-host-applications/releases).

Download the zip file that matches your application and hardware setup, extract it, and flash `merged.hex` with a J-Link debugger or `nrfutil device program`. See [Flashing a release](#flashing-a-release).

Release version `vX.Y.Z` matches the semantic version embedded in the firmware (`X.Y.Z+0` in the device boot log).

## Release bundles

Each release contains one zip file per CI build flavor:

| **Release zip** | **Application** | **Hardware** | **Description** |
|-----------------|-----------------|--------------|-----------------|
| `ppp-nrf54l15-v{VERSION}.zip` | [ppp](https://github.com/nrfconnect/ncs-serial-modem-host-applications/blob/main/applications/ppp/) | nRF54L15 DK + nRF9151 DK | Standard PPP host; nRF Cloud CoAP/DTLS on the host |
| `ppp-nrf54lm20b-v{VERSION}.zip` | [ppp](https://github.com/nrfconnect/ncs-serial-modem-host-applications/blob/main/applications/ppp/) | nRF54LM20B DK + nRF9151 DK | Same feature set on nRF54LM20B |
| `ppp-nrf54lm20b-location-v{VERSION}.zip` | [ppp](https://github.com/nrfconnect/ncs-serial-modem-host-applications/blob/main/applications/ppp/) | nRF54LM20B DK + nRF7002-EB II + Serial Modem | Wi-Fi® scan for cloud-assisted location |
| `ppp-nrf93m1-v{VERSION}.zip` | [ppp](https://github.com/nrfconnect/ncs-serial-modem-host-applications/blob/main/applications/ppp/) | nRF93M1 DK | PPP host for the integrated nRF93M1 modem |
| `93m1_at-nrf93m1-v{VERSION}.zip` | [93m1_at](https://github.com/nrfconnect/ncs-serial-modem-host-applications/blob/main/applications/93m1_at/) | nRF93M1 DK | AT host; modem terminates the cloud connection |
| `91m1_at-nrf54lm20b-v{VERSION}.zip` | [91m1_at](https://github.com/nrfconnect/ncs-serial-modem-host-applications/blob/main/applications/91m1_at/) | nRF54LM20B DK + nRF9151 DK | AT host; Serial Modem onboards itself and terminates the cloud connection |

Replace `{VERSION}` with the release tag without the `v` prefix (for example `1.2.3` for tag `v1.2.3`).

The nRF54 host setups also require Serial Modem firmware on the nRF9151 or nRF9151 SMA DK. See [Serial Modem firmware (nRF9151 DK)](#serial-modem-firmware-nrf9151-dk).

## Inside each zip file

Everything sits at the top level, with no nested directories:

```text
ppp-nrf54l15-v1.2.3/
├── README.md                                          what each file here is for
├── merged.hex                                         flash this on the host DK
├── zephyr.elf
├── zephyr.signed.bin
├── dfu_application.zip
├── .config
└── serial_modem_v{VERSION}_nrf9151dk_nrf91m1.zip      firmware for the companion nRF9151 / SMA DK
```

The `ppp-nrf93m1` bundles carry no modem archive, because their board has an integrated modem. The AT host bundles (`93m1_at` and `91m1_at`) contain neither `zephyr.signed.bin` nor `dfu_application.zip`.

### Top-level files

| **File** | **Description** | **Use case** |
|----------|-----------------|--------------|
| `README.md` | Generated per bundle: the files it contains, flash commands, and console port | Flashing a downloaded zip without opening this page |
| `merged.hex` | Full sysbuild image (TF-M, MCUboot where applicable, signed application, partition layout). Same contents as `build/merged.hex` from a local sysbuild. | Programming the device with a J-Link debugger or `nrfutil` |
| `zephyr.elf` | ELF file with debug symbols | Debugging, coredump analysis, nRF Cloud symbol upload, `addr2line` |
| `zephyr.signed.bin` | MCUboot-signed application image | OTA payload for an nRF Cloud FOTA release |
| `dfu_application.zip` | DFU package wrapping the signed image with its manifest | nRF Cloud FOTA jobs |
| `.config` | Kconfig snapshot from the release build | Inspecting which options were enabled without rebuilding |
| `serial_modem_*_nrf9151dk_nrf91m1.zip` | Serial Modem release tested by CI that night, exactly as published upstream | Programming the companion DK (nRF54 host bundles only) |

Upload `zephyr.elf` to nRF Cloud once per release build so coredumps decode correctly. The GNU build ID logged at boot must match the symbol file. See [Remote debugging with nRF Cloud](applications/ppp/remote-debugging.md).

For other build outputs, build the same tag locally as described in [Building locally instead](#building-locally-instead).

## Serial Modem firmware (nRF9151 DK)

The PPP Host Application on the nRF54L15 or nRF54LM20B DK needs a Serial Modem image on the wired nRF9151 or nRF9151 SMA DK, configured for PPP and CMUX on UART0. Every nRF54 host `ppp` bundle carries the upstream archive that CI tested it against that night.

Each bundle's `README.md` names the Serial Modem version and links to its [upstream release](https://github.com/nrfconnect/ncs-serial-modem/releases).

Extract it, flash the `.hex` file on the Serial Modem DK first, then flash the host bundle's `merged.hex` on the host DK:

```shell
unzip serial_modem_v<tag>_nrf9151dk_nrf91m1.zip
nrfutil device recover --serial-number <modem_dk_serial_number>
nrfutil device program \
  --firmware serial_modem_v<tag>_nrf9151dk_nrf91m1.hex \
  --serial-number <modem_dk_serial_number>
nrfutil device reset --serial-number <modem_dk_serial_number>
```

Replace `<tag>` with the version named in the bundle `README.md` (for example `2.0.0-preview3`), and `<modem_dk_serial_number>` with the serial number that `nrfutil device list` shows for the nRF9151 DK.

See [Set up the hardware](getting-started/ppp-nrf54.md#step-3-set-up-the-hardware) in the nRF54 PPP getting-started guide for wiring and Board Configurator settings.

## Flashing a release

Extract the zip file, then flash `merged.hex` from the extracted directory.

To flash with west from an initialized nRF Connect SDK workspace with the SEGGER J-Link connected, run the following command:

```shell
west flash --hex-file merged.hex --recover
```

To flash with nRF Util, run the following commands, replacing `<serial_number>` with the serial number that `nrfutil device list` shows for the DK:

```shell
nrfutil device recover --serial-number <serial_number>
nrfutil device program --firmware merged.hex --serial-number <serial_number>
nrfutil device reset --serial-number <serial_number>
```

Recover on the first flash or when TF-M or credential storage must be reset. For routine re-flash during development, skip `--recover` and `nrfutil device recover` if you want to keep TF-M Protected Storage credentials.

For the nRF54 host two-board setups, extract the bundled Serial Modem archive and flash its `.hex` file on the nRF9151 DK before programming the host `merged.hex`.

### Console port after flashing

| Bundle | Typical console |
|--------|-----------------|
| `ppp-nrf54l15-*` | Host DK VCOM1 |
| `ppp-nrf54lm20b-*` | nRF54LM20B DK VCOM0 |
| `ppp-nrf54lm20b-location-*` | nRF54LM20B DK VCOM0 |
| `91m1_at-nrf54lm20b-*` | nRF54LM20B DK VCOM0 |
| `ppp-nrf93m1-*`, `93m1_at-nrf93m1-*` | nRF93M1 DK VCOM0 |

See the [getting-started guide](getting-started/index.md#step-1-choose-your-application) for your application for wiring and serial port details.

## Building locally instead

To modify the firmware or match an unreleased commit, set up a workspace as described in [Set up the repository and toolchain](getting-started/setup.md) and build from source:

```shell
cd applications/<app>
west build -b <board> -p
west flash --recover
```

Board identifiers match CI. Each application's [getting-started guide](getting-started/index.md#step-1-choose-your-application) lists its board targets and build arguments (for example the Wi-Fi location overlays on `ppp`).

## Related documentation

- [CI and contribution](ci-and-contribution.md) – How releases are versioned and published.
- [PPP Host Application's remote debugging](applications/ppp/remote-debugging.md) – Symbol upload and coredump workflow.

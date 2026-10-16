# nRF93M1 PPP Host Application

The [nRF93M1 PPP Host Application](https://github.com/nrfconnect/ncs-serial-modem-host-applications/blob/main/applications/93m1_ppp/) runs on the nRF54L15 host of the [nRF93M1 DK](https://www.nordicsemi.com/Products/Development-hardware/nRF93M1-DK), with cellular over the on-board nRF93M1 Serial Modem through PPP. The host terminates DTLS and CoAP itself. PPP just carries IP traffic between the host's network stack and the modem's cellular radio, and the modem's own AT-based cloud client is not used. The application targets the non-secure (TF-M) build and stores the host's TLS credentials in Protected Storage.

Telemetry, location, and FOTA go to nRF Cloud over CoAP. Location fixes still pull raw cell and Wi-Fi® scan data out of the modem with AT commands, but the actual cloud request is host-side CoAP. Diagnostics go to your nRF Cloud project.

> [!TIP]
> New here? Follow [Get started with the nRF93M1 PPP Host Application](../../getting-started/93m1-ppp.md) to build, flash, and connect to nRF Cloud. This page is the reference for the application.

## Supported hardware

| Hardware | Board target | Host console |
|----------|--------------|--------------|
| nRF93M1 DK | `nrf93m1dk/nrf54l15/cpuapp/ns` | VCOM0 (`uart20`) |

## Modules

The application follows the shared [Architecture](../../architecture.md). It consists of the Main, Network, Cloud, FOTA, Location, Battery, Buttons, and Modem AT modules. See [Modules per application](../../architecture.md#modules-per-application) for what each one does in this application.

## Cloud synchronization

The Main module runs a cloud synchronization shortly after the network connects, and then every `CONFIG_APP_MAIN_SYNC_INTERVAL_SECONDS` (default 600 seconds). Each synchronization runs through the following steps, waiting for each to finish before starting the next:

1. Uploads heartbeats and diagnostics to nRF Cloud.
1. Requests a location fix from cell and Wi-Fi measurements.
1. Polls nRF Cloud for a firmware update. Without a pending update, it logs `No FOTA update available`. With one, it downloads the update and reboots to apply it.

If no cloud session can be established, the synchronization logs `No cloud session this cycle, skipping location/FOTA` and waits for the next interval.

## Buttons and shell commands

| Input | Action |
|-------|--------|
| **Button 1** or `button 1` | Start a cloud synchronization right away. |
| **Button 2** or `button 2` | Upload heartbeats and diagnostics right away. |
| `at "<command>"` | Send an AT command to the modem and print the response. |
| `network connect` / `network disconnect` | Bring the cellular link up or down. |
| `location fix`, `location cell`, `location wifi` | Request a combined, cell-only, or Wi-Fi-only location fix now. |
| `fota poll` / `fota cancel` | Check nRF Cloud for a pending firmware update, or cancel an ongoing download. |

## Credentials

The host uses the same nRF Cloud credential scheme as the nRF91M1 PPP Host Application. The Device ID comes from the nRF54L15 hardware ID, not the modem UUID, and the sec tag is `16842753`. See [Credentials explained](../91m1_ppp/README.md#credentials-explained) for what the `device_credentials_installer` options do.

`west flash --recover`, `west flash --erase`, and `nrfutil device recover` wipe Protected Storage, so the credentials must be installed again afterwards. Use a plain `west flash` to keep them.

## Firmware updates and Memfault

The application supports host firmware updates over the air through nRF Cloud. See [Application FOTA](../../app-fota.md), which uses this application in its examples.

Memfault data goes through the nRF Cloud CoAP session (`CONFIG_MEMFAULT_USE_NRF_CLOUD_COAP`). See [Memfault remote debugging](../91m1_ppp/memfault.md), which applies to this application too.

## Troubleshooting

| Symptom | Action |
|---------|--------|
| `Missing nRF Cloud credentials` followed by the missing items | Install the credentials as described in [Connect to nRF Cloud](../../getting-started/93m1-ppp.md#step-3-connect-to-nrf-cloud). The application retries until they are present. |
| Cloud connection refused after onboarding | Check that you onboarded the host **Device ID** from the boot log, not the modem UUID. |
| `device_credentials_installer` cannot connect | Confirm the shell prompt (`uart:~$`) is visible on VCOM0, and that no serial terminal has the port open. |
| `No cloud session this cycle, skipping location/FOTA` | The cloud connection failed this cycle. Check the cloud log lines before it for the reason. |

The [nRF91M1 PPP troubleshooting](../91m1_ppp/README.md#troubleshooting) table covers DNS, time, DTLS, and memory issues of the host-side CoAP stack, which apply here too.

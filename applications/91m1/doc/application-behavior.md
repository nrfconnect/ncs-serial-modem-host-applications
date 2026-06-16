# Application behavior

General runtime behavior of the [91m1](../) host application. For hardware wiring, build, and cloud provisioning, see the [main guide](README.md).

## Overview

The application runs on the **nRF54L15** host MCU and treats the **nRF91M1 Serial Modem** as a cellular data interface over PPP. Cloud connectivity uses **host-native CoAP/DTLS** to nRF Cloud. Credentials are stored in **TF-M Protected Storage** and used to sign JWTs for CoAP authentication.

## Architecture

Four cooperating modules run as dedicated threads (or a workqueue for periodic tasks). Each module owns an SMF state machine and publishes events on zbus channels:

| Module | Channel | Responsibility |
|--------|---------|----------------|
| Network | `network_chan` | Bring PPP up/down via the connection manager; report L4 connect/disconnect |
| Cloud | `cloud_chan` | Connect to nRF Cloud and send device messages |
| FOTA | `fota_chan` | Poll nRF Cloud for updates, download via CoAP proxy, stage MCUboot images |
| Main | `main_sync_chan` | Orchestrate cloud synchronization and coordinate FOTA with network teardown |

On boards with a modem reset GPIO, `modem_reset.c` pulses the nRF91 reset line during host boot before modules start.

## Startup sequence

1. **Boot** — MCUboot loads the application. The nRF Cloud library logs the device ID (from the nRF54L15 HW ID), protocol, and security tag.
2. **Network** — The network module connects on startup. When L4 connectivity is established it publishes `NETWORK_CONNECTED`.
3. **Cloud** — On network up, the cloud module waits for valid time (NTP over PPP) and installed credentials, then calls `nrf_cloud_coap_connect()`. Missing credentials or time cause a retry every 10 seconds (`CONFIG_APP_CLOUD_CREDENTIAL_RETRY_SECONDS`).
4. **Main** — When the cloud module publishes `CLOUD_CONNECTED`, main transitions to the cloud-connected state and runs the first cloud synchronization.

If the network drops, the cloud module disconnects and main returns to the cloud-disconnected state. A network disconnect also notifies the FOTA module so an in-progress update can proceed to reboot safely.

## Cloud synchronization

While nRF Cloud is connected, main keeps a periodic timer on a dedicated workqueue (`CONFIG_APP_MAIN_CLOUD_SYNCHRONIZATION_PERIOD_SECONDS`, default **30 s**). Each synchronization:

1. Publishes a demo JSON device message on `cloud_chan` (payload: `{"appId":"SMHA","messageType":"DATA","data":"hello"}`).
2. Publishes `FOTA_POLL_REQUEST` on `fota_chan` to check for firmware updates.

An initial synchronization runs immediately on cloud connect. The timer is cancelled when cloud disconnects.

## Memfault

Memfault is configured for firmware type `smha-91m1` and uploads through nRF Cloud CoAP (`CONFIG_MEMFAULT_USE_NRF_CLOUD_COAP`). The SDK collects heartbeat metrics on its internal timer and uploads data periodically (`CONFIG_MEMFAULT_PERIODIC_UPLOAD`). Coredumps are RAM-backed. See [Memfault remote debugging](memfault.md) for dashboard setup.

## Configuration

| Option | Default | Effect |
|--------|---------|--------|
| `CONFIG_APP_CLOUD_CREDENTIAL_RETRY_SECONDS` | 10 | Retry interval when credentials or time are missing |
| `CONFIG_APP_MAIN_CLOUD_SYNCHRONIZATION_PERIOD_SECONDS` | 30 | Cloud sync interval while connected |
| `CONFIG_NRF_CLOUD_SEC_TAG` | 16842753 | TLS credential tag for nRF Cloud |

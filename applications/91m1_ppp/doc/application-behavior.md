# Application behavior

General runtime behavior of the [91m1_ppp](../) host application. For the modules, their channels, and their state machines, see [Architecture](architecture.md). For hardware wiring, build, and cloud provisioning, see the [main guide](README.md).

## Overview

The application runs on the **nRF54L15** host MCU and treats the **nRF91M1 Serial Modem** as a cellular data interface over PPP. Cloud connectivity uses **host-native CoAP/DTLS** to nRF Cloud. Credentials are stored in **TF-M Protected Storage** and used to sign JWTs for CoAP authentication.

Cooperating modules run as dedicated threads, each owning an SMF state machine and communicating over zbus channels. See [Architecture](architecture.md) for what each module does and how they interact.

## Startup sequence

1. **Boot** — MCUboot loads the application. On boards with a modem reset GPIO, `modem_reset.c` pulses the nRF91 reset line before the modules start. The nRF Cloud library logs the device ID (from the host HW ID), protocol, and security tag.
2. **Network** — The network module connects on startup. When L4 connectivity is established it publishes `NETWORK_CONNECTED`.
3. **Cloud** — On network up, the cloud module waits for valid time (NTP over PPP) and installed credentials, then calls `nrf_cloud_coap_connect()`. Missing credentials or time cause a retry every 10 seconds (`CONFIG_APP_CLOUD_CREDENTIAL_RETRY_SECONDS`).
4. **Main** — When the cloud module publishes `CLOUD_CONNECTED`, main transitions to the cloud-connected state and runs the first cloud synchronization.

## Cloud synchronization

While nRF Cloud is connected, main keeps a periodic timer on a dedicated workqueue (`CONFIG_APP_MAIN_CLOUD_SYNCHRONIZATION_PERIOD_SECONDS`, default **30 s**). Each synchronization:

1. Publishes a demo JSON device message on `cloud_chan` (payload: `{"appId":"SMHA","messageType":"DATA","data":"hello"}`).
2. Publishes `LOCATION_SEARCH_TRIGGER` on `location_chan` when the application is built with the location overlay (`CONFIG_APP_LOCATION`).
3. Publishes `CLOUD_SHADOW_POLL_REQUEST` on `cloud_chan` to pick up any desired configuration from the device shadow.
4. Publishes `FOTA_POLL_REQUEST` on `fota_chan` to check for firmware updates.
5. Publishes `CLOUD_MEMFAULT_POST_REQUEST` on `cloud_chan` to post any pending Memfault data.

An initial synchronization runs immediately on cloud connect. The timer is cancelled when cloud disconnects.

A location search runs asynchronously: the location module scans for Wi-Fi access points and publishes the result as `LOCATION_CLOUD_REQUEST`, which main forwards to the cloud module. The cloud module resolves it into a position with an nRF Cloud CoAP ground-fix request and logs the position together with a Google Maps URL. A trigger that arrives while a search is still in progress is ignored, so a scan timeout longer than the synchronization period simply means fewer position updates.

## Memfault

Memfault is configured for firmware type `smha-91m1` and uploads through nRF Cloud CoAP (`CONFIG_MEMFAULT_USE_NRF_CLOUD_COAP`). The SDK collects heartbeat metrics on its internal timer and uploads data periodically (`CONFIG_MEMFAULT_PERIODIC_UPLOAD`), and the cloud module posts pending data on every cloud synchronization, when main asks for it with `CLOUD_MEMFAULT_POST_REQUEST`. Coredumps are RAM-backed. See [Memfault remote debugging](memfault.md) for dashboard setup.

## Configuration

| Option | Default | Effect |
|--------|---------|--------|
| `CONFIG_APP_CLOUD_CREDENTIAL_RETRY_SECONDS` | 10 | Retry interval when credentials or time are missing |
| `CONFIG_APP_MAIN_CLOUD_SYNCHRONIZATION_PERIOD_SECONDS` | 30 | Cloud sync interval while connected |
| `CONFIG_NRF_CLOUD_SEC_TAG` | 16842753 | TLS credential tag for nRF Cloud |

Per-module options are documented in the module guides linked from [Architecture](architecture.md).

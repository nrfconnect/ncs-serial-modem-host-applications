# nRF93M1 Host Application

This guide walks you through to get started with the nRF93M1 Host Application, including building and flashing the firmware, provisioning device credentials, and connecting securely to nRF Cloud over CoAP/DTLS.

The application runs on the nRF54L15 host of the nRF93M1 DK, with cellular over the on-board nRF93M1 Serial Modem through PPP. The host terminates DTLS and CoAP itself. PPP just carries IP traffic between the host's network stack and the modem's cellular radio. The modem's own onboard AT-based cloud client is not used here. It targets the non-secure (TF-M) build and stores the host's TLS credentials in Protected Storage. Telemetry, location, and FOTA go to nRF Cloud over CoAP. Location fixes still pull raw cell and Wi-Fi® scan data out of the modem with AT commands, but the actual cloud request is host-side CoAP. Diagnostics go to your nRF Cloud project.

## Prerequisites

To follow this guide, you must meet the following requirements:

- [nRF93M1 DK](https://www.nordicsemi.com/Products/Development-hardware/nRF93M1-DK) running Serial Modem firmware with PPP and CMUX enabled on the nRF93M1.
- An [nRF Cloud account](https://nrfcloud.com/) and API key.
- nRF Cloud Utils: `pip3 install nrfcloud-utils`.

## 1. Build and flash

Run `west` from inside the nRF Connect SDK toolchain environment (see [Initialize the workspace](../../../README.md#initialize-the-workspace)):

```shell
nrfutil sdk-manager toolchain launch --ncs-version v3.4.0 --shell
```

Use the following command to build on the DK:

```shell
cd applications/93m1_ppp
west build -p -b nrf93m1dk/nrf54l15/cpuapp/ns
west flash --erase
```

> [!NOTE]
> `--recover` and `--erase` wipe Protected Storage, so the device credentials are lost and must be re-provisioned (steps 3–4). After the first flash, use a plain `west flash` to keep the credentials in place.

## 2. Get the device ID

Open a serial terminal on the host console (UART20). Note the device ID from the boot log:

```text
<inf> nrf_cloud_info: Device ID: <16-hex-device-id>
```

It comes from the nRF54L15 SoC HW ID, not the modem UUID.

## 3. Create a CA certificate

Run the following command once for each CA from the directory where the CA files are stored:

```shell
create_ca_cert -c US -f self_
```

## 4. Install credentials

Run the following commands from the directory containing the CA certificate files:

```shell
device_credentials_installer \
  --ca self_<serial>_ca.pem --ca-key self_<serial>_prv.pem \
  --id-str <device_id> \
  -s -d --verify --coap --local-cert --cmd-type tls_cred_shell \
  --port /dev/cu.usbmodem*
```

The sec tag is `16842753`. On success it writes `onboard.csv`.

## 5. Onboard

Use the following command to onboard the devices listed in the `.csv` file to nRF Cloud:

```shell
nrf_cloud_onboard --api-key <your_api_key> --csv onboard.csv
```

## 6. Verify

Wait for the cellular link, then for the connection and first upload:

```text
<inf> main: Network connected
<inf> nrf_cloud_coap_transport: Authorized
<inf> cloud: Cloud connected
<inf> cloud: Diagnostics uploaded
```

If the CA certificate or private key is missing, each cloud synchronization logs the missing items instead and skips the connection attempt until the credentials are installed:

```text
<wrn> cloud: Missing nRF Cloud credentials (see docs/applications/93m1_ppp/README.md)
<wrn> cloud:   - CA cert (run device_credentials_installer --coap)
```

Heartbeats and metrics appear in the linked nRF Cloud project. Location and FOTA use the same CoAP session.

See the [nRF91M1 Host Application](../91m1_ppp/README.md) documentation for credential and troubleshooting detail, which applies here too.

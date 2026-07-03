# nRF93M1 Host Application

Getting started with the 93m1 ppp application: build, flash, provision credentials, and connect to nRF Cloud over CoAP/DTLS.

The application runs on the nRF54L15 host of the nRF93M1 DK, with cellular over the on-board nRF93M1 Serial Modem via PPP. The host terminates DTLS and CoAP itself. PPP just carries IP traffic between the host's network stack and the modem's cellular radio. The modem's own onboard AT-based cloud client is not used here. It targets the non-secure (TF-M) build and stores the host's TLS credentials in Protected Storage. Telemetry, location, and FOTA go to nRF Cloud over CoAP. Location fixes still pull raw cell and Wi-Fi scan data out of the modem with AT commands, but the actual cloud request is host-side CoAP. Memfault data goes to the Memfault project linked to your nRF Cloud account.

## Prerequisites

- nRF93M1 DK running Serial Modem firmware with PPP and CMUX enabled on the nRF93M1.
- An nRF Cloud account and API key, with a linked Memfault project.
- nRF Cloud Utils: `pip3 install nrfcloud-utils`.

## 1. Build and flash

```shell
cd applications/93m1_ppp
west build -p -b nrf93m1dk/nrf54l15/cpuapp/ns
west flash --erase
```

> **Note:** `--recover` and `--erase` wipe Protected Storage, so the device credentials are lost and must be re-provisioned (steps 3–4). After the first flash, use a plain `west flash` to keep the credentials in place.

## 2. Get the device ID

Open a serial terminal on the host console (uart20). Note the device ID from the boot log:

```text
<inf> nrf_cloud_info: Device ID: <16-hex-device-id>
```

It comes from the nRF54L15 SoC HW ID, not the modem UUID.

## 3. Create a CA certificate

Once per CA, in the directory where you keep the CA files:

```shell
create_ca_cert -c US -f self_
```

## 4. Install credentials

From the same directory as the CA files:

```shell
device_credentials_installer \
  --ca self_<serial>_ca.pem --ca-key self_<serial>_prv.pem \
  --id-str <device_id> \
  -s -d --verify --coap --local-cert --cmd-type tls_cred_shell \
  --port /dev/cu.usbmodem*
```

The sec tag is 16842753. On success it writes `onboard.csv`.

## 5. Onboard

```shell
nrf_cloud_onboard --api-key <your_api_key> --csv onboard.csv
```

## 6. Verify

Wait for the cellular link, then for the connection and first upload:

```text
<inf> main: Network connected
<inf> nrf_cloud_coap_transport: Authorized
<inf> memfault_module: Memfault data uploaded
```

Heartbeats and metrics appear in the linked Memfault project. Location and FOTA use the same CoAP session.

See the [91m1 README](../../91m1/doc/README.md) for credential and troubleshooting detail, which applies here too.

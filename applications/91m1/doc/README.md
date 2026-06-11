# nRF91M1 Host Application Documentation

End-to-end guide for the [91m1](../) host application on nRF54L15. Complete these steps in order to wire up the hardware, build and flash firmware, and connect the device to nRF Cloud over CoAP.

The application connects to nRF Cloud over **CoAP/DTLS** from the host MCU (nRF54L15). Cellular data goes through the nRF91M1 Serial Modem via PPP; credentials are stored on the host using the TLS credentials shell and TF-M Protected Storage. Until onboarding is complete, `nrf_cloud_coap_connect()` will fail even if credentials are installed locally.

## Getting started

1. **Wire up the development boards** — Connect the nRF54L15 DK (host) to the nRF9151 DK (Serial Modem). Configure both DKs in the Board Configurator and flash Serial Modem firmware with PPP and CMUX enabled on the nRF9151. See [Hardware setup](hardware-setup.md) for wiring, pin assignments, and firmware requirements.

2. **Build and flash 91m1 on the nRF54L15 DK**

   ```shell
   cd applications/91m1
   west build -b nrf54l15dk/nrf54l15/cpuapp/ns -p
   west flash --recover
   ```

3. **Verify the host application boots** — Open a serial terminal on the **nRF54L15 console** (uart20 — the DK USB port for the host shell, not the modem link on uart30). Confirm the application starts and note the **Device ID** from the boot log:

   ```text
   <inf> nrf_cloud_info: Device ID: E4361AD8FD3E4554
   ```

   This 16-character hex ID comes from the nRF54L15 SoC (`CONFIG_NRF_CLOUD_CLIENT_ID_SRC_HW_ID`). Use it for credential generation and portal onboarding — it is **not** the nRF91 modem UUID. The cloud module logs it again when connecting:

   ```text
   <inf> cloud: nRF Cloud client ID: E4361AD8FD3E4554
   ```

4. **Install nRF Cloud Utils** (one-time setup on your host machine)

   ```shell
   pip3 install nrfcloud-utils
   ```

   See the [nRF Cloud Utils](https://github.com/nRFCloud/utils) repository for details.

5. **Create a self-signed CA certificate** (one-time; reuse the same CA for all devices)

   ```shell
   create_ca_cert -c US -f self_
   ```

   Replace `US` with your two-letter country code. This produces three files (serial number varies):

   - `self_<serial>_ca.pem` — CA certificate
   - `self_<serial>_prv.pem` — CA private key
   - `self_<serial>_pub.pem` — CA public key

   Keep the CA private key secure.

6. **Install device credentials on the host** — Run `device_credentials_installer` from the directory containing your CA files, pointing at the nRF54L15 serial port and the device ID from step 3:

   ```shell
   device_credentials_installer \
     --ca self_<serial>_ca.pem \
     --ca-key self_<serial>_prv.pem \
     --id-str <device_id> \
     -s -d --verify --coap --local-cert --cmd-type tls_cred_shell \
     --port /dev/cu.usbmodem*
   ```

   Where:

   - `<device_id>` is the 16-character hex ID from the boot log (for example `E4361AD8FD3E4554`).
   - `--coap` installs the CoAP server root CA in addition to the AWS root CA chain.
   - `--local-cert` generates the device certificate and private key on the host (required because the TLS credentials shell does not support CSR generation).
   - `--cmd-type tls_cred_shell` sends `cred` shell commands over the host UART.
   - `-s` saves generated PEM files (`<device_id>_crt.pem`, `<device_id>_prv.pem`, etc.) in the current directory.
   - `-d` deletes any existing credentials in sec tag 16842753 before installing new ones.
   - `--verify` confirms credentials were written correctly.
   - `--port` selects the nRF54L15 console serial port (adjust for your OS; omit if auto-detection works).

   On success you should see:

   ```text
   Saving nRF Cloud device onboarding CSV file onboard.csv...
   Onboarding CSV file saved, row count: 1
   ```

   Delete the device private key PEM from your host after installation if you used `--local-cert`. The default nRF Cloud security tag is **16842753** (`CONFIG_NRF_CLOUD_SEC_TAG`).

7. **Onboard the device to your nRF Cloud account** — Register the device public key using the CSV generated in the previous step:

   ```shell
   nrf_cloud_onboard --api-key <your_api_key> --csv onboard.csv
   ```

   Use an API key from your [nRF Cloud user account](https://nrfcloud.nordicsemi.com/).

8. **Verify the nRF Cloud connection** — After onboarding:

   1. Ensure the cellular link is up — wait for `Network connected` in the log (PPP over the Serial Modem).
   2. The cloud module retries automatically every 10 seconds if credentials or valid time are missing.
   3. On success, logs show:

      ```text
      <inf> cloud: nRF Cloud client ID: E4361AD8FD3E4554
      <inf> main: Cloud connected
      <inf> main: Cloud message sent
      ```

   4. A demo JSON device message is sent automatically from `main.c`.

   Credentials persist across reboot when `CONFIG_TLS_CREDENTIALS_BACKEND_PROTECTED_STORAGE` is enabled.

## Application behavior

| Stage | Behavior |
|-------|----------|
| Boot | nRF Cloud library prints device ID, protocol (CoAP), and sec tag |
| Network up | Cloud module waits for valid time (NTP over PPP), then calls `nrf_cloud_coap_connect()` |
| Missing credentials | Retries every 10 s; see [Troubleshooting](#troubleshooting) |
| Connected | Polls device shadow periodically; accepts outbound messages on the cloud zbus channel |

CoAP authentication uses a JWT signed with the installed private key. Only the **CA certificate** and **private key** are required at runtime; the device certificate is used for portal onboarding.

## Troubleshooting

| Symptom | Action |
|---------|--------|
| `Missing required nRF Cloud credentials` | Re-run `device_credentials_installer` with `--coap --cmd-type tls_cred_shell` |
| `nrf_cloud_coap_connect` auth failure | Confirm the device is onboarded in the portal with the correct device ID |
| Wrong device in portal | Use the nRF54L15 **Device ID** from boot log, not the modem `%DEVICEUUID` |
| JWT / time errors | Wait for NTP after PPP connects; check `date_time` logs |
| `device_credentials_installer` cannot connect | Confirm the shell prompt (`uart:~$`) is visible on the selected serial port |

## Reference guides

| Guide | Description |
|-------|-------------|
| [Hardware setup](hardware-setup.md) | Two-DK wiring, board configurator, devicetree, Serial Modem firmware |

## References

- [nRF Cloud CoAP library](https://docs.nordicsemi.com/bundle/ncs-latest/page/nrf/libraries/networking/nrf_cloud_coap.html)
- [nRF Cloud Utils](https://github.com/nRFCloud/utils)

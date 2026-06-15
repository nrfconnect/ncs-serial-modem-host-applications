# nRF91M1 Host Application Documentation

End-to-end guide for the [91m1](../) host application on nRF54L15. Complete these steps in order to wire up the hardware, build and flash firmware, and connect the device to nRF Cloud over CoAP.

The application connects to nRF Cloud over **CoAP/DTLS** from the host MCU (nRF54L15). Cellular data goes through the nRF91M1 Serial Modem via PPP; credentials are stored on the host using the TLS credentials shell and TF-M Protected Storage. Until onboarding is complete, `nrf_cloud_coap_connect()` will fail even if credentials are installed locally.

## Getting started

1. **Wire up the development boards** — Connect the nRF54L15 DK (host) to the nRF9151 DK (Serial Modem). Configure both DKs in the Board Configurator and flash Serial Modem firmware with PPP and CMUX enabled on the nRF9151. The host application is tested with Serial Modem commit [`e23c2bde`](https://github.com/nrfconnect/ncs-serial-modem/commit/e23c2bde08a83e8a2908f78ee19f2b2ff5c6e46e). See [Hardware setup](hardware-setup.md) for wiring, pin assignments, and [how to flash Serial Modem on the nRF91](https://docs.nordicsemi.com/bundle/addon-serial_modem-latest/page/gsg_guide.html#building_and_running).

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
| Connected | Accepts outbound messages on the cloud zbus channel |

CoAP authentication uses a JWT signed with the installed private key. Only the **CA certificate** and **private key** are required at runtime; the device certificate is used for portal onboarding.

## Troubleshooting

| Symptom | Action |
|---------|--------|
| `Missing required nRF Cloud credentials` | Re-run `device_credentials_installer` with `--coap --cmd-type tls_cred_shell` |
| `nrf_cloud_coap_connect` auth failure | Confirm the device is onboarded in the portal with the correct device ID |
| Wrong device in portal | Use the nRF54L15 **Device ID** from boot log, not the modem `%DEVICEUUID` |
| JWT / time errors | After `Network connected`, run `net dns query time.google.com`. Ping to an IP does not prove DNS works. If logs show `getaddrinfo entries overflow`, ensure `CONFIG_DNS_RESOLVER_AI_MAX_ENTRIES=4` is set. If SNTP fails with `Not enough connection contexts`, increase `CONFIG_NET_MAX_CONN` (IPv4-only builds default to 4, which is too low once DNS + modem + SNTP + CoAP are active) |
| `nrf_cloud_coap_connect` error -111 | Host-native CoAP needs DTLS sockets (`CONFIG_NET_SOCKETS_SOCKOPT_TLS`, `CONFIG_NET_SOCKETS_ENABLE_DTLS`). If enabling DTLS causes `RAM overflowed`, lower `CONFIG_MBEDTLS_SSL_IN/OUT_CONTENT_LEN` to 1024 (defaults are 16 KB each) and disable IPv6. Then run `net dns query coap.nrfcloud.com` after PPP is up |
| `Failed to parse certificate` err `-0x2180` | Shell-provisioned credentials are PEM. Enable `CONFIG_MBEDTLS_PEM_PARSE_C=y` (see `nrf/samples/wifi/nrf_cloud/prj.conf`). Reinstall credentials if a prior install truncated them |
| `Failed to parse certificate` err `-0xffffffff` | mbedTLS returned `1`: one cert in the CA chain or device cert failed to parse. Enable `CONFIG_MBEDTLS_RSA_C`, `CONFIG_MBEDTLS_ECP_C`, and `CONFIG_PSA_WANT_ALG_ECDH` (see `nrf/samples/wifi/nrf_cloud/prj.conf`) |
| `RAM overflowed` with cloud enabled | Trim TLS buffers (`MBEDTLS_SSL_IN/OUT_CONTENT_LEN=1024`), disable `CONFIG_NET_IPV6`, and reduce `NET_PKT/BUF` counts before dropping DTLS |
| Boot loop (~6 s) | Usually a hard fault from stack overflow. Increase `CONFIG_APP_CLOUD_THREAD_STACK_SIZE` (default 10240) if cloud connect faults; keep `CONFIG_MODEM_DEDICATED_WORKQUEUE=y`. To capture the fault, temporarily set `CONFIG_RESET_ON_FATAL_ERROR=n` and check for `fatal_error: Resetting system` or stack traces |
| `device_credentials_installer` cannot connect | Confirm the shell prompt (`uart:~$`) is visible on the selected serial port |

## Memfault

The application includes Memfault for remote crash reporting and device health metrics. Once the device is onboarded and connected to nRF Cloud, coredumps and metrics are forwarded automatically via CoAP.

See [Memfault remote debugging](memfault.md) for how to open the Memfault dashboard from nRF Cloud, upload `zephyr.elf`, and verify data collection.

## Reference guides

| Guide | Description |
|-------|-------------|
| [Hardware setup](hardware-setup.md) | Two-DK wiring, board configurator, Serial Modem firmware version and flashing |
| [Memfault remote debugging](memfault.md) | Open Memfault from nRF Cloud, upload symbol files, view coredumps |

## References

- [nRF Cloud CoAP library](https://docs.nordicsemi.com/bundle/ncs-latest/page/nrf/libraries/networking/nrf_cloud_coap.html)
- [nRF Cloud Utils](https://github.com/nRFCloud/utils)

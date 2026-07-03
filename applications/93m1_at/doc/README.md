# Serial Modem Host 93m1 (AT)

Minimal tracker application for the **nRF93M1 Serial Modem**. The host uses Zephyr's cellular
modem driver over PPP purely to detect that the modem has attached to the network. No host IP
traffic is exchanged over it. All cloud communication (telemetry, location, TLS) instead goes
through the modem's own nRF Cloud client via raw AT commands over a dedicated pipe. The host
samples battery state locally and periodically syncs it and location fixes to nRF Cloud over
that AT interface.

## Prerequisites

- nRF93M1 DK.
- nRF Connect for Desktop Serial Terminal.
- An nRF Cloud account.

## Building and flashing

```shell
cd applications/93m1_at
west build -p -b nrf93m1dk/nrf54l15/cpuapp/ns
west flash
```

## Bring-up

The modem is its own nRF Cloud client. It needs to be registered to your account before it can send location fixes or telemetry. All commands below go through the app's `modem at` shell command over the console, not a raw AT passthrough.

### Claim the device on nRF Cloud

1. In nRF Cloud, go to **Fleet → Devices**.
2. Click **+ Add New Devices** and select nRF93M1.
3. Paste the two supplied AT commands to generate the device UUID and JWT with the `modem at` shell:

   ```text
   uart:~$ modem at AT%DEVICEUUID
   %DEVICEUUID: xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
   OK
   ```

   ```text
   uart:~$ modem at AT%REGJWT=<team-id>
   %REGJWT: <jwt>
   OK
   ```

4. Paste the resulting UUID and JWT back into nRF Cloud to finish claiming the device.
5. Review and confirm.

The device should appear in the device list within a few seconds. It may take a little longer to show as **Connected**.

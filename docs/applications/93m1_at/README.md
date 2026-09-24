# Serial Modem Host 93m1 (AT)

Minimal tracker application for the **nRF93M1 Serial Modem**. The host runs Zephyr's cellular
modem driver, which dials the modem and brings up a PPP link over a CMUX channel. The application
uses this only for connectivity management: `conn_mgr` powers the link up and down, and the PPP
interface's L4 connected/disconnected events tell the application when the network is available.
The application sends no IP traffic of its own over PPP. All cloud communication (telemetry,
location, TLS) instead goes through the modem's own nRF Cloud client via raw AT commands over a
separate CMUX user pipe. The host
samples battery state locally and periodically syncs it and location fixes to nRF Cloud over
that AT interface.

## Prerequisites

- nRF93M1 DK.
- nRF Connect for Desktop Serial Terminal.
- An nRF Cloud account.

## Building and flashing

Run `west` from inside the nRF Connect SDK toolchain environment (see [Initialize the workspace](../../../README.md#initialize-the-workspace)):

```shell
nrfutil sdk-manager toolchain launch --ncs-version v3.4.0 --shell
```

```shell
cd applications/93m1_at
west build -p -b nrf93m1dk/nrf54l15/cpuapp/ns
west flash
```

## Bring-up

The modem is its own nRF Cloud client. It needs to be registered to your account before it can send location fixes or telemetry. All commands below go through the app's `at` shell command over the console, not a raw AT passthrough.

### Claim the device on nRF Cloud

1. In nRF Cloud, go to **Fleet → Devices**.
2. Click **+ Add New Devices** and select nRF93M1.
3. Paste the two supplied AT commands to generate the device UUID and JWT with the `at` shell:

   ```text
   uart:~$ at AT%DEVICEUUID
   %DEVICEUUID: xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
   OK
   ```

   ```text
   uart:~$ at AT%REGJWT=<team-id>
   %REGJWT: <jwt>
   OK
   ```

4. Paste the resulting UUID and JWT back into nRF Cloud to finish claiming the device.
5. Review and confirm.

The device should appear in the device list within a few seconds. The new nRF Cloud UI does not show a connection status. Instead, the device's **Last seen** time updates once the modem first talks to nRF Cloud.

## After onboarding

Once the modem has attached to the network it performs a cloud sync every 10 minutes (`CONFIG_APP_SYNC_BOOT_DELAY_SECONDS` and `CONFIG_APP_SYNC_INTERVAL`). Each sync does two things through the modem's nRF Cloud client:

- **Location:** sends `AT%NRFCLOUDLOCATION=7,1`. The modem collects single-cell, multicell and Wi-Fi measurements, nRF Cloud resolves them to a position, and the result is returned to the host. Change the method with `CONFIG_APP_LOCATION_METHOD` (1 single-cell, 2 multicell, 4 Wi-Fi, or a sum of them).
- **Battery:** sends the fuel gauge state of charge as a device message: `{"appId":"BATTERY","messageType":"DATA","data":"<percent>"}`. The fuel gauge is sampled every 60 seconds whether or not the device is connected (`CONFIG_APP_BATTERY_SAMPLE_INTERVAL`).

The console also offers these shell commands:

| Command | Action |
| --- | --- |
| `at "<command>"` | Send an AT command to the modem and print the response. |
| `network connect` / `network disconnect` | Bring the cellular link up or down. |
| `button 1` / `button 2` | Simulate a press of the matching DK button. |

Pressing **Button 1** (or running `button 1`) starts a sync right away instead of waiting for the next interval. **Button 2** has no action yet.

## Verify that data reaches nRF Cloud

### On the device

After the modem attaches, each sync logs the location result and the battery report on the host console:

```text
<inf> main: Connected
<inf> location: Location: 63.421,10.437 Uncertainty: 25m Method: Wi-Fi
<inf> cloud: Battery percentage reported: 87%
```

The location line only appears after nRF Cloud has answered, so it confirms the whole round trip. `Method` is the source nRF Cloud actually used, which can differ from the requested one when several are allowed. The battery line means the modem accepted the message for sending.

To test the cloud path by hand, send a message or request a location with the `at` shell. Single quotes keep the JSON double quotes intact:

```text
uart:~$ at 'AT%NRFCLOUDMESSAGE={"appId":"TEST","data":"hello"}'
%NRFCLOUDMESSAGE: SENT
OK
uart:~$ at AT%NRFCLOUDLOCATION=1,1
OK
<inf> location: Location: 63.421,10.437 Uncertainty: 1200m Method: Single-cell
```

### In nRF Cloud

In the UI, go to **Fleet → Devices** and find your device. Its **Last seen** time updates after each sync: every 10 minutes, or right away when you press Button 1.

> [!NOTE]
> nRF Cloud is transitioning to a Memfault-integrated experience. For now, the device data is only visible in the **legacy nRF Cloud portal**. After logging in at [nrfcloud.com](https://nrfcloud.com), open the legacy app using the link in the **bottom left corner** of the new UI.

1. In the legacy app, open **Device Management → Devices** and click the device ID. The device ID is the modem UUID from `AT%DEVICEUUID`.
2. The map on the device page shows the location fixes from each sync.
3. The **Terminal** card shows the `BATTERY` device messages, one per sync. To load older messages, click the clock icon, select a time range and click **Get Data**.

The same data is also available from the [nRF Cloud REST API](https://api.nrfcloud.com/):

```shell
curl -H "Authorization: Bearer <api_key>" \
  "https://api.nrfcloud.com/v1/messages?deviceId=<device_uuid>&pageLimit=10"
curl -H "Authorization: Bearer <api_key>" \
  "https://api.nrfcloud.com/v1/location/history?deviceId=<device_uuid>&pageLimit=10"
```

Get `<api_key>` from the legacy app: select your team, then **burger menu → User Account → Team Details**. See [Managing tokens and keys](https://docs.memfault.com/docs/legacy-nrfcloud/tokens-and-keys).

### Troubleshooting

| Symptom | Meaning |
| --- | --- |
| Device listed under **Fleet → Devices**, but no location or messages visible | The new UI only shows **Last seen**. Open the device in the legacy app as described in [In nRF Cloud](#in-nrf-cloud). |
| `modem_at: CoAP response: 4.01 Unauthorized (device may not be onboarded to nRF Cloud)` | nRF Cloud rejected the modem. Finish the claiming steps above and check that the device is listed under your team. |
| `location: Failed to request location, network is down?` or `cloud: Failed to sent battery data, network is down?` | The AT command failed, usually because the modem has not attached yet. Check the link with `at AT+CEREG?`. |
| `main: battery_percent_get, error: -61` | The fuel gauge has no valid sample yet, so no battery message is sent this sync. Check that a battery is connected to the DK. |
| Location lines always report `Method: Single-cell` | Multicell and Wi-Fi positioning do not work while the modem is in RRC Connected mode, so nRF Cloud falls back to single-cell. |

# Memfault remote debugging

[Memfault](https://memfault.com/) is a device observability platform that complements on-device debugging. It collects crash coredumps, reboot events, stack/heap metrics, and logs from deployed devices so you can diagnose issues without physical access — especially useful for sporadic faults or problems that only appear on real networks.

The 91m1 application forwards Memfault data through the existing **nRF Cloud CoAP** connection (same DTLS session and JWT as cloud messaging). No separate Memfault credentials or HTTP upload path is required.

## Prerequisites

Complete [Getting started](README.md) first — the device must be onboarded to your nRF Cloud account and successfully connect (`Cloud connected` in the log). Memfault data is routed to the Memfault project linked to that nRF Cloud account.

## Setup

1. **Open Memfault from nRF Cloud** — Log in to [nRF Cloud](https://nrfcloud.nordicsemi.com/) and click **Memfault** in the left sidebar. This opens the Memfault project linked to your account.

2. **Upload the firmware symbol file** — Memfault needs the build's `zephyr.elf` to decode crash addresses into function names and line numbers. Upload it **once per firmware build**, before or as soon as devices start reporting data:

   - In the Memfault UI: **Software → Symbol Files → Upload Symbol File**
   - Select `build/zephyr/zephyr.elf` from your west build directory (the default output when building from `applications/91m1`).

   Alternatively, upload from the command line with the [Memfault CLI](https://docs.memfault.com/docs/ci/install-memfault-cli):

   ```shell
   memfault \
     --org-token <token> \
     --org <org> \
     --project <project> \
     upload-mcu-symbols build/zephyr/zephyr.elf
   ```

3. **Verify data is flowing** — After the device connects to nRF Cloud, periodic Memfault uploads run in the background (`CONFIG_MEMFAULT_PERIODIC_UPLOAD`). Look for:

   ```text
   <inf> mflt: Periodic background upload scheduled - initial delay=... period=...
   ```

   To trigger a manual upload from the shell:

   ```shell
   uart:~$ mflt test heartbeat
   uart:~$ mflt post_chunks
   ```

## Viewing device data

In the Memfault UI:

1. Click **Devices** in the left toolbar to see devices that have reported in.
2. Select a device to inspect its coredumps, metrics, and event history.

Coredumps are captured automatically on crashes (RAM-backed, 3 KB). Without an uploaded symbol file, traces appear with a **Symbols Missing** label and cannot be decoded.

## Testing

Trigger test faults from the nRF54L15 shell to verify the full pipeline:

```shell
uart:~$ mflt test hardfault
uart:~$ mflt test assert
uart:~$ mflt test usagefault
```

After a test fault the device reboots, reconnects to nRF Cloud, and uploads the coredump. Confirm the decoded trace appears under **Issues** or on the device's page in Memfault.

## What gets collected

| Data | Description |
|------|-------------|
| Coredumps | Register state, stack, and memory snapshot on crash |
| Reboot events | Unexpected reboots and boot reason |
| Stack metrics | Thread stack high-water marks (`CONFIG_MEMFAULT_NCS_STACK_METRICS`) |
| Heap stats | Heap usage snapshots (`CONFIG_MEMFAULT_HEAP_STATS`) |
| Logs | Recent log buffer captured around faults (`CONFIG_MEMFAULT_LOGGING_ENABLE`) |

## References

- [Asset Tracker Template — Memfault](https://docs.nordicsemi.com/bundle/asset-tracker-template-latest/page/common/tooling_troubleshooting.html#memfault-remote-debugging)
- [Memfault in nRF Connect SDK](https://docs.nordicsemi.com/bundle/ncs-latest/page/nrf/external_comp/memfault.html)

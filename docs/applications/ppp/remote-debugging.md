# Remote debugging with nRF Cloud

[nRF Cloud](https://nrfcloud.nordicsemi.com/) includes device observability that complements on-device debugging. It collects crash coredumps, reboot events, stack/heap metrics, and logs from deployed devices so you can diagnose issues without physical access, especially useful for sporadic faults or problems that only appear on real networks. On the device, the data is collected by the Memfault SDK, which is part of nRF Connect SDK.

## Prerequisites

Complete the getting-started guide for [an nRF54 DK](../../getting-started/ppp-nrf54.md) or [the nRF93M1 DK](../../getting-started/ppp-nrf93m1.md) before proceeding.

## Setup

1. **Open nRF Cloud:**

    1. Log in to [nRF Cloud](https://nrfcloud.nordicsemi.com/).

1. **Upload the firmware symbol file** - nRF Cloud needs the build's `zephyr.elf` to decode crash addresses into function names and line numbers. Upload it once per firmware build, before or as soon as devices start reporting data:

    1. In nRF Cloud, navigate to **Software**.
    1. Select **Symbol Files**.
    1. Click on **Upload Symbol File**.
    1. Select `build/zephyr/zephyr.elf` from your west build directory (the default output when building from `applications/ppp`).

    Alternatively, upload from the command line with the [`memfault` CLI](https://docs.memfault.com/docs/ci/install-memfault-cli):

    ```shell
    memfault \
      --org-token <token> \
      --org <org> \
      --project <project> \
      upload-mcu-symbols build/zephyr/zephyr.elf
    ```

1. **Verify data is flowing** - After the device connects to nRF Cloud, periodic observability uploads run in the background (`CONFIG_MEMFAULT_PERIODIC_UPLOAD`). Look for:

    ```text
    <inf> mflt: Periodic background upload scheduled - initial delay=... period=...
    ```

    To trigger a manual upload from the shell:

    ```shell
    uart:~$ mflt test heartbeat
    uart:~$ mflt post_chunks
    ```

## Viewing device data

In nRF Cloud:

1. Click **Devices** in the left toolbar to see devices that have reported in.
1. Select a device to inspect its coredumps, metrics, and event history.

### Software watchdog

Each module thread is monitored by a Zephyr task watchdog. When a thread stops feeding its watchdog, the timeout callback calls `MEMFAULT_SOFTWARE_WATCHDOG()`, which captures a coredump tagged **Software Watchdog** before rebooting. To trigger this manually, suspend a monitored thread from the kernel shell so it can no longer feed its watchdog:

```shell
uart:~$ kernel thread list
uart:~$ kernel thread suspend <thread_id>
```

Use the `0x...` id printed next to the `main` thread. After the watchdog timeout (`CONFIG_APP_MAIN_WATCHDOG_TIMEOUT_SECONDS`) elapses, the device captures the coredump, reboots, and uploads it. The trace appears in nRF Cloud with the **Software Watchdog** reason.

## References

- [Asset Tracker Template - Remote debugging](https://docs.nordicsemi.com/bundle/asset-tracker-template-latest/page/common/tooling_troubleshooting.html#memfault-remote-debugging)
- [Memfault SDK in nRF Connect SDK](https://docs.nordicsemi.com/bundle/ncs-latest/page/nrf/external_comp/memfault.html)

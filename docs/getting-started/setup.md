# Set up the repository and toolchain

This page sets up everything you need to build the applications from the command line: nRF Util, the nRF Connect SDK toolchain, and a west workspace with this repository. You only need to do this once per computer.

If you only want to flash pre-built firmware, skip this page and go to your [application guide](index.md#choose-your-application).

## Install nRF Util and the toolchain

1. Install nRF Util by following the instructions in the [nRF Util documentation](https://docs.nordicsemi.com/bundle/nrfutil/page/guides/installing.html).

1. Install the SDK manager and device commands:

    ```shell
    nrfutil install sdk-manager device
    ```

1. Install the nRF Connect SDK toolchain. Its version must match the `nrf` revision in [`west.yml`](https://github.com/nrfconnect/ncs-serial-modem-host-applications/blob/main/west.yml), currently `v3.5.0-preview2`. The SDK itself is downloaded by west in [Create the workspace](#create-the-workspace), so only the toolchain is installed here:

    ```shell
    nrfutil sdk-manager toolchain install --ncs-version v3.5.0-preview2
    ```

## Start the toolchain shell

All `west` commands in this guide and in the application guides run inside the toolchain environment. Start it with:

```shell
nrfutil sdk-manager toolchain launch --ncs-version v3.5.0-preview2 --shell
```

Run this again in every new terminal before you use `west`. To run a single command without starting a shell, use:

```shell
nrfutil sdk-manager toolchain launch --ncs-version v3.5.0-preview2 -- <your command>
```

## Create the workspace

From the toolchain shell, create a workspace folder named `smha-workspace` and download the nRF Connect SDK modules into it:

```shell
west init -m https://github.com/nrfconnect/ncs-serial-modem-host-applications --mr main smha-workspace
cd smha-workspace/project
west update
```

`west update` downloads the nRF Connect SDK and all its modules and can take a while. The repository is cloned into `smha-workspace/project`, and the application guides run their commands from there.

## You are done when

The following command, run from `smha-workspace/project` inside the toolchain shell, lists the `manifest`, `nrf`, and `zephyr` projects without errors:

```shell
west list manifest nrf zephyr
```

---

**Previous:** [Getting started](index.md) | **Next:** your application guide, [nRF91M1 PPP](91m1-ppp.md), [nRF93M1 PPP](93m1-ppp.md), or [nRF93M1 AT](93m1-at.md)

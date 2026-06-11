# Serial Modem Host Applications

Host-side firmware for Nordic Smart Modem modules. This repository is built on [nRF Connect SDK](https://www.nordicsemi.com/Products/Development-software/nRF-Connect-SDK) (NCS) and follows the modular **zbus + SMF** architecture used by the [Asset Tracker Template](https://github.com/nrfconnect/Asset-Tracker-Template).

Supported product targets:

* **91m1** — host application for the [nRF91M1](https://www.nordicsemi.com/Products/nRF91M1) Smart Modem
* **93m1** — host application for the [nRF93M1](https://www.nordicsemi.com/Products/nRF93-Series) Smart Modem

---

## Repository structure

| Path | Purpose |
|------|---------|
| [`west.yml`](west.yml) | West manifest; pins NCS to `v3.4-branch` |
| [`zephyr/module.yml`](zephyr/module.yml) | Registers this repo as a Zephyr module |
| [`lib/include/`](lib/include/) | Shared headers (`app_common.h`) |
| [`applications/91m1/`](applications/91m1/) | nRF91M1 host application |
| [`applications/93m1/`](applications/93m1/) | nRF93M1 host application |

## Getting started

### Install prerequisites

1. Install nRF Util by following the instructions in the [nRF Util documentation](https://docs.nordicsemi.com/bundle/nrfutil/page/guides/installing.html).

2. Install the SDK manager command:

   ```shell
   nrfutil install sdk-manager
   ```

3. Install the nRF Connect SDK toolchain (v3.4.0, matching [`west.yml`](west.yml)):

   ```shell
   nrfutil sdk-manager install v3.4.0
   ```

### Initialize the workspace

Before initializing, start the toolchain environment:

```shell
nrfutil sdk-manager toolchain launch --ncs-version v3.4.0 --shell
```

To initialize the workspace folder (`smha-workspace`) where the firmware project and all nRF Connect SDK modules will be cloned, run:

```shell
west init -m https://github.com/nrfconnect/ncs-serial-modem-host-applications --mr main smha-workspace
cd smha-workspace/project
west update
```

The repository is now cloned into the `smha-workspace/project` folder, the west modules are downloaded, and you are ready to build an application.

### Build and flash

Follow the application documentation for hardware setup, build, flash, and (where applicable) cloud provisioning:

| Application | Documentation |
|-------------|---------------|
| **91m1** | [applications/91m1/doc/](applications/91m1/doc/README.md) |
| **93m1** | Not yet supported |

---

## License

See [LICENSE](LICENSE).

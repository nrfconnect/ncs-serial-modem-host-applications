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
| [`lib/modules/`](lib/modules/) | Shared zbus/SMF feature modules |
| [`applications/91m1/`](applications/91m1/) | nRF91M1 host application |
| [`applications/93m1/`](applications/93m1/) | nRF93M1 host application |

## Getting started

### Initialize a West workspace

```shell
west init -m https://github.com/nrfconnect/ncs-serial-modem-host-applications --mr main smha-workspace
cd smha-workspace/project
west update
```

### Build an application

```shell
cd applications/91m1
west build -b nrf54l15dk/nrf54l15/cpuapp/ns -p
```

```shell
cd applications/93m1
west build -b nrf54l15dk/nrf54l15/cpuapp -p
```

Board overlays under `applications/*/boards/` are placeholders

---

## License

See [LICENSE](LICENSE).

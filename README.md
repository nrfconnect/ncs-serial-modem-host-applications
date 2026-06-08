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

### Two-DK setup (91m1 + nRF9151 Serial Modem)

Development setup: **nRF54L15 DK** (host) wired to **nRF9151 DK** (Serial Modem).

**Board Configurator (both DKs):** disable VCOM0 on the nRF54L15; disable VCOM0/VCOM1 on the nRF9151. Set matching VDD (typically 1.8 V).

| nRF54L15 DK | nRF9151 DK | Signal |
|---|---|---|
| P0.00 | P0.03 | UART TX → RX |
| P0.01 | P0.02 | UART RX ← TX |
| P0.02 | P0.07 | UART RTS → CTS |
| P0.03 | P0.06 | UART CTS ← RTS |
| P1.11 | P0.31 | DTR |
| P1.12 | P0.30 | RI |
| **P1.10** | **P20 pin 7** | **nRESET** |
| GND | GND | Ground |

P0 = UART (connector P0). P1 = DTR, RI, and reset. Add a 1 kΩ series resistor on the reset wire if IO levels differ.

On host boot, `applications/91m1/src/modem_reset.c` pulses nRESET (500 ms), then waits for Serial Modem `"Ready"` before the cellular driver starts. Devicetree: `applications/91m1/boards/nrf54l15dk_nrf54l15_cpuapp_ns.overlay`.

---

## License

See [LICENSE](LICENSE).

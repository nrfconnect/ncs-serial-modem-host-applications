# Hardware Setup — 91m1_ppp + nRF9151 Serial Modem

The host application runs on an nRF54 Series DK and talks to an **nRF9151 DK** or **nRF9151 SMA DK** (Serial Modem) over UART with hardware flow control, DTR/RI, and an optional modem reset line.

The nRF9151 SMA DK uses the same board controller, GPIO pinout, and build target (`nrf9151dk/nrf9151/ns`) as the nRF9151 DK. The only hardware difference is the SMA antenna connector instead of a PCB antenna — **wiring is identical**.

Supported host boards:

| Host DK | Serial Modem wiring |
|---|---|
| [nRF54L15 DK](#nrf54l15-dk--nrf9151-dk) | P0 connector (uart30) |
| [nRF54LM20B DK](#nrf54lm20b-dk--nrf9151-dk) | P1/P2 connector (uart21) |
| [nRF54LM20B DK + nRF7002-EB2 (Wi-Fi location)](#nrf54lm20b-dk--nrf7002-eb2-wi-fi-location) | P1/P2 connector (uart21) |

## Board Configurator

On both DKs:

- Set matching VDD on both boards (typically **1.8 V**).
- Leave **VCOM1** enabled on the **nRF9151 / nRF9151 SMA DK**. Serial Modem logs to uart1 (P0.28/P0.29 → VCOM1), which is the only window into the modem side when the link misbehaves. The external host link uses uart2 on P0.02–P0.07 and does not collide with either VCOM, so neither has to be disabled.

Host-specific settings are listed in each section below.

## nRF54L15 DK + nRF9151 DK

Development setup: **nRF54L15 DK** (host) wired to **nRF9151 DK** (Serial Modem).

On the nRF54L15 DK:

- Disable **VCOM0** (releases uart30 on the P0 connector for the modem link).

### Wiring

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

- **P0 connector:** UART signals.
- **P1 connector:** DTR, RI, and modem reset.
- Add a **1 kΩ** series resistor on the reset wire if IO levels differ.

### Build

```shell
cd applications/91m1_ppp
west build -b nrf54l15dk/nrf54l15/cpuapp/ns -p
```

### Devicetree

[`boards/nrf54l15dk_nrf54l15_cpuapp_ns.overlay`](../boards/nrf54l15dk_nrf54l15_cpuapp_ns.overlay)

### Console

Open a serial terminal on **VCOM1** (uart20 — the secondary USB serial port on the nRF54L15 DK).

---

## nRF54LM20B DK + nRF9151 / SMA DK

Development setup: **nRF54LM20B DK** (host) wired to **nRF9151 DK** or **nRF9151 SMA DK** (Serial Modem).

On the nRF54LM20B DK, no Board Configurator changes are required — both VCOM ports stay enabled.

### Wiring

The Serial Modem link uses **uart21** on the P1 connector (P1.8/P1.9 for TX/RX, P1.23/P1.24 for RTS/CTS).

| nRF54LM20B DK | nRF9151 / SMA DK | Signal |
|---|---|---|
| P1.8 | P0.03 (P4) | UART TX → RX |
| P1.9 | P0.02 (P4) | UART RX ← TX |
| P1.23 | P0.07 (P4) | UART RTS → CTS |
| P1.24 | P0.06 (P4) | UART CTS ← RTS |
| P1.11 | P0.31 (P3) | DTR |
| P1.12 | P0.30 (P3) | RI |
| **P1.10** | **P20 pin 7** | **nRESET** |
| GND | GND | Ground |

- **Host P1/P2 connector:** UART (uart21 on P1.8/P1.9 and P1.23/P1.24), DTR (P1.11), RI (P1.12), modem reset (P1.10).
- **Modem side:** UART and HWFC on **P4**; DTR and RI on **P3**; nRESET on **P20 pin 7** (debug-out connector).
- Add a **1 kΩ** series resistor on the reset wire if IO levels differ.

> **Note:** DTR/RI use P1.11/P1.12, which conflict with the DK default uart21 HWFC pins. The overlay maps RTS/CTS to P1.23/P1.24 instead. All four UART wires plus DTR/RI must be connected for reliable operation.

> **Note:** The `UART_TX`/`UART_RX` psels in the host overlay must match the orientation in the table above — host TX on P1.8 drives the modem's RX (P0.03). Swapping the two leaves both sides transmitting into each other's transmitters, and the only symptom is that the modem never answers the init chat script.

### Build

```shell
cd applications/91m1_ppp
west build -b nrf54lm20dk/nrf54lm20b/cpuapp/ns -p
```

### Devicetree

[`boards/nrf54lm20dk_nrf54lm20b_cpuapp_ns.overlay`](../boards/nrf54lm20dk_nrf54lm20b_cpuapp_ns.overlay)

### Console

Open a serial terminal on **VCOM1** (uart20 — the secondary USB serial port on the nRF54LM20B DK).

Serial Modem logs appear on **VCOM1 of the nRF9151 / SMA DK** (uart1, P0.28/P0.29). Keep that terminal open alongside the host console when bringing up the link.

---

## nRF54LM20B DK + nRF7002-EB2 (Wi-Fi location)

Development setup: **nRF54LM20B DK** with **nRF7002-EB II** (EB2) on the **P18 expansion header**, wired to **nRF9151 DK** or **nRF9151 SMA DK** (Serial Modem). Use this configuration only when building with Wi-Fi location support — the EB2 shield is not required for the base PPP application.

### nRF7002-EB2 mounting

Mount the nRF7002-EB2 on the **P18 expansion header** on the nRF54LM20B DK. The expansion interface uses **P17** for GPIO/SPI signals and **P18** for 5 V power when the shield is plugged in.

On the nRF54LM20B DK:

- Disable **VCOM1** in Board Configurator (required by the nRF7002-EB2 shield — uart20 pins conflict with the expansion header).
- Keep **VCOM0** enabled for the shell console (uart30 on P0.06/P0.07).

### Wiring

The shield moves the host console to **uart30** (VCOM0). Serial Modem wiring is the same as the [plain nRF54LM20B setup](#wiring-1) above (uart21 on P1).

### Build

```shell
cd applications/91m1_ppp
west build -b nrf54lm20dk/nrf54lm20b/cpuapp/ns -p -- \
  -DSHIELD=nrf7002eb2 \
  -DEXTRA_CONF_FILE=overlay-location.conf \
  -DSB_EXTRA_CONF_FILE=sysbuild-location.conf
```

### Devicetree

[`boards/nrf54lm20dk_nrf54lm20b_cpuapp_ns.overlay`](../boards/nrf54lm20dk_nrf54lm20b_cpuapp_ns.overlay)

The Zephyr `nrf7002eb2` shield overlay provides the Wi-Fi companion IC devicetree and moves the console to uart30.

### Console

Open a serial terminal on **VCOM0** (primary USB serial port — uart30 on P0.06/P0.07). Do not use VCOM1 while the EB2 is attached.

Serial Modem logs appear on **VCOM1 of the nRF9151 / SMA DK** (uart1, P0.28/P0.29). Keep that terminal open alongside the host console when bringing up the link.

---

## Firmware behavior

On host boot, [`src/modem_reset.c`](../src/modem_reset.c) pulses nRESET (500 ms), then waits for the Serial Modem `"Ready"` string before the cellular driver starts.

## Serial Modem firmware

The 91m1_ppp host application is tested with Serial Modem firmware at commit [`e23c2bde08a83e8a2908f78ee19f2b2ff5c6e46e`](https://github.com/nrfconnect/ncs-serial-modem/commit/e23c2bde08a83e8a2908f78ee19f2b2ff5c6e46e). Check out that commit in the [ncs-serial-modem](https://github.com/nrfconnect/ncs-serial-modem) repository, then build from the Serial Modem application directory:

```shell
west build -p -b nrf9151dk/nrf9151/ns -- \
  -DEXTRA_CONF_FILE="overlay-ppp.conf;overlay-cmux.conf" \
  -DEXTRA_DTC_OVERLAY_FILE="overlay-external-mcu.overlay" \
  -DCONFIG_SM_LOG_LEVEL_DBG=y
```

The `overlay-external-mcu.overlay` file routes Serial Modem to **uart2** on P0.02/P0.03 (TX/RX) and P0.06/P0.07 (RTS/CTS), with DTR/RI on P0.31/P0.30. Without this overlay, the modem listens on the USB VCOM UART instead — the host will see `init_chat_script: timed out`.

See the [Serial Modem getting started guide](https://docs.nordicsemi.com/bundle/addon-serial_modem-latest/page/gsg_guide.html#building_and_running) for workspace setup and how to flash Serial Modem firmware on the nRF9151 / SMA DK.

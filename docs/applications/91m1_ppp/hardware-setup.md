# Hardware Wiring Guide

Follow these steps to wire an nRF54 Series DK (host) to an [nRF9151 DK](https://www.nordicsemi.com/Products/Development-hardware/nRF9151-DK) or [nRF9151 SMA DK](https://www.nordicsemi.com/Products/Development-hardware/nRF9151-SMA-DK) (Serial Modem) and bring up the link between them. The two DKs talk over UART with hardware flow control, DTR/RI, and an optional modem reset line.

The nRF9151 SMA DK uses the same board controller, GPIO pinout, and build target (`nrf9151dk/nrf9151/ns`) as the nRF9151 DK. The only hardware difference is the SMA antenna connector instead of a PCB antenna, so **every step is identical for both**.

The guide covers the following host setups. Pick one before you start; some steps differ between them.

| Host setup | Serial Modem link on the host |
|---|---|
| nRF54L15 DK | **P0** connector (`uart30`) |
| nRF54LM20B DK | **P1**/**P2** connector (`uart21`) |
| nRF54LM20B DK + nRF7002-EB II (Wi-Fi® location) | **P1**/**P2** connector (`uart21`) |

The nRF7002-EB II is only needed for Wi-Fi location. The base PPP application runs without it.

## Step 1: Gather the hardware and tools

You need:

- An nRF9151 DK or nRF9151 SMA DK with a SIM card.
- One host DK: nRF54L15 DK or nRF54LM20B DK.
- An nRF7002-EB II, only for the Wi-Fi location setup.
- Eight jumper wires: four for UART, one each for DTR, RI and modem reset, and one for ground.
- A **1 kΩ** resistor for the reset wire, if the two DKs run at different IO voltages.
- Two USB cables, one per DK.
- [nRF Connect for Desktop](https://www.nordicsemi.com/Products/Development-tools/nRF-Connect-for-Desktop) with the [Board Configurator app](https://docs.nordicsemi.com/bundle/nrf-connect-board-configurator/page/index.html), and [nRF Util](https://www.nordicsemi.com/Products/Development-tools/nRF-Util) for flashing.

## Step 2: Configure the nRF9151 DK in Board Configurator

Each Board Configurator switch connects or disconnects a group of DK pins from the interface MCU, which is the on-board debugger behind the USB virtual serial ports (VCOM0 and VCOM1). Pins that carry the link between the two DKs must be disconnected, otherwise the interface MCU drives them too.

1. Connect the nRF9151 DK to your computer over USB and turn it on.
1. Open Board Configurator and select the nRF9151 DK.
1. Set **VCOM0** to **Disconnected**. This frees UART0 (**P0.26**/**P0.27** data, **P0.14**/**P0.15** flow control) for the host link.
1. Check that **VCOM0 HWFC** also reads **Disconnected**, and set it if not. If it stays connected, the interface MCU holds the modem's CTS deasserted through 150 Ω series resistors, and the modem never answers the host.
1. Leave **VCOM1** and **VCOM1 HWFC** connected. VCOM1 carries the Serial Modem logs (UART1, **P0.28**/**P0.29**).
1. Note the **VDD** setting. You set the host DK to the same value in [Step 4](#step-4-configure-the-host-dk-in-board-configurator); 1.8 V is typical.
1. Write the configuration to the board.

## Step 3: Flash the Serial Modem firmware

The host application needs the nRF91M1 variant of the Serial Modem firmware, which enables PPP and CMUX on UART0 using the nRF91M1 pinout. Any other variant listens on the USB serial port instead, and the host never gets an answer.

1. Download `serial_modem_<tag>_nrf9151dk_nrf91m1.zip` from the newest [ncs-serial-modem release](https://github.com/nrfconnect/ncs-serial-modem/releases) that ships it, or use the copy at the top level of your [SMHA release bundle](../../release-artifacts.md#serial-modem-firmware-nrf9151-dk).
1. Extract the zip.
1. Flash the `.hex` on the nRF9151 DK. `--recover` erases the whole chip first:

    ```shell
    nrfutil device program --firmware serial_modem_<tag>_nrf9151dk_nrf91m1.hex --recover
    ```

To match an unreleased Serial Modem commit instead, build the modem application yourself; see the [Serial Modem getting started guide](https://docs.nordicsemi.com/bundle/addon-serial_modem-latest/page/gsg_guide.html#building_and_running) for workspace setup and build arguments.

## Step 4: Configure the host DK in Board Configurator

1. Connect the host DK to your computer over USB and turn it on.
1. Open Board Configurator and select the host DK.
1. Set **VDD** to the same value as the nRF9151 DK.
1. Change the VCOM switches for your host setup:

    - **nRF54L15 DK:** Set **VCOM0** and **VCOM0 HWFC** to **Disconnected**. VCOM0 is the P0 connector (**P0.00**–**P0.03**, `uart30`), which carries the modem link. The host console uses VCOM1 (`uart20`).
    - **nRF54LM20B DK:** Leave all VCOM switches connected. The modem link uses `uart21` on pins the interface MCU doesn't touch, and the host console uses VCOM0 (`uart30`).
    - **nRF54LM20B DK + nRF7002-EB II:** Set **VCOM1** and **VCOM1 HWFC** to **Disconnected**, and leave VCOM0 connected for the host console (`uart30` on **P0.06**/**P0.07**).

1. Write the configuration to the board.

## Step 5: Mount the nRF7002-EB II (Wi-Fi location only)

Skip this step unless you are building with Wi-Fi location.

1. Power off the nRF54LM20B DK.
1. Mount the EB II on the **P18 expansion header**. The expansion interface uses **P17** for GPIO/SPI signals and **P18** for 5 V power when the shield is plugged in.

## Step 6: Wire the DKs together

Power off both DKs, then connect the eight wires from the table for your host DK. The nRF7002-EB II setup uses the same wiring as the plain nRF54LM20B DK.

**nRF54L15 DK:**

| nRF54L15 DK | nRF9151 DK | Signal |
|---|---|---|
| **P0.00** | **P0.26** | UART TX → RX |
| **P0.01** | **P0.27** | UART RX ← TX |
| **P0.02** | **P0.15** | UART RTS → CTS |
| **P0.03** | **P0.14** | UART CTS ← RTS |
| **P1.11** | **P0.31** | DTR |
| **P1.12** | **P0.30** | RI |
| **P1.10** | **P5 pin 5** | **nRESET** |
| GND | GND | Ground |

On the host, the UART pins are on the **P0** connector, and DTR, RI and modem reset are on the **P1** connector.

**nRF54LM20B DK:**

| nRF54LM20B DK | nRF9151 / SMA DK | Signal |
|---|---|---|
| **P1.8** | **P0.26** | UART TX → RX |
| **P1.9** | **P0.27** | UART RX ← TX |
| **P1.23** | **P0.15** | UART RTS → CTS |
| **P1.24** | **P0.14** | UART CTS ← RTS |
| **P1.11** | **P0.31** (**P3**) | DTR |
| **P1.12** | **P0.30** (**P3**) | RI |
| **P1.10** | **P5 pin 5** | **nRESET** |
| GND | GND | Ground |

All host pins are on the **P1**/**P2** connector. DTR and RI on **P1.11**/**P1.12** take the DK's default `uart21` flow-control pins, so the host overlay moves RTS/CTS to **P1.23**/**P1.24**.

On the nRF9151 DK, the UART0 pins are on the DK edge, DTR and RI are on **P3**, and nRESET is on **P5 pin 5**.

When wiring, check the following:

- **The UART pairs cross.** Host TX goes to modem RX, and host RTS goes to modem CTS on **P0.15**, not **P0.14**, which is the modem's RTS. Wiring either pair straight through leaves the modem silent while it still boots and takes reset pulses normally.
- **Connect all four UART wires plus DTR and RI.** The link is unreliable without flow control or DTR.
- **Add the 1 kΩ series resistor** on the reset wire if the two DKs run at different IO voltages.
- **Don't jumper P0.31 to GND** on the nRF9151 DK. That jumper is only for a PC host; this application drives DTR from host pin **P1.11**.

## Step 7: Build and flash the host application

Run `west` inside the nRF Connect SDK toolchain environment; see [Initialize the workspace](../../getting-started.md#initialize-the-workspace). Power on both DKs, then build and flash for your host setup.

**nRF54L15 DK:**

```shell
cd applications/91m1_ppp
west build -b nrf54l15dk/nrf54l15/cpuapp/ns -p
west flash --recover
```

**nRF54LM20B DK:**

```shell
cd applications/91m1_ppp
west build -b nrf54lm20dk/nrf54lm20b/cpuapp/ns -p
west flash --recover
```

**nRF54LM20B DK + nRF7002-EB II:**

```shell
cd applications/91m1_ppp
west build -b nrf54lm20dk/nrf54lm20b/cpuapp/ns -p -- \
  -DSHIELD=nrf7002eb2 \
  -DEXTRA_CONF_FILE=overlay-location.conf \
  -DSB_EXTRA_CONF_FILE=sysbuild-location.conf
west flash --recover
```

## Step 8: Verify the link

1. Open a serial terminal at 115200 baud on the host console:

    - **nRF54L15 DK:** VCOM1, the secondary USB serial port.
    - **nRF54LM20B DK**, with or without the nRF7002-EB II: VCOM0, the primary USB serial port. Don't use VCOM1 while the EB II is attached.

1. Open a second serial terminal at 1000000 baud on VCOM1 of the nRF9151 DK to see the Serial Modem logs. Keep both open while you bring up the link.
1. Reset the host DK. On boot, the host pulses the modem's nRESET for 500 ms and gives it 2 s to start before the cellular driver opens the link.
1. Wait for `Network connected` in the host console. This means the PPP link through the modem is up.

If the host logs `init_chat_script: timed out`, the modem isn't answering. Check, in order:

- The nRF9151 DK runs the `nrf91m1` Serial Modem variant from [Step 3](#step-3-flash-the-serial-modem-firmware).
- VCOM0 **and** VCOM0 HWFC are disconnected on the nRF9151 DK ([Step 2](#step-2-configure-the-nrf9151-dk-in-board-configurator)).
- The TX/RX and RTS/CTS pairs cross as shown in [Step 6](#step-6-wire-the-dks-together).

## Reference

### Board Configurator settings

All VCOM switches default to Connected; the settings in bold are the ones to change.

| Setting | nRF9151 / SMA DK | nRF54L15 DK | nRF54LM20B DK | nRF54LM20B DK + nRF7002-EB II |
|---|---|---|---|---|
| VDD | Same as host (typically 1.8 V) | Same as modem | Same as modem | Same as modem |
| VCOM0 | **Disconnected** | **Disconnected** | Connected | Connected |
| VCOM0 HWFC | **Disconnected** | **Disconnected** | Connected | Connected |
| VCOM1 | Connected | Connected | Connected | **Disconnected** |
| VCOM1 HWFC | Connected | Connected | Connected | **Disconnected** |

### Serial Modem UART pinout

The `*_nrf9151dk_nrf91m1.zip` firmware uses the [nRF91M1 pinout](https://nrfconnectdocs.nordicsemi.com/addons/addon-serial_modem/latest/main/uart_configuration.html#nrf91m1-pre-programmed-sm-application) on UART0: **P0.27** TX, **P0.26** RX, **P0.14** RTS, **P0.15** CTS. DTR is on **P0.31** and RI on **P0.30**. Logs go to UART1 (**P0.28**/**P0.29**), exposed as VCOM1.

### Devicetree overlays

The host pin assignments come from the board overlays:

- nRF54L15 DK: [`boards/nrf54l15dk_nrf54l15_cpuapp_ns.overlay`](https://github.com/nrfconnect/ncs-serial-modem-host-applications/blob/main/applications/91m1_ppp/boards/nrf54l15dk_nrf54l15_cpuapp_ns.overlay)
- nRF54LM20B DK, with or without the nRF7002-EB II: [`boards/nrf54lm20dk_nrf54lm20b_cpuapp_ns.overlay`](https://github.com/nrfconnect/ncs-serial-modem-host-applications/blob/main/applications/91m1_ppp/boards/nrf54lm20dk_nrf54lm20b_cpuapp_ns.overlay). With the shield, the Zephyr `nrf7002eb2` shield overlay adds the Wi-Fi companion IC devicetree.

If you change the wiring, update the matching overlay. The `UART_TX`/`UART_RX` psels must keep the orientation shown in [Step 6](#step-6-wire-the-dks-together).

The modem reset pulse on host boot comes from [`src/modem_reset.c`](https://github.com/nrfconnect/ncs-serial-modem-host-applications/blob/main/applications/91m1_ppp/src/modem_reset.c).

### CI

CI on-target tests resolve and flash the newest upstream Serial Modem release that ships the nRF91M1 bundle before each 91m1 run, and capture the modem logs on VCOM1 at 1000000 baud. Static settings such as `console_baudrate` live in [`tests/on_target/ci/serial_modem_firmware.yml`](https://github.com/nrfconnect/ncs-serial-modem-host-applications/blob/main/tests/on_target/ci/serial_modem_firmware.yml). Set `SERIAL_MODEM_RELEASE` to pin a specific tag locally. See [Serial logs](../../ci-and-contribution.md#serial-logs).

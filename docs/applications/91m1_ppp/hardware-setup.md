# Hardware reference

This page is the reference for the hardware of the nRF91M1 PPP Host Application: an nRF54 Series DK (host) wired to an [nRF9151 DK](https://www.nordicsemi.com/Products/Development-hardware/nRF9151-DK) or [nRF9151 SMA DK](https://www.nordicsemi.com/Products/Development-hardware/nRF9151-SMA-DK) (Serial Modem). The two DKs talk over UART with hardware flow control, DTR/RI, and a modem reset line.

> [!TIP]
> To set up the hardware step by step, follow [Set up the hardware](../../getting-started/91m1-ppp.md#step-1-set-up-the-hardware) in the getting-started guide. It includes the wiring tables for each host DK.

The nRF9151 SMA DK uses the same board controller, GPIO pinout, and build target (`nrf9151dk/nrf9151/ns`) as the nRF9151 DK. The only hardware difference is the SMA antenna connector instead of a PCB antenna, so everything on this page applies to both.

| Host setup | Serial Modem link on the host | Host console |
|---|---|---|
| nRF54L15 DK | **P0** connector (`uart30`) | VCOM1 (`uart20`) |
| nRF54LM20B DK | **P1**/**P2** connector (`uart21`) | VCOM0 (`uart30`) |
| nRF54LM20B DK + nRF7002-EB II (Wi-Fi® location) | **P1**/**P2** connector (`uart21`) | VCOM0 (`uart30`) |

## Board Configurator settings

All VCOM switches default to Connected; the settings in bold are the ones to change.

| Setting | nRF9151 / SMA DK | nRF54L15 DK | nRF54LM20B DK | nRF54LM20B DK + nRF7002-EB II |
|---|---|---|---|---|
| VDD | Same as host (typically 1.8 V) | Same as modem | Same as modem | Same as modem |
| VCOM0 | **Disconnected** | **Disconnected** | Connected | Connected |
| VCOM0 HWFC | **Disconnected** | **Disconnected** | Connected | Connected |
| VCOM1 | Connected | Connected | Connected | **Disconnected** |
| VCOM1 HWFC | Connected | Connected | Connected | **Disconnected** |

## Serial Modem UART pinout

The `*_nrf9151dk_nrf91m1.zip` firmware uses the [nRF91M1 pinout](https://nrfconnectdocs.nordicsemi.com/addons/addon-serial_modem/latest/main/uart_configuration.html#nrf91m1-pre-programmed-sm-application) on UART0: **P0.27** TX, **P0.26** RX, **P0.14** RTS, **P0.15** CTS. DTR is on **P0.31** and RI on **P0.30**. Logs go to UART1 (**P0.28**/**P0.29**), exposed as VCOM1 at 1000000 baud.

## Devicetree overlays

The host pin assignments come from the board overlays:

- nRF54L15 DK: [`boards/nrf54l15dk_nrf54l15_cpuapp_ns.overlay`](https://github.com/nrfconnect/ncs-serial-modem-host-applications/blob/main/applications/91m1_ppp/boards/nrf54l15dk_nrf54l15_cpuapp_ns.overlay)
- nRF54LM20B DK, with or without the nRF7002-EB II: [`boards/nrf54lm20dk_nrf54lm20b_cpuapp_ns.overlay`](https://github.com/nrfconnect/ncs-serial-modem-host-applications/blob/main/applications/91m1_ppp/boards/nrf54lm20dk_nrf54lm20b_cpuapp_ns.overlay). With the shield, the Zephyr `nrf7002eb2` shield overlay adds the Wi-Fi companion IC devicetree.

If you change the wiring, update the matching overlay. The `UART_TX`/`UART_RX` psels must keep the orientation shown in the [wiring tables](../../getting-started/91m1-ppp.md#wire-the-dks-together).

The modem reset pulse on host boot comes from [`src/modem_reset.c`](https://github.com/nrfconnect/ncs-serial-modem-host-applications/blob/main/applications/91m1_ppp/src/modem_reset.c). It pulses nRESET for 500 ms and gives the modem 2 s to start before the cellular driver opens the link.

## CI

CI on-target tests resolve and flash the newest upstream Serial Modem release that ships the nRF91M1 bundle before each 91m1 run, and capture the modem logs on VCOM1 at 1000000 baud. Static settings such as `console_baudrate` live in [`tests/on_target/ci/serial_modem_firmware.yml`](https://github.com/nrfconnect/ncs-serial-modem-host-applications/blob/main/tests/on_target/ci/serial_modem_firmware.yml). Set `SERIAL_MODEM_RELEASE` to pin a specific tag locally. See [Serial logs](../../ci-and-contribution.md#serial-logs).

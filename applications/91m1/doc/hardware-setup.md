# Hardware Setup — 91m1 + nRF9151 Serial Modem

Development setup: **nRF54L15 DK** (host) wired to **nRF9151 DK** (Serial Modem).

## Board Configurator

On both DKs:

- Disable VCOM0 on the nRF54L15 (releases uart30 for the modem link).
- Disable VCOM0 and VCOM1 on the nRF9151.
- Set matching VDD on both boards (typically 1.8 V).

## Wiring

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

- P0 connector: UART signals.
- P1 connector: DTR, RI, and modem reset.
- Add a 1 kΩ series resistor on the reset wire if IO levels differ.

## Firmware behavior

On host boot, [`src/modem_reset.c`](../src/modem_reset.c) pulses nRESET (500 ms), then waits for the Serial Modem `"Ready"` string before the cellular driver starts.

Devicetree overlay: [`boards/nrf54l15dk_nrf54l15_cpuapp_ns.overlay`](../boards/nrf54l15dk_nrf54l15_cpuapp_ns.overlay).

## Serial Modem firmware

The 91m1 host application is tested with Serial Modem firmware at commit [`e23c2bde08a83e8a2908f78ee19f2b2ff5c6e46e`](https://github.com/nrfconnect/ncs-serial-modem/commit/e23c2bde08a83e8a2908f78ee19f2b2ff5c6e46e). Build and flash the nRF9151 DK with PPP and CMUX enabled (`overlay-ppp.conf` and `overlay-cmux.conf`).

See the [Serial Modem getting started guide](https://docs.nordicsemi.com/bundle/addon-serial_modem-latest/page/gsg_guide.html#building_and_running) for how to build and flash Serial Modem firmware on the nRF91.

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

Flash the nRF9151 DK with Serial Modem firmware built with PPP and CMUX enabled (stock nRF91M1 build or `overlay-ppp.conf` + `overlay-cmux.conf`).

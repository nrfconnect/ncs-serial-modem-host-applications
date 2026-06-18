# Serial Modem Host 93m1 (AT)

Minimal tracker application for the **nRF93M1 Serial Modem** using raw AT over UART. The modem
handles connectivity, TLS, and nRF Cloud — the host attaches to the network and runs periodic
location and battery syncs.

## Building

```
west build -p -b nrf93m1dk/nrf54l15/cpuapp/ns
```

## Bring-up

Provision the device to nRF Cloud.

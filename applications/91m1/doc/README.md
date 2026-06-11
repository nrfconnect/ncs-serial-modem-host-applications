# nRF91M1 Host Application Documentation

Guides for the [91m1](../) host application on nRF54L15.

| Guide | Description |
|-------|-------------|
| [Hardware setup](hardware-setup.md) | Two-DK wiring, board configurator, devicetree |
| [nRF Cloud CoAP onboarding](nrf-cloud-onboarding.md) | Credential install and portal onboarding (CoAP) |

Build:

```shell
cd applications/91m1
west build -b nrf54l15dk/nrf54l15/cpuapp/ns -p
```

Flash:

```shell
west flash --recover
```

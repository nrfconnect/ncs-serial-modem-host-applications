# Serial Modem Host Applications

Host-side firmware for Nordic Smart Modem modules. The applications run on a host MCU, use a Serial Modem for cellular connectivity, and send data to nRF Cloud. They are built on the [nRF Connect SDK](https://www.nordicsemi.com/Products/Development-software/nRF-Connect-SDK) and share the modular **zbus and SMF** architecture of the [Asset Tracker Template](https://github.com/nrfconnect/Asset-Tracker-Template).

## Applications

| Your hardware | Application | What it does |
|---------------|-------------|--------------|
| nRF54L15 DK or nRF54LM20B DK, wired to an nRF9151 DK | [nRF91M1 PPP Host Application](applications/91m1_ppp/README.md) | The host connects to nRF Cloud over its own IP stack, with the modem carrying IP over PPP. Includes FOTA, Memfault, and optional Wi-Fi location. |
| nRF93M1 DK | [nRF93M1 PPP Host Application](applications/93m1_ppp/README.md) | The same host-side cloud connection on the integrated nRF93M1 modem, with FOTA, Memfault, and cell and Wi-Fi location. |
| nRF93M1 DK | [nRF93M1 AT Host Application](applications/93m1_at/README.md) | A minimal host. The modem connects to nRF Cloud with its built-in client, and the host drives it with AT commands. |

## Get started

**[Getting started](getting-started/index.md)** takes you from choosing your application to a device sending data to nRF Cloud, using the command line.

## Reference

- [Architecture](architecture.md): the module design shared by all applications.
- [Application FOTA](app-fota.md): update the PPP host applications over the air through nRF Cloud.
- [Release artifacts](release-artifacts.md): what each release zip contains and how to flash it.
- [CI and contribution](ci-and-contribution.md): continuous integration, releases, and commit message guidelines.

The source repository lives at [nrfconnect/ncs-serial-modem-host-applications](https://github.com/nrfconnect/ncs-serial-modem-host-applications#readme).

# Serial Modem Host Applications

Host-side firmware for Nordic Smart Modem modules, built on the [nRF Connect SDK](https://www.nordicsemi.com/Products/Development-software/nRF-Connect-SDK) and follows the modular **zbus and SMF** architecture of the [Asset Tracker Template](https://github.com/nrfconnect/Asset-Tracker-Template).

## Applications

| Application                                                       | Cloud connectivity                                                                              |
|-------------------------------------------------------------------|-------------------------------------------------------------------------------------------------|
| [nRF91M1 Host Application](applications/91m1_ppp/README.md)       | Host terminates CoAP/DTLS to nRF Cloud itself. PPP carries IP to the nRF91M1 modem              |
| [nRF93M1 Host Application](applications/93m1_ppp/README.md)       | Host terminates CoAP/DTLS to nRF Cloud itself. PPP carries IP to the nRF93M1 modem              |
| [nEF93M1 Serial Modem Host (AT)](applications/93m1_at/README.md)  | Modem terminates the connection itself with its built-in AT client. Host just sends AT commands |

## Reference

- [Getting started](getting-started.md) — Toolchain installation, workspace setup, and building and flashing an application.
- [CI and contribution](ci-and-contribution.md) — Continuous integration, releases, and commit message guidelines.
- [Release artifacts](release-artifacts.md) — Pre-built firmware bundles, file descriptions, and flash commands.

The source repository lives at [nrfconnect/ncs-serial-modem-host-applications](https://github.com/nrfconnect/ncs-serial-modem-host-applications#readme).

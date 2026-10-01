# Serial Modem Host Applications

Host-side firmware for Nordic Smart Modem modules, built on the [nRF Connect SDK](https://www.nordicsemi.com/Products/Development-software/nRF-Connect-SDK) and follows the modular **zbus and SMF** architecture of the [Asset Tracker Template](https://github.com/nrfconnect/Asset-Tracker-Template).

## Applications

| Application | Hardware | Board target | Cloud connectivity |
|-------------|----------|--------------|--------------------|
| [nRF91M1 PPP Host Application](applications/91m1_ppp/README.md) | nRF54L15 DK + nRF9151 DK<br>nRF54LM20B DK + nRF9151 DK | `nrf54l15dk/nrf54l15/cpuapp/ns`<br>`nrf54lm20dk/nrf54lm20b/cpuapp/ns` | Host terminates CoAP/DTLS to nRF Cloud itself. PPP carries IP to the nRF91M1 modem |
| [nRF93M1 PPP Host Application](applications/93m1_ppp/README.md) | nRF93M1 DK | `nrf93m1dk/nrf54l15/cpuapp/ns` | Host terminates CoAP/DTLS to nRF Cloud itself. PPP carries IP to the nRF93M1 modem |
| [nRF93M1 AT Host Application](applications/93m1_at/README.md) | nRF93M1 DK | `nrf93m1dk/nrf54l15/cpuapp/ns` | Modem terminates the connection itself with its built-in AT client. Host just sends AT commands |

See [Getting started](getting-started.md) for how to build and flash.

## Reference

- [Getting started](getting-started.md) — Toolchain installation, workspace setup, and building and flashing an application.
- [Application FOTA](app-fota.md) — Issue a FOTA update to the PPP host applications from the command line through nRF Cloud.
- [CI and contribution](ci-and-contribution.md) — Continuous integration, releases, and commit message guidelines.
- [Release artifacts](release-artifacts.md) — Pre-built firmware bundles, file descriptions, and flash commands.

The source repository lives at [nrfconnect/ncs-serial-modem-host-applications](https://github.com/nrfconnect/ncs-serial-modem-host-applications#readme).

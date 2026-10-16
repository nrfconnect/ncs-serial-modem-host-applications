# Serial Modem Host Applications

[![Build, Test, and Release](https://img.shields.io/github/actions/workflow/status/nrfconnect/ncs-serial-modem-host-applications/ci.yml?event=schedule&branch=main&label=Build%2C%20Test%2C%20and%20Release)](https://github.com/nrfconnect/ncs-serial-modem-host-applications/actions/workflows/ci.yml?query=branch%3Amain+event%3Aschedule)

Host-side firmware for Nordic Smart Modem modules. The applications run on a host MCU, use a Serial Modem for cellular connectivity, and send data to nRF Cloud. They are built on [nRF Connect SDK](https://www.nordicsemi.com/Products/Development-software/nRF-Connect-SDK) and share the modular **zbus + SMF** architecture of the [Asset Tracker Template](https://github.com/nrfconnect/Asset-Tracker-Template).

## Applications

| Your hardware | Application | What it does |
|---------------|-------------|--------------|
| [nRF54L15 DK](https://www.nordicsemi.com/Products/Development-hardware/nRF54L15-DK) or [nRF54LM20B DK](https://www.nordicsemi.com/Products/Development-hardware/nRF54LM20-DK), wired to an [nRF9151 DK](https://www.nordicsemi.com/Products/Development-hardware/nRF9151-DK) | [nRF91M1 PPP Host Application](docs/applications/91m1_ppp/README.md) | The host connects to nRF Cloud over its own IP stack, with the modem carrying IP over PPP. Includes FOTA, Memfault, and optional Wi-Fi location. |
| [nRF93M1 DK](https://www.nordicsemi.com/Products/Development-hardware/nRF93M1-DK) | [nRF93M1 PPP Host Application](docs/applications/93m1_ppp/README.md) | The same host-side cloud connection on the integrated nRF93M1 modem, with FOTA, Memfault, and cell and Wi-Fi location. |
| [nRF93M1 DK](https://www.nordicsemi.com/Products/Development-hardware/nRF93M1-DK) | [nRF93M1 AT Host Application](docs/applications/93m1_at/README.md) | A minimal host. The modem connects to nRF Cloud with its built-in client, and the host drives it with AT commands. |

## Get started

**[Getting started](docs/getting-started/index.md)** takes you from choosing your application to a device sending data to nRF Cloud, using the command line. Pre-built firmware for every application is on the [releases page](https://github.com/nrfconnect/ncs-serial-modem-host-applications/releases).

## More documentation

- [Architecture](docs/architecture.md): the module design shared by all applications.
- [Application FOTA](docs/app-fota.md): update the PPP host applications over the air through nRF Cloud.
- [Release artifacts](docs/release-artifacts.md): what each release zip contains and how to flash it.
- [CI and contribution](docs/ci-and-contribution.md): repository structure, continuous integration, releases, and commit message guidelines.

## License

See [LICENSE](LICENSE).

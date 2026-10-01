# Serial Modem Host Applications

[![Build, Test, and Release](https://img.shields.io/github/actions/workflow/status/nrfconnect/ncs-serial-modem-host-applications/ci.yml?event=schedule&branch=main&label=Build%2C%20Test%2C%20and%20Release)](https://github.com/nrfconnect/ncs-serial-modem-host-applications/actions/workflows/ci.yml?query=branch%3Amain+event%3Aschedule)

Host-side firmware for Nordic Smart Modem modules. This repository is built on [nRF Connect SDK](https://www.nordicsemi.com/Products/Development-software/nRF-Connect-SDK) (NCS) and follows the modular **zbus + SMF** architecture used by the [Asset Tracker Template](https://github.com/nrfconnect/Asset-Tracker-Template).

## Applications

| Application | Hardware | Cloud connectivity |
|-------------|----------|--------------------|
| [nRF91M1 PPP Host Application](docs/applications/91m1_ppp/README.md) | [nRF54L15 DK](https://www.nordicsemi.com/Products/Development-hardware/nRF54L15-DK) + [nRF9151 DK](https://www.nordicsemi.com/Products/Development-hardware/nRF9151-DK)<br>[nRF54LM20B DK](https://www.nordicsemi.com/Products/Development-hardware/nRF54LM20-DK) + [nRF9151 DK](https://www.nordicsemi.com/Products/Development-hardware/nRF9151-DK) | Host terminates CoAP/DTLS to nRF Cloud itself. PPP just carries IP to the nRF91M1 modem |
| [nRF93M1 PPP Host Application](docs/applications/93m1_ppp/README.md) | [nRF93M1 DK](https://www.nordicsemi.com/Products/Development-hardware/nRF93M1-DK) | Host terminates CoAP/DTLS to nRF Cloud itself. PPP just carries IP to the nRF93M1 modem |
| [nRF93M1 AT Host Application](docs/applications/93m1_at/README.md) | [nRF93M1 DK](https://www.nordicsemi.com/Products/Development-hardware/nRF93M1-DK) | Modem terminates the connection itself with its built-in AT client. Host just sends AT commands |

## Getting started

See [Getting started](docs/getting-started.md) for toolchain installation, workspace setup, and building and flashing an application with either nRF Connect for VS Code or the command line.

For pre-built firmware, download the [latest release](https://github.com/nrfconnect/ncs-serial-modem-host-applications/releases) and see [Release artifacts](docs/release-artifacts.md).

## Application FOTA

See [Application FOTA](docs/app-fota.md) for how to issue a FOTA update to the PPP host applications from the command line through nRF Cloud.

---

## Contributing

See [CI and contribution](docs/ci-and-contribution.md) for the repository structure, continuous integration, releases, and commit message guidelines. Pre-built firmware bundles are described in [Release artifacts](docs/release-artifacts.md).

## License

See [LICENSE](LICENSE).

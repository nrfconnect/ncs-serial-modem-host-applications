# Serial Modem Host Applications

Host-side firmware for Nordic Smart Modem modules, built on the [nRF Connect SDK](https://www.nordicsemi.com/Products/Development-software/nRF-Connect-SDK) (NCS) and following the modular **zbus + SMF** architecture of the [Asset Tracker Template](https://github.com/nrfconnect/Asset-Tracker-Template).

## Applications

| Application | Documentation | Cloud connectivity |
|-------------|---------------|--------------------|
| **91m1_ppp** | [91m1_ppp](applications/91m1_ppp/README.md) | Host terminates CoAP/DTLS to nRF Cloud itself. PPP carries IP to the nRF91M1 modem |
| **93m1_ppp** | [93m1_ppp](applications/93m1_ppp/README.md) | Host terminates CoAP/DTLS to nRF Cloud itself. PPP carries IP to the nRF93M1 modem |
| **93m1_at** | [93m1_at](applications/93m1_at/README.md) | Modem terminates the connection itself with its built-in AT client. Host just sends AT commands |

## Reference

- [CI and contribution](ci-and-contribution.md) — continuous integration, releases, and commit message guidelines
- [Release artifacts](release-artifacts.md) — pre-built firmware bundles, file descriptions, and flash commands

The source repository, build instructions, and workspace setup live in the [project README](https://github.com/nrfconnect/ncs-serial-modem-host-applications#readme).

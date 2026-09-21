# Getting started

This guide walks you through getting a Serial Modem host application built and running on supported hardware. It covers:

* Setting up the development environment
* Initializing the west workspace
* Building the application
* Flashing the application to the device

There are two options for setting up the project, depending on your preferred development environment:

* **Option 1**: Using Visual Studio Code and the [nRF Connect for VS Code](https://docs.nordicsemi.com/bundle/nrf-connect-vscode/page/index.html) extension (recommended).
* **Option 2**: Using the command line and nRF Util.

For pre-built binaries that do not require a build environment, refer to the [latest release](https://github.com/nrfconnect/ncs-serial-modem-host-applications/releases) and the [Release artifacts](release-artifacts.md) documentation.

## Build targets

Both options use the same board targets. Pick the row that matches your hardware:

| Application | Hardware | Board target |
|-------------|----------|--------------|
| [nRF91M1 Host Application](applications/91m1_ppp/README.md) | nRF54L15 DK + nRF9151 Serial Modem | `nrf54l15dk/nrf54l15/cpuapp/ns` |
| [nRF91M1 Host Application](applications/91m1_ppp/README.md) | nRF54LM20B DK + nRF9151 Serial Modem | `nrf54lm20dk/nrf54lm20b/cpuapp/ns` |
| [nRF93M1 Host Application](applications/93m1_ppp/README.md) | nRF93M1 DK | `nrf93m1dk/nrf54l15/cpuapp/ns` |
| [nEF93M1 Serial Modem Host (AT)](applications/93m1_at/README.md) | nRF93M1 DK | `nrf93m1dk/nrf54l15/cpuapp/ns` |


## Option 1: nRF Connect for VS Code (recommended)

1. Open the **nRF Connect** extension panel from the VS Code activity bar.
1. In the application picker, select the **Browse nRF Connect SDK add-on Index** option.
1. Search for **Serial Modem Host Applications** and create the project.
1. Click **Add build configuration** under the application entry to open the build configuration dialog. Set the **Board target** to the value from the [Build targets](#build-targets) table for your hardware.

1. Keep the other fields at their defaults and click **Generate and build** to build the application.

1. Flash the device.

    In the **Actions** panel, use **Erase and flash to board** for the first flash of a device. A plain **Flash** can fail on a device that has never been programmed with this application, or that is read-back protected. If it does, run `west flash --recover` once from the nRF Connect terminal, then continue using the extension.


        > **Note:** **Erase and flash to board** (and `--recover`) wipes TF-M Protected Storage, so any installed nRF Cloud device credentials are lost and must be provisioned again. For routine re-flashing during development, use the plain **Flash** action to keep the credentials in place.

    For nRF91M1 setups, two development kits are connected at the same time. When prompted for which device to flash, select the debugger ID of the **host** DK (nRF54L15 or nRF54LM20B), not the nRF9151 Serial Modem DK.

1. After flashing, open a serial terminal on the host console port to view the device logs. The [Serial Terminal app](https://www.nordicsemi.com/Products/Development-tools/nRF-Connect-for-Desktop) (part of nRF Connect for Desktop) is recommended; PuTTY, Tera Term, and minicom also work. The console port depends on the board — see the [application guides](#application-guides).

1. Follow the [application guides](#application-guides) for hardware setup and cloud onboarding for the respective application.

For more details on how to use nRF Connect for VS Code, refer to the [nRF Connect for VS Code documentation](https://docs.nordicsemi.com/bundle/nrf-connect-vscode/page/index.html).

## Option 2: Command line

### Install prerequisites

1. Install nRF Util by following the instructions in the [nRF Util documentation](https://docs.nordicsemi.com/bundle/nrfutil/page/guides/installing.html).

1. Install the SDK manager command:

    ```shell
    nrfutil install sdk-manager
    ```

1. Install the nRF Connect SDK toolchain (v3.4.0, matching [`west.yml`](https://github.com/nrfconnect/ncs-serial-modem-host-applications/blob/main/west.yml)):

    ```shell
    nrfutil sdk-manager install v3.4.0
    ```

### Initialize the workspace

Before initializing, start the toolchain environment:

```shell
nrfutil sdk-manager toolchain launch --ncs-version v3.4.0 --shell
```

You can also run a single command within a specific nRF Connect SDK toolchain. For example:

```shell
nrfutil sdk-manager toolchain launch --ncs-version v3.4.0 -- <your command>
```

This form is useful for running, for instance, a single `west` command with a specific toolchain. You can create an alias or shell function for this command to avoid typing it in full every time.

To initialize the workspace folder (`smha-workspace`) where the firmware project and all nRF Connect SDK modules will be cloned, run the following commands:

```shell
# Initialize the smha-workspace workspace
west init -m https://github.com/nrfconnect/ncs-serial-modem-host-applications --mr main smha-workspace

cd smha-workspace/project

# Update nRF Connect SDK modules. This may take a while.
west update
```

The repository is now cloned into the `smha-workspace/project` folder, the west modules are downloaded, and you are ready to build an application.

### Build and flash

Build from the application folder, passing the board target from the [Build targets](#build-targets) table. For example, to build and flash `91m1_ppp` on the nRF54L15 DK:

```shell
cd applications/91m1_ppp
west build -b nrf54l15dk/nrf54l15/cpuapp/ns -p
west flash --recover
```

> [!NOTE]
> `--recover` and `--erase` wipe TF-M Protected Storage, so any installed device credentials are lost and must be provisioned again. After the first flash, use a plain `west flash` to keep the credentials in place.

The application is now built and flashed to the device. Open a serial terminal on the host console port to view the logs. The [Serial Terminal app](https://www.nordicsemi.com/Products/Development-tools/nRF-Connect-for-Desktop) (part of nRF Connect for Desktop) is recommended; PuTTY, Tera Term, and minicom also work. The console port depends on the board — see the application guides below.

## Application guides

Follow the application documentation for hardware setup, build details, flashing, and (where applicable) cloud provisioning:

| Application | Cloud connectivity |
|-------------|--------------------|
| [nRF91M1 Host Application](applications/91m1_ppp/README.md) | Host terminates CoAP/DTLS to nRF Cloud itself. PPP just carries IP to the nRF91M1 modem |
| [nRF93M1 Host Application](applications/93m1_ppp/README.md) | Host terminates CoAP/DTLS to nRF Cloud itself. PPP just carries IP to the nRF93M1 modem |
| [nRF93M1 Serial Modem Host (AT)](applications/93m1_at/README.md) | Modem terminates the connection itself with its built-in AT client. Host just sends AT commands |

If you experience issues, check the logs in the serial terminal for any error messages. Each application guide has a troubleshooting section, for example [91m1_ppp troubleshooting](applications/91m1_ppp/README.md#troubleshooting). You can also open a support ticket on [DevZone](https://devzone.nordicsemi.com) for further assistance.

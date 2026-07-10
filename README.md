# Somfy Awning Matter Remote

An ESP32 firmware that makes a Somfy RTS awning controllable by Google Home, with no hub, no cloud account, and no self-hosted server. The ESP32 presents itself to the home network as a standard Matter Window Covering and translates open, close, and stop into Somfy RTS radio frames on 433.42 MHz through a CC1101 transceiver.

It emulates an additional Somfy remote. The existing physical Telis remote keeps working; this device registers alongside it, so voice and app control are added without giving anything up.

- [How It Works](#how-it-works)
- [What You Get](#what-you-get)
- [Hardware](#hardware)
- [Build And Flash](#build-and-flash)
- [First-Time Setup](#first-time-setup)
- [Daily Use](#daily-use)
- [Automations](#automations)
- [Direction, Position, And Limitations](#direction-position-and-limitations)
- [Repository Layout](#repository-layout)
- [License](#license)

## How It Works

The ESP32 does three jobs at once: it joins Wi-Fi, it serves the Matter protocol so a controller can adopt it as a Window Covering, and it drives a CC1101 radio to transmit Somfy RTS frames. A Matter command such as "open" is translated into the matching Somfy button press and radiated to the awning motor.

```text
Google Home app / Assistant ─┐
Alexa / Apple Home / SmartThings (optional, Matter multi-admin) ─┤
                             │  Matter over local Wi-Fi (IPv6/mDNS)
                             ▼
                 ┌───────────────────────────────┐
                 │ ESP32 (Elegoo DevKit V1)       │
                 │  - Wi-Fi + Matter Window Cover │
                 │  - Command translation layer   │
                 │  - Somfy RTS frame generator   │
                 └───────────────┬───────────────┘
                                 │ SPI + one data line (GDO0)
                                 ▼
                 ┌───────────────────────────────┐
                 │ CC1101 transceiver @ 433.42MHz │
                 └───────────────┬───────────────┘
                                 │ 433.42 MHz OOK, Somfy RTS
                                 ▼
                        Awning tubular motor
```

Matter is a local-network standard, so the controller commissions the ESP32 directly and then talks to it peer-to-peer. Matter multi-admin means the same device can be joined to several ecosystems at once. The radio path is one-way: Somfy RTS is fire-and-forget, with no position feedback from the motor. See [the architecture reference](docs/architecture.md) for the design.

## What You Get

- Voice and app control of the awning through Google Home: open, close, and stop.
- Automations through the controller: schedules, sunrise and sunset offsets, and more (see [Automations](#automations)).
- A built-in Wi-Fi setup portal. On first boot the device opens an `Awning-Setup-XXXX` access point (with a per-device suffix so two units do not collide); connect to it and a captive portal walks through entering home Wi-Fi credentials. No app, no hub required.
- A web diagnostics dashboard at `http://somfy-awning-XXXX.local` once the device is on the network. It shows the hostname, IP address, Wi-Fi SSID and signal strength, and Matter commissioning state, including the pairing code and a QR-code link before commissioning. It also has a `Change Wi-Fi` form for switching networks without a factory reset.
- A device that survives reboots. The Somfy rolling code and the last-known position persist in flash, so commands keep working and the tile stays sensible after a power cycle.
- Local controls for setup and recovery: a serial command interface for the bench, plus a panel-mount pairing button and status LED for headless, in-the-box operation.
- Optional multi-ecosystem control. The same device can be shared into Alexa, Apple Home, or SmartThings through Matter multi-admin.

## Hardware

The canonical build is an Elegoo ESP32 DevKit V1 (`ESP32-WROOM-32`, 4 MB flash) with a CC1101 433 MHz transceiver, a quarter-wave antenna, and a panel-mount button and LED. The CC1101 is required rather than a cheap fixed-frequency 433 MHz transmitter, because North American Somfy RTS uses 433.42 MHz and the CC1101 can be tuned there precisely in software.

Refer to [the hardware reference](docs/hardware.md) for the full bill of materials, the wiring table and diagram, and the pre-power validation checklist.

## Build And Flash

This project uses [PlatformIO](https://platformio.org/) with the community [pioarduino platform](https://github.com/pioarduino/platform-espressif32), which provides the Arduino-ESP32 3.x core and its built-in Matter library.

1. Install [VS Code](https://code.visualstudio.com/) and the [PlatformIO extension](https://marketplace.visualstudio.com/items?itemName=platformio.platformio-ide).
2. Clone this repository and open the folder in VS Code.
3. Let PlatformIO resolve the platform and libraries. The first Matter build is slow because the toolchain and framework are large.
4. Build the firmware with `platformio run`.
5. Connect the ESP32 by USB and flash it with `platformio run --target upload`.
6. Open the serial monitor at 115200 baud to watch boot logs and use the serial command interface.

For the full build, validation, and contribution workflow, see [`CONTRIBUTING.md`](CONTRIBUTING.md).

## First-Time Setup

Set the device up in three stages. Connect it to Wi-Fi first, prove the radio second, then add network control.

1. Connect the device to your home Wi-Fi. On first boot, the device opens an `Awning-Setup-XXXX` access point (the suffix is unique per device). Connect a phone or laptop to that network, complete the portal, and the device stores credentials and reboots onto the home network.
2. Pair the virtual remote with the awning motor, using the physical Telis remote to enter programming mode. Follow [the Somfy pairing procedure](docs/pairing.md). Radio pairing is independent of Wi-Fi and can be done over serial at any time.
3. Commission the device into Google Home, using the pairing code shown on the diagnostics dashboard or in the serial monitor. Follow [the Matter commissioning guide](docs/commissioning.md).

Keeping these stages separate means a problem in one is easy to localize without disturbing the others.

## Daily Use

Once commissioned, control the awning by voice ("open the awning," "close the awning," "stop the awning") or from the Window Covering tile in the controller app.

The panel-mount button provides local control and recovery without a laptop. A short press stops the awning, a medium press enters Somfy pairing mode, and a long press performs a full factory reset: it clears the stored Wi-Fi credentials and decommissions Matter, returning the device to the `Awning-Setup` portal on next boot. The status LED confirms each action. See [the button reference](docs/pairing.md#button-reference) for the exact timings and blink patterns.

## Automations

Once the device is a Matter window covering in the controller, several automation routes are available with no additional hosting.

- Schedules open or close the awning at set times, including sunrise and sunset offsets, which suit an awning well.
- Voice routines can trigger the awning from a custom phrase.
- Temperature conditions can drive the awning if a compatible temperature source is present, or through Alexa or SmartThings after sharing the device by multi-admin.
- IFTTT applets can trigger the awning through the Google Assistant and Google Home services for cross-service automations.

A practical caution for awnings: automatic extension in high wind can damage the awning. If you build weather automations, prefer conservative logic, such as retracting on wind and not auto-extending unattended in gusty conditions.

## Direction, Position, And Limitations

Matter treats 0 percent lift as fully open (awning retracted) and 100 percent as fully closed (extended). The default maps Matter Open to Somfy Up and Matter Close to Somfy Down, which is convention-correct. If it feels backward, flip the `INVERT_DIRECTION` build flag and reflash, or rename the device. See [the direction semantics notes](docs/hardware.md#direction-semantics).

Because Somfy RTS gives no position feedback, the device reports only the two end states. Asking for a specific percentage moves the awning to the nearer end stop rather than to a partial position.

A do-it-yourself Matter device uses test credentials, so the controller shows an "uncertified device" warning during setup. This is expected and acceptable for personal use.

## Repository Layout

- `src/rf`: the radio layer, wrapping the CC1101 and the Somfy frame generator.
- `src/matter`: the Matter Window Covering endpoint and command translation.
- `src/net`: the network layer — Wi-Fi station and SoftAP management (`WiFiConnection`) and the HTTP setup portal and diagnostics dashboard (`WebInterface`).
- `src/storage`: persistent configuration in NVS, including Wi-Fi credentials and the last-known lift position.
- `src/config.h`: the remote ID, pin assignments, direction flag, button thresholds, AP SSID, and device hostname.
- `docs/`: durable hardware, architecture, pairing, and commissioning references.

## License

This project is licensed under the MIT License. See the [`LICENSE`](LICENSE) file for details.

# Somfy Awning Matter Remote

An ESP32 firmware that makes a Somfy RTS awning controllable by Google Home, with no hub, no cloud account, and no self-hosted server. The ESP32 presents itself to the home network as a standard Matter Window Covering and translates open, close, and stop into Somfy RTS radio frames on 433.42 MHz through a CC1101 transceiver.

It emulates an additional Somfy remote. The existing physical Telis remote keeps working; this device registers alongside it, so voice and app control are added without giving anything up.

- [How It Works](#how-it-works)
- [What You Get](#what-you-get)
- [Hardware](#hardware)
- [Getting Started](#getting-started)
  - [Install The Firmware](#install-the-firmware)
  - [Connect To Wi-Fi](#connect-to-wi-fi)
  - [Pair With The Awning](#pair-with-the-awning)
  - [Commission Into Google Home](#commission-into-google-home)
- [Daily Use](#daily-use)
- [Button Reference](#button-reference)
- [LED Reference](#led-reference)
- [Troubleshooting](#troubleshooting)
- [Automations](#automations)
- [Direction, Position, And Limitations](#direction-position-and-limitations)
- [Building From Source](#building-from-source)
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
- A web diagnostics dashboard at `http://somfy-awning-XXXX.local` once the device is on the network. It shows a status table (radio detection, Wi-Fi, Matter commissioning, hostname, IP address, and the installed firmware version with a note when a newer release is available), the step-by-step awning pairing procedure, a Startup Log replay so the device can be checked without a serial monitor, and the Matter pairing code and QR-code link before commissioning. It also has a `Change Wi-Fi` form, with a password Show/Hide toggle, for switching networks without a factory reset, which safely reverts to the current network if the new one cannot be reached.
- A device that survives reboots. The Somfy rolling code and the last-known position persist in flash, so commands keep working and the tile stays sensible after a power cycle.
- Local controls for setup and recovery: a serial command interface for the bench, plus a panel-mount pairing button and status LED for headless, in-the-box operation.
- Optional multi-ecosystem control. The same device can be shared into Alexa, Apple Home, or SmartThings through Matter multi-admin.

## Hardware

The canonical build is an Elegoo ESP32 DevKit V1 (`ESP32-WROOM-32`, 4 MB flash) with a CC1101 433 MHz transceiver, a quarter-wave antenna, and a panel-mount button and LED. The CC1101 is required rather than a cheap fixed-frequency 433 MHz transmitter, because North American Somfy RTS uses 433.42 MHz and the CC1101 can be tuned there precisely in software.

Refer to [the hardware reference](docs/hardware.md) for the full bill of materials, the wiring table and diagram, and the pre-power validation checklist.

## Getting Started

This section is the full first-run path for a device that is already wired and built: flash the firmware, connect it to Wi-Fi, pair it with the awning, then commission it into Google Home. Each stage stands on its own, so a problem in one is easy to localize without disturbing the others.

### Install The Firmware

The easiest way to install the firmware is the browser-based flasher. It writes a prebuilt image to the ESP32 straight from a web page, with no build tools or command line.

1. Connect the ESP32 to a computer with a USB data cable.
2. Open the [web flasher](https://paraesthesia.com/somfy-matter-remote/) in desktop Chrome or Edge. It relies on Web Serial, which Firefox, Safari, and mobile browsers do not support.
3. Click `Connect and Install` under `First-Time Install` and choose the ESP32's serial port when prompted.
4. Wait for the install to finish, then continue with [Connect To Wi-Fi](#connect-to-wi-fi).

The flasher installs the image from the most recent [release](https://github.com/tillig/somfy-matter-remote/releases). To build and flash from source instead, see [Building From Source](#building-from-source).

The page offers a second path, `Update Firmware`, for a device that is already set up. It writes only the program and leaves stored settings alone, so the Wi-Fi credentials, the Google Home pairing, and the awning pairing all survive; use it for every update after the first install. The browser asks whether to erase the device first: leave `Erase device` unchecked, or those pairings are lost anyway. `First-Time Install` writes the bootloader as well, which is what a blank board needs, but it clears those saved settings.

### Connect To Wi-Fi

On first boot, with no Wi-Fi credentials stored, the device opens its own `Awning-Setup-XXXX` access point (`XXXX` is a per-device suffix, so two units never collide).

1. From a phone or laptop, join the `Awning-Setup-XXXX` Wi-Fi network.
2. A setup page should open automatically (a captive portal). If it does not, browse to `http://192.168.4.1`.
3. Enter your home Wi-Fi network name and password, then choose `Save and Restart`. The password is tested before it is saved, so a typo is reported on the page rather than silently stored; use the Show/Hide toggle next to the field to check what was typed.
4. The page tells you the address the device will move to once it joins your network, for example `http://somfy-awning-a4c1.local`. Note it, then wait for the device to reboot onto your Wi-Fi.

Credentials persist in flash, so this is a one-time step. To change networks later, use the `Change Wi-Fi` form on the web dashboard instead of a factory reset; see [What You Get](#what-you-get).

### Pair With The Awning

This step registers the device's virtual remote as an additional remote on the awning motor, using the physical Telis remote to put the motor into programming mode. The physical remote keeps working afterward. Pairing is independent of Wi-Fi and can be done at any time.

1. Locate the `Prog` button on the back of the physical Telis remote, often behind the battery cover or a small pinhole.
2. Press and hold `Prog` on the physical remote until the awning jogs briefly, a short back-and-forth movement. The motor is now in programming mode for a few seconds.
3. Within that window, register the device: hold the panel-mount button until the status LED gives two quick blinks, at about three seconds, then release. On a bench with a serial monitor open, type `pair` instead.
4. The awning should jog again, confirming the new remote is registered.
5. Test it. Tap the button to stop the awning, or type `retract`, `extend`, and `stop` in the serial monitor. The awning should roll up on `retract`, unroll on `extend`, and halt on `stop`.

Somfy RTS is transmit-only with no acknowledgment, so the device can never confirm the motor accepted the pairing; the awning jogging and then responding to commands is the only proof. For the full procedure, rolling-code details, and troubleshooting, see [the Somfy pairing guide](docs/pairing.md).

### Commission Into Google Home

The device must already be on Wi-Fi before commissioning; this ESP32 Matter build has no Bluetooth commissioning path.

1. Get the Matter pairing code, either from the web dashboard at `http://somfy-awning-XXXX.local` or from the serial monitor.
2. In the Google Home app, choose to add a device, then choose the Matter path (labeled "Matter-enabled device," or scan the QR code).
3. Scan the QR code from the dashboard, or enter the manual pairing code.
4. Accept the "uncertified device" warning; this is expected for a do-it-yourself Matter device and is not a failure.
5. Let Google Home commission the device and add it as a window covering. Assign it a room and a speakable name, for example "Patio Awning."

For reconnection behavior, multi-admin sharing, and troubleshooting, see [the Matter commissioning guide](docs/commissioning.md).

## Daily Use

Once commissioned, control the awning by voice ("stop the awning," "open the awning," "close the awning") or from the Window Covering tile in the controller app. The panel-mount button and status LED give the same control without a controller or app; see [Button Reference](#button-reference) and [LED Reference](#led-reference).

## Button Reference

The panel-mount button on the device chooses its action by how long it is held. The LED marks each threshold as you reach it, so you release the button on a cue instead of counting seconds. Watch the LED rather than a clock.

| Hold Until | LED Cue | Release Action |
| --- | --- | --- |
| Press registers | Blinks off and back on once | Stop the awning (Somfy calls this `My`) |
| Pairing threshold, about 3 seconds | Two quick blinks | Pair with the awning (Somfy calls this `Prog`) |
| Reset threshold, about 10 seconds | Four slow, heavy blinks | Factory reset, which fires while still held: clears Wi-Fi credentials and Matter commissioning, reopening the `Awning-Setup` portal on next boot |

So a quick tap stops the awning; hold until the double blink and let go to pair; keep holding past it to reset. The reset fires on its own at the threshold rather than waiting for release, so its confirmation pattern cannot be mistaken for another cue.

The onboard `BOOT` button is deliberately not used at runtime, because it ends up sealed inside the enclosure.

## LED Reference

While idle, the LED reports device state as a repeating count of short pulses, with a 1.5-second gap between repetitions; the pulse count is the code. Codes are evaluated most-blocking-first, so the LED always shows the next thing to fix. Solid on, with no counting, means the device is still booting.

| Pulses | Meaning |
| --- | --- |
| 1 | Ready: Wi-Fi is up, Matter is commissioned, and the radio is detected |
| 2 | Waiting for Wi-Fi setup: the device is hosting the `Awning-Setup-XXXX` portal |
| 3 | Cannot reach Wi-Fi: credentials are stored but the device is not connecting |
| 4 | Not yet added to Google Home: on Wi-Fi, but no Matter fabric yet |
| 5 | Radio not detected: the CC1101 did not respond over SPI |

While the button is held, the LED shows the hold cues instead of the idle count (see [Button Reference](#button-reference)), then returns to the code above.

## Troubleshooting

- **LED shows 5 pulses (radio not detected).** The CC1101 did not respond over SPI. Re-check the wiring, especially `3V3` power and the SPI pins. See [the wiring validation checklist](docs/hardware.md#wiring-validation).
- **The awning does not respond after pairing.** Confirm the awning actually jogged during the pairing steps; if it never jogged, the physical remote's `Prog` window was missed, or the frequency or wiring is off. See [the pairing troubleshooting section](docs/pairing.md#troubleshooting).
- **Can't find the device on the network.** Check the serial monitor or your router for its IP address, and confirm the network is not blocking mDNS. See [the commissioning troubleshooting section](docs/commissioning.md#troubleshooting).
- **Google Home can't find the device.** Confirm the phone and the ESP32 are on the same Wi-Fi network and subnet, and that the network allows the IPv6 and mDNS traffic Matter relies on. Many mesh and guest networks block this.
- **Direction feels backward.** Flip the `INVERT_DIRECTION` build flag and reflash, or simply rename the device. See [Direction, Position, And Limitations](#direction-position-and-limitations).
- **Flashing fails with "Wrong boot mode detected."** Something is holding a strapping pin at reset. Disconnect the CC1101 `GDO0` data wire and try again. This is why the data line uses `D4` rather than `D2`, which is a strapping pin; a board wired to `D2` cannot be flashed with the radio powered. See [the CC1101 radio wiring table](docs/hardware.md#cc1101-radio-wiring).

## Automations

Once the device is a Matter window covering in the controller, several automation routes are available with no additional hosting.

- Schedules open or close the awning at set times, including sunrise and sunset offsets, which suit an awning well.
- Voice routines can trigger the awning from a custom phrase.
- Temperature conditions can drive the awning if a compatible temperature source is present, or through Alexa or SmartThings after sharing the device by multi-admin.
- IFTTT applets can trigger the awning through the Google Assistant and Google Home services for cross-service automations.

A practical caution for awnings: automatic extension in high wind can damage the awning. If you build weather automations, prefer conservative logic, such as retracting on wind and not auto-extending unattended in gusty conditions.

## Direction, Position, And Limitations

This build uses the awning sense of the words: "open the awning" unrolls it for shade, and "close the awning" rolls it up and puts it away. That is `INVERT_DIRECTION=1`, the default here. Set the flag to 0 for the literal Matter reading, where open retracts. Either way the reported state matches the command issued, so the tile never contradicts itself. See [the direction semantics notes](docs/hardware.md#direction-semantics).

Because Somfy RTS gives no position feedback, the device reports only the two end states. Asking for a specific percentage moves the awning to the nearer end stop rather than to a partial position.

A do-it-yourself Matter device uses test credentials, so the controller shows an "uncertified device" warning during setup. This is expected and acceptable for personal use.

## Building From Source

Building from source is only needed to modify the firmware or to change compile-time options such as `INVERT_DIRECTION`. For a normal install, use the [web flasher](#install-the-firmware) instead.

This project uses [PlatformIO](https://platformio.org/) with the community [pioarduino platform](https://github.com/pioarduino/platform-espressif32), which provides the Arduino-ESP32 3.x core and its built-in Matter library.

1. Install [VS Code](https://code.visualstudio.com/) and the [PlatformIO extension](https://marketplace.visualstudio.com/items?itemName=platformio.platformio-ide).
2. Clone this repository and open the folder in VS Code.
3. Let PlatformIO resolve the platform and libraries. The first Matter build is slow because the toolchain and framework are large.
4. Build the firmware with `platformio run`.
5. Connect the ESP32 by USB and flash it with `platformio run --target upload`.
6. Open the serial monitor at 115200 baud to watch boot logs and use the serial command interface.

For the full build, validation, and contribution workflow, see [`CONTRIBUTING.md`](CONTRIBUTING.md).

## License

This project is licensed under the MIT License. See the [`LICENSE`](LICENSE) file for details.

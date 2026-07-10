# Architecture Reference

- [Layers](#layers)
- [Source Layout](#source-layout)
- [Wi-Fi Provisioning And Boot Modes](#wi-fi-provisioning-and-boot-modes)
- [Runtime Flow](#runtime-flow)
- [Direction And Position Semantics](#direction-and-position-semantics)
- [Persisted State](#persisted-state)
- [Why There Is No Hub Or Server](#why-there-is-no-hub-or-server)
- [Documentation Roles](#documentation-roles)

## Layers

The firmware uses a small layered architecture with deliberately clean boundaries. The device does several jobs at once: it joins Wi-Fi (or hosts a setup access point), serves the Matter protocol so a controller can adopt it as a Window Covering, serves a small web interface, and drives a CC1101 radio to transmit Somfy RTS frames.

- **Matter layer** (`src/matter/`): owns the Matter Window Covering endpoint and maps its commands onto radio intents. It translates commands and never touches CC1101 registers directly.
- **Radio layer** (`src/rf/`): owns the CC1101 transceiver and the Somfy frame generator, and exposes an intent-based API (`retract`, `extend`, `stop`, `pair`). It knows nothing about Matter.
- **Network layer** (`src/net/`): `WiFiConnection` owns Wi-Fi bring-up and reconnection, choosing between station mode and the setup access point. `WebInterface` serves the Wi-Fi setup portal and the diagnostics dashboard.
- **Storage layer** (`src/storage/`): persists firmware-owned state (last-known position, Wi-Fi credentials) in NVS through the `Preferences` API.
- **Application glue** (`src/main.cpp`): wires the layers together, chooses the boot path, and owns the local affordances (serial command interface, pairing button state machine, status LED).

The boundary rule is strict: the Matter layer depends on the radio layer through a narrow interface, the radio layer never depends upward, and persistence is reached only through `ConfigStore`. `WiFiConnection` manages only the radio and reports state; it does not own the web server or Matter. This mirrors the layered decoupling used in the `MarantzVolumeMonitor` project.

The network layer exists because this ESP32 Matter build has no over-BLE commissioning (see [Wi-Fi Provisioning And Boot Modes](#wi-fi-provisioning-and-boot-modes)). The class is named `WiFiConnection` rather than `NetworkManager` because the Arduino-ESP32 core already defines a `NetworkManager` class, and the two names would collide.

## Source Layout

```text
src/
├── main.cpp                # setup()/loop(); boot path, serial, button, LED
├── config.h                # REMOTE_ID, pins, INVERT_DIRECTION, network, thresholds
├── rf/
│   ├── SomfyController.h
│   └── SomfyController.cpp # wraps CC1101 + SomfyRemote: retract/extend/stop/pair
├── matter/
│   ├── AwningCovering.h
│   └── AwningCovering.cpp  # Matter Window Covering endpoint + command callbacks
├── net/
│   ├── WiFiConnection.h
│   ├── WiFiConnection.cpp  # station connect / setup AP / reconnection
│   ├── WebInterface.h
│   └── WebInterface.cpp    # Wi-Fi setup portal + diagnostics dashboard
└── storage/
    ├── ConfigStore.h
    └── ConfigStore.cpp     # NVS-backed position and Wi-Fi credentials
```

New behavior should keep the ownership boundaries intact. Radio-facing work belongs in `SomfyController`, controller-facing work in `AwningCovering`, network bring-up in `WiFiConnection`, HTTP surfaces in `WebInterface`, and any new persisted field in `ConfigStore`.

## Wi-Fi Provisioning And Boot Modes

The classic ESP32 build of the Arduino Matter library does not enable Bluetooth (CHIPoBLE) commissioning, so the device cannot receive Wi-Fi credentials from the commissioning phone the way some Matter devices do. It must already be on Wi-Fi before Matter commissioning runs. The firmware therefore provisions Wi-Fi itself and branches at boot:

- **Setup mode** (no stored credentials): `WiFiConnection` hosts an open access point named `Awning-Setup-XXXX`, and `WebInterface` serves a setup form with a captive-portal DNS redirect. Matter and the radio are left down. Saving the form validates the network live before persisting anything (see below), then reboots.
- **Station mode** (credentials present): `WiFiConnection` joins the home network, then the radio and Matter come up and the web interface serves a diagnostics dashboard. If the network is briefly unreachable the device stays in station mode and keeps retrying rather than dropping back into setup.

The setup access point SSID and the mDNS hostname both carry a per-device suffix derived from the chip MAC (the `XXXX` above), so two units never collide on the setup network name or on `<hostname>.local`. In station mode `WiFiConnection` starts an mDNS responder for the dashboard; this is separate from the mDNS the Matter stack runs for commissioning discovery.

The firmware never persists a set of credentials it has not proven, and never strands itself on a bad one. Two distinct paths handle the two situations, because a mistyped password must never require a physical factory reset:

- **Setup portal (test before save)**: the phone is on the device's own access point, which stays up during the test (`WIFI_AP_STA` mode). `WiFiConnection::testCredentials()` attempts the join while the portal remains reachable, so the page reports success or failure. Credentials are written to NVS only on success; on failure the form is redisplayed for another try.
- **Dashboard change (pending, try on reboot, auto-revert)**: here the browser reaches the device through the current network, so a live test cannot be reported (testing means leaving that network). `ConfigStore` stores the new network as a separate *pending* slot alongside the active credentials. On the next boot `WiFiConnection::begin()` tries pending first; on success it promotes pending to active, and on failure it discards pending and connects with the still-intact active credentials. A dashboard typo therefore leaves the device on its current network.

A factory reset (long button press) is the heavier action: it clears the Matter fabric and both the active and pending Wi-Fi credentials, returning the device to setup mode on the next boot.

## Runtime Flow

1. `setup()` initializes the status LED and pairing button pins, restores state through `ConfigStore`, then calls `WiFiConnection::begin()`.
2. If the result is setup mode, `WebInterface::begin()` starts the portal and `setup()` returns early. `loop()` then services only the web interface and LED until the saved credentials trigger a reboot.
3. In station mode, `SomfyController::begin()` initializes the CC1101 over SPI, points the Somfy emitter at GPIO2 (`GDO0`), sets ASK/OOK modulation, and tunes to 433.42 MHz. It confirms the radio responds over SPI and reports readiness; a failure almost always means a wiring fault.
4. `AwningCovering::begin()` starts the Matter Window Covering endpoint as an `AWNING` type, restores the last-known lift position, registers the open/close/stop and go-to-percentage callbacks, and prints the manual pairing code and QR-code URL when the device is not yet commissioned. `Matter.begin()` runs last, after Wi-Fi is up.
5. `loop()` services Wi-Fi reconnection, Matter housekeeping, the web interface, the serial command interface, the pairing button, and the LED on every pass. Nothing blocks, so Matter stays responsive.
6. A Matter command invokes the matching `SomfyController` intent, then reports the resulting end-stop position back to controllers and persists it.

## Direction And Position Semantics

Matter Window Covering treats 0 percent lift as fully open (awning retracted, letting light in) and 100 percent as fully closed (awning extended, blocking light). Somfy uses Up to retract and Down to extend.

The convention-correct default (`INVERT_DIRECTION=0`) maps Matter Open to Somfy Up and Matter Close to Somfy Down. The `INVERT_DIRECTION` compile-time flag flips only the physical motor direction; the reported Matter state stays convention-correct so a controller tile still reads right. See [Direction Semantics in the hardware reference](hardware.md) for the daily-use rationale.

Because a Somfy RTS motor gives no position feedback, the endpoint reports only the two end states. An open, close, or go-to-percentage command drives the motor and then snaps the reported lift to the nearest end stop (a go-to value below 50 percent opens, otherwise it closes). This keeps slider drags and mid-value routines working sensibly without pretending to have real intermediate positioning. Timed partial-position estimation is a possible future enhancement, not a current feature.

## Persisted State

Two independent NVS namespaces keep persistence concerns separate.

- The Somfy rolling code lives in its own namespace, owned entirely by the `Somfy_Remote_Lib` `NVSRollingCodeStorage`. It must increment on every transmission and survive reboots, or the motor will reject later commands as stale. This is the single most important piece of persisted state.
- `ConfigStore` owns the estimated lift position and the Wi-Fi credentials in a separate `awningcfg` namespace. It keeps two credential slots: the active network the device connects to, and an optional pending network submitted from the dashboard and tried on the next boot. The position lets the Matter tile show a sensible last-known state after a power cycle; the active credentials let the device rejoin Wi-Fi without re-running setup. Position writes are skipped when the value is unchanged to avoid flash wear. Credentials are only ever entered through the setup portal or dashboard, never hardcoded in source.

## Why There Is No Hub Or Server

Matter is a local-network standard. The commissioning controller adopts the ESP32 directly and then talks to it peer-to-peer over the local network. Matter multi-admin means the same physical device can be joined to several ecosystems at once without any of them being a required intermediary. The ESP32 is the entire integration; there is no bridge, driver, or always-on server to host.

The radio path is one-way. Somfy RTS is fire-and-forget: the remote broadcasts a signed, rolling-code frame and the motor acts on it, with no acknowledgment. That is why there is no position feedback and why rolling-code persistence is non-negotiable.

## Documentation Roles

- `README.md` explains what the project is and how to build, flash, and use it.
- `CONTRIBUTING.md` explains how to build, validate, extend, and contribute.
- `docs/hardware.md` covers the bill of materials, wiring, and pre-power validation.
- `docs/pairing.md` covers the Somfy add-a-remote procedure.
- `docs/commissioning.md` covers Matter commissioning and multi-admin sharing.

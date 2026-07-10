# Architecture Reference

- [Layers](#layers)
- [Source Layout](#source-layout)
- [Runtime Flow](#runtime-flow)
- [Direction And Position Semantics](#direction-and-position-semantics)
- [Persisted State](#persisted-state)
- [Why There Is No Hub Or Server](#why-there-is-no-hub-or-server)
- [Documentation Roles](#documentation-roles)

## Layers

The firmware uses a small layered architecture with deliberately clean boundaries. The device does three jobs at once: it joins Wi-Fi, serves the Matter protocol so a controller can adopt it as a Window Covering, and drives a CC1101 radio to transmit Somfy RTS frames.

- **Matter layer** (`src/matter/`): owns the Matter Window Covering endpoint and maps its commands onto radio intents. It translates commands and never touches CC1101 registers directly.
- **Radio layer** (`src/rf/`): owns the CC1101 transceiver and the Somfy frame generator, and exposes an intent-based API (`retract`, `extend`, `stop`, `pair`). It knows nothing about Matter.
- **Storage layer** (`src/storage/`): persists firmware-owned state in NVS through the `Preferences` API.
- **Application glue** (`src/main.cpp`): wires the layers together and owns the local affordances (serial command interface, pairing button state machine, status LED).

The boundary rule is strict: the Matter layer depends on the radio layer through a narrow interface, the radio layer never depends upward, and persistence is reached only through `ConfigStore`. This mirrors the layered decoupling used in the `MarantzVolumeMonitor` project.

## Source Layout

```text
src/
├── main.cpp                # setup()/loop(); serial, button, and LED affordances
├── config.h                # REMOTE_ID, pins, INVERT_DIRECTION, thresholds
├── rf/
│   ├── SomfyController.h
│   └── SomfyController.cpp # wraps CC1101 + SomfyRemote: retract/extend/stop/pair
├── matter/
│   ├── AwningCovering.h
│   └── AwningCovering.cpp  # Matter Window Covering endpoint + command callbacks
└── storage/
    ├── ConfigStore.h
    └── ConfigStore.cpp     # NVS-backed last-known lift position
```

New behavior should keep the ownership boundaries intact. Radio-facing work belongs in `SomfyController`, controller-facing work in `AwningCovering`, and any new persisted field in `ConfigStore`.

## Runtime Flow

1. `setup()` initializes the status LED and pairing button pins, then brings up the layers in dependency order: `ConfigStore` (restore last-known position), `SomfyController` (CC1101 and Somfy emitter), then `AwningCovering` (Matter).
2. `SomfyController::begin()` initializes the CC1101 over SPI, points the Somfy emitter at GPIO2 (`GDO0`), sets ASK/OOK modulation, and tunes to 433.42 MHz. It confirms the radio responds over SPI and reports readiness; a failure almost always means a wiring fault.
3. `AwningCovering::begin()` starts the Matter Window Covering endpoint as an `AWNING` type, restores the last-known lift position, registers the open/close/stop and go-to-percentage callbacks, and prints the manual pairing code and QR-code URL when the device is not yet commissioned.
4. `loop()` services Matter housekeeping, the serial command interface, the pairing button, and the LED on every pass. Nothing blocks, so Matter stays responsive.
5. A Matter command invokes the matching `SomfyController` intent, then reports the resulting end-stop position back to controllers and persists it.

## Direction And Position Semantics

Matter Window Covering treats 0 percent lift as fully open (awning retracted, letting light in) and 100 percent as fully closed (awning extended, blocking light). Somfy uses Up to retract and Down to extend.

The convention-correct default (`INVERT_DIRECTION=0`) maps Matter Open to Somfy Up and Matter Close to Somfy Down. The `INVERT_DIRECTION` compile-time flag flips only the physical motor direction; the reported Matter state stays convention-correct so a controller tile still reads right. See [Direction Semantics in the hardware reference](hardware.md) for the daily-use rationale.

Because a Somfy RTS motor gives no position feedback, the endpoint reports only the two end states. An open, close, or go-to-percentage command drives the motor and then snaps the reported lift to the nearest end stop (a go-to value below 50 percent opens, otherwise it closes). This keeps slider drags and mid-value routines working sensibly without pretending to have real intermediate positioning. Timed partial-position estimation is a possible future enhancement, not a current feature.

## Persisted State

Two independent NVS namespaces keep persistence concerns separate.

- The Somfy rolling code lives in its own namespace, owned entirely by the `Somfy_Remote_Lib` `NVSRollingCodeStorage`. It must increment on every transmission and survive reboots, or the motor will reject later commands as stale. This is the single most important piece of persisted state.
- `ConfigStore` owns the estimated lift position in a separate `awningcfg` namespace, so the Matter tile shows a sensible last-known state after a power cycle. Writes are skipped when the value is unchanged to avoid flash wear.

## Why There Is No Hub Or Server

Matter is a local-network standard. The commissioning controller adopts the ESP32 directly and then talks to it peer-to-peer over the local network. Matter multi-admin means the same physical device can be joined to several ecosystems at once without any of them being a required intermediary. The ESP32 is the entire integration; there is no bridge, driver, or always-on server to host.

The radio path is one-way. Somfy RTS is fire-and-forget: the remote broadcasts a signed, rolling-code frame and the motor acts on it, with no acknowledgment. That is why there is no position feedback and why rolling-code persistence is non-negotiable.

## Documentation Roles

- `README.md` explains what the project is and how to build, flash, and use it.
- `CONTRIBUTING.md` explains how to build, validate, extend, and contribute.
- `docs/hardware.md` covers the bill of materials, wiring, and pre-power validation.
- `docs/pairing.md` covers the Somfy add-a-remote procedure.
- `docs/commissioning.md` covers Matter commissioning and multi-admin sharing.

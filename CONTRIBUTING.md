# Contributing

This guide explains how to build, validate, and extend the firmware. It is written for both human developers and AI assistants. Read [`CLAUDE.md`](CLAUDE.md) and [the architecture reference](docs/architecture.md) alongside it.

- [Development Environment](#development-environment)
- [Architecture And Principles](#architecture-and-principles)
- [Build And Validation](#build-and-validation)
- [Flashing And Bench Testing](#flashing-and-bench-testing)
- [Extending The Firmware](#extending-the-firmware)
- [Hardware-Gated Work](#hardware-gated-work)
- [Continuous Integration](#continuous-integration)
- [Toolchain Gotchas](#toolchain-gotchas)
- [Documentation Responsibilities](#documentation-responsibilities)

## Development Environment

- **Framework:** Arduino (ESP32).
- **Platform:** the community [pioarduino platform](https://github.com/pioarduino/platform-espressif32), pinned in `platformio.ini`, which provides the Arduino-ESP32 3.x core (ESP-IDF 5.x) with the built-in `Matter` library.
- **Board:** Elegoo ESP32 DevKit V1 (`ESP32-WROOM-32`, 4 MB flash).
- **Tooling:** PlatformIO, `pre-commit`, `clang-format`, `markdownlint`, and `cppcheck`.
- **Libraries:** [`Somfy_Remote_Lib`](https://github.com/Legion2/Somfy_Remote_Lib) for RTS frame generation and NVS rolling-code storage, and [`SmartRC-CC1101-Driver-Lib`](https://github.com/LSatan/SmartRC-CC1101-Driver-Lib) for the CC1101. The `Matter` library ships with the core, so it is not in `lib_deps`.

Install VS Code and the PlatformIO extension, clone the repository, and open the folder. PlatformIO resolves the platform and libraries on first build. The first Matter build is slow because the toolchain and framework are large.

## Architecture And Principles

The firmware is layered with strict boundaries, described in full in [the architecture reference](docs/architecture.md).

1. **Layered decoupling.** The radio layer (`src/rf/`) never knows about Matter, the Matter layer (`src/matter/`) never touches CC1101 registers, and persistence lives in `src/storage/`. Keep new work inside these boundaries.
2. **Non-blocking loop.** `loop()` services Matter, serial, the button, and the LED on every pass. Do not add `delay()` or other blocking calls to the runtime path; Matter must keep running. The status LED uses a non-blocking blink state machine for this reason.
3. **Persistence is sacred.** The Somfy rolling code must increment and persist on every transmission, or the motor rejects later commands. It is owned by the Somfy library's NVS storage in its own namespace. Firmware-owned state (the last-known position) lives in `ConfigStore` in a separate namespace.
4. **Documentation first.** Any change to functionality, hardware, or tooling updates the appropriate doc before the change is considered done.

## Build And Validation

Run these from the repository root before finishing any change:

```bash
pre-commit run --all-files
platformio check --fail-on-defect=low
platformio run
```

The `pre-commit` hooks cover JSON, YAML, and TOML validation, trailing whitespace, merge-conflict markers, `clang-format` for C and C++ sources, `markdownlint` (with autofix), and JSON sorting. C++ formatting must match `.clang-format`; Markdown must pass `.markdownlint.json`. Static analysis must report no defects at low severity or above, scoped to `src/`.

## Flashing And Bench Testing

Flash a connected ESP32 with:

```bash
platformio run --target upload
```

Then open the serial monitor at 115200 baud. The serial command interface mirrors the Somfy library examples: type `Up`, `Down`, `My`, or `Prog` and press enter to transmit that command. This is the fastest way to bring up and debug the radio before Matter is involved. On first boot, when the device is not yet commissioned, the firmware also prints the Matter manual pairing code and QR-code URL.

## Extending The Firmware

- **New radio behavior** (for example, exposing the Somfy `SunFlag` or `Flag` sensor commands) goes in `SomfyController` as a new intent method, then is surfaced upward. The `Command` enum from the Somfy library already defines the available buttons.
- **New Matter behavior** goes in `AwningCovering`. The endpoint is a `MatterWindowCovering`; its callbacks (`onOpen`, `onClose`, `onStop`, `onGoToLiftPercentage`) return `bool` and must call into `SomfyController` rather than the radio directly. Report state back with `setLiftPercentage()` followed by `updateAccessory()`.
- **New persisted state** goes in `ConfigStore` as a typed getter and setter backed by `Preferences`. Keep the NVS namespace and key names short to stay within the ESP32 key-length limit, and skip writes when the value is unchanged.
- **New pins, thresholds, or the direction default** go in `src/config.h`. Never change `REMOTE_ID` once a physical device is paired.
- **Timed position estimation** is a natural future enhancement: calibrate full-travel time, store it in `ConfigStore`, and drive the motor for a proportional interval. Keep the current bang-bang behavior as the fallback.

## Hardware-Gated Work

Radio pairing, Matter commissioning, direction confirmation, and range checks require the physical CC1101 and awning. Code these paths against the library APIs and describe how to validate them, but do not claim them verified without hardware. The on-hardware procedures are in [the pairing guide](docs/pairing.md) and [the commissioning guide](docs/commissioning.md).

## Continuous Integration

GitHub Actions runs the same validation on pushes to `main`, `develop`, and `feature/**` branches, and on pull requests: `pre-commit`, then `platformio check`, then `platformio run`. The workflow caches the PlatformIO toolchain and framework, keyed on `platformio.ini` and `partitions.csv`, and allows a longer job timeout because the Matter build is large.

If a `feature/**` branch already has an open pull request, the branch-push build is skipped and the pull-request build remains the authoritative result. No secrets are needed; nothing is uploaded to a cloud account, so the build itself is the validation. Use the `feature/<short-name>` branch naming convention.

## Toolchain Gotchas

Two environment issues can block a first local build. Both are environmental, not code problems.

- **Static analysis and package headers.** `platformio.ini` sets `check_skip_packages = yes`. Without it, `cppcheck` tries to parse the toolchain's libstdc++ headers, cannot evaluate their `__has_builtin()` use, and aborts the entire check. Keep this setting.
- **TLS interception.** On a network with a TLS-inspecting proxy (for example, Zscaler), PlatformIO's framework download can fail with a certificate-verification error, because the proxy re-signs the connection with a corporate root that is not in the bundled `certifi` store. The fix is to make the Python environment trust the system roots, for example by appending the macOS system and root keychains to the `certifi` `cacert.pem` that PlatformIO's Python uses. This is a local environment fix and is not committed.

## Documentation Responsibilities

- Update `README.md` for user-facing setup, usage, hardware summary, and project consumption changes.
- Update this file for build, validation, extension, or workflow changes.
- Update `docs/hardware.md` for wiring, bill-of-materials, or physical-assembly changes.
- Update `docs/architecture.md` for durable layer, ownership, or source-layout changes.
- Update `docs/pairing.md` for the Somfy add-a-remote procedure and button behavior.
- Update `docs/commissioning.md` for Matter commissioning, reconnection, and multi-admin changes.

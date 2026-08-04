# Contributing

This guide explains how to build, validate, and extend the firmware. It is written for both human developers and AI assistants. Read [`CLAUDE.md`](CLAUDE.md) and [the architecture reference](docs/architecture.md) alongside it.

- [Development Environment](#development-environment)
- [Architecture And Principles](#architecture-and-principles)
- [Build And Validation](#build-and-validation)
- [Flashing And Bench Testing](#flashing-and-bench-testing)
- [Extending The Firmware](#extending-the-firmware)
- [Hardware-Gated Work](#hardware-gated-work)
- [Continuous Integration](#continuous-integration)
- [Releases And The Web Flasher](#releases-and-the-web-flasher)
- [Firmware Version](#firmware-version)
- [Toolchain Gotchas](#toolchain-gotchas)
- [Documentation Responsibilities](#documentation-responsibilities)

## Development Environment

- **Framework:** Arduino (ESP32).
- **Platform:** the community [pioarduino platform](https://github.com/pioarduino/platform-espressif32), pinned in `platformio.ini`, which provides the Arduino-ESP32 3.x core (ESP-IDF 5.x) with the built-in `Matter` library.
- **Board:** Elegoo ESP32 DevKit V1 (`ESP32-WROOM-32`, 4 MB flash).
- **Tooling:** PlatformIO, `pre-commit`, `clang-format`, `markdownlint`, and `cppcheck`.
- **Libraries:** [`Somfy_Remote_Lib`](https://github.com/Legion2/Somfy_Remote_Lib) for RTS frame generation and NVS rolling-code storage, and [`SmartRC-CC1101-Driver-Lib`](https://github.com/LSatan/SmartRC-CC1101-Driver-Lib) for the CC1101. The `Matter`, `WiFi`, `WebServer`, and `DNSServer` libraries all ship with the Arduino-ESP32 core and are not in `lib_deps`. `WiFi` and `WebServer` handle the station connection and HTTP surfaces; `DNSServer` powers the captive-portal DNS redirect during SoftAP provisioning.

Install VS Code and the PlatformIO extension, clone the repository, and open the folder. PlatformIO resolves the platform and libraries on first build. The first Matter build is slow because the toolchain and framework are large.

## Architecture And Principles

The firmware is layered with strict boundaries, described in full in [the architecture reference](docs/architecture.md).

1. **Layered decoupling.** The radio layer (`src/rf/`) never knows about Matter, the Matter layer (`src/matter/`) never touches CC1101 registers, the network layer (`src/net/`) handles Wi-Fi and HTTP surfaces, and persistence lives in `src/storage/`. Keep new work inside these boundaries.
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

Then open the serial monitor at 115200 baud. The serial command interface mirrors the Somfy library examples: type `Up`, `Down`, `My`, or `Prog` and press enter to transmit that command. This is the fastest way to bring up and debug the radio before Matter is involved. Type `help` for the command list, or `status` for a diagnostics dump covering radio state (including the CC1101 detection result and the current rolling code), Wi-Fi mode and address, and Matter commissioning state, which is useful during hardware bring-up. Unknown input is rejected rather than transmitted, so a typo cannot accidentally key the radio. On first boot, when the device is not yet commissioned, the firmware also prints the Matter manual pairing code and QR-code URL.

## Extending The Firmware

- **New radio behavior** (for example, exposing the Somfy `SunFlag` or `Flag` sensor commands) goes in `SomfyController` as a new intent method, then is surfaced upward. The `Command` enum from the Somfy library already defines the available buttons.
- **New Matter behavior** goes in `AwningCovering`. The endpoint is a `MatterWindowCovering`; its callbacks (`onOpen`, `onClose`, `onStop`, `onGoToLiftPercentage`) return `bool` and must call into `SomfyController` rather than the radio directly. Report state back with `setLiftPercentage()` followed by `updateAccessory()`.
- **New persisted state** goes in `ConfigStore` as a typed getter and setter backed by `Preferences`. Keep the NVS namespace and key names short to stay within the ESP32 key-length limit, and skip writes when the value is unchanged.
- **New Wi-Fi or connectivity behavior** goes in `WiFiConnection` in `src/net/`. New HTTP endpoints or pages go in `WebInterface` in `src/net/`. Do not name anything `NetworkManager` in this codebase: the ESP32 Arduino core already defines a class of that name, and the name collision is a compile error.
- **New pins, thresholds, or the direction default** go in `src/config.h`. Never change `REMOTE_ID` once a physical device is paired.
- **Timed position estimation** is a natural future enhancement: calibrate full-travel time, store it in `ConfigStore`, and drive the motor for a proportional interval. Keep the current bang-bang behavior as the fallback.

## Hardware-Gated Work

Radio pairing, Matter commissioning, direction confirmation, and range checks require the physical CC1101 and awning. Code these paths against the library APIs and describe how to validate them, but do not claim them verified without hardware. The on-hardware procedures are in [the pairing guide](docs/pairing.md) and [the commissioning guide](docs/commissioning.md).

## Continuous Integration

GitHub Actions runs the same validation on pushes to `main`, `develop`, and `feature/**` branches, and on pull requests: `pre-commit`, then `platformio check`, then `platformio run`. The workflow caches the PlatformIO toolchain and framework, keyed on `platformio.ini` and `partitions.csv`, and allows a longer job timeout because the Matter build is large.

If a `feature/**` branch already has an open pull request, the branch-push build is skipped and the pull-request build remains the authoritative result. No secrets are needed; nothing is uploaded to a cloud account, so the build itself is the validation. Use the `feature/<short-name>` branch naming convention.

## Releases And The Web Flasher

Consumers install the firmware from a browser with [ESP Web Tools](https://esphome.github.io/esp-web-tools/), so they never build from source. Two pieces support this.

- The `release.yml` workflow runs on a version tag (for example `v1.0.0`), builds the firmware, and attaches two images to a GitHub Release. `firmware.factory.bin` is the merged image: it flashes at offset `0x0` and contains the bootloader, partition table, and application, so it works on a blank board. `firmware.bin` is the application partition alone, flashed at `0x10000`.
- The `web-flasher/` directory holds the flasher page and two manifests, deployed to GitHub Pages by the `pages.yml` workflow. That workflow downloads the release binaries into the published directory, so the manifests reference them as same-origin paths (`./firmware.bin`). It runs on release publication as well as on flasher edits, which is what keeps the served firmware current.
- Do not point a manifest at `releases/latest/download/`. Those URLs redirect cross-origin to `objects.githubusercontent.com`, which the browser blocks as a CORS violation, and ESP Web Tools reports it only as `Failed to fetch`.
- `pages.yml` triggers on the version tag, not on `release: published`. A release created by the default `GITHUB_TOKEN`, which is how `release.yml` publishes, raises no events that start further workflow runs, so a release trigger never fires. It also carries no `paths` filter, because that would apply to tag pushes and filter out the very tags it needs to run for. The download step retries, because the tag starts both workflows at once and the assets do not exist until `release.yml` finishes building.
- Both manifests set `new_install_prompt_erase: true`. Without it, ESP Web Tools force-erases any device that does not support Improv Serial, which would defeat the update path; with it, the user gets a checkbox and can decline.

The two images exist because the merged one cannot preserve device state. It spans the NVS region with erased padding, so flashing it clears the Matter fabric and the Somfy rolling code, and a cleared rolling code means re-pairing with the motor. `firmware-manifest.json` uses it for a first-time install, where that does not matter. `firmware-manifest-update.json` writes only the application at `0x10000` and leaves NVS untouched, so an update keeps the Wi-Fi credentials, the Matter commissioning, and the awning pairing. Keep both paths working: the update image alone cannot bring up a blank board, since it carries no bootloader or partition table.

To cut a release, tag a commit on `main` and push the tag:

```bash
git tag v1.0.0
git push origin v1.0.0
```

The tag starts both `release.yml` (which builds and publishes) and `pages.yml` (which republishes the flasher with the new binaries); the flasher picks up the new firmware automatically. GitHub Pages must be enabled once in the repository settings with the source set to GitHub Actions. When testing the flasher, use desktop Chrome or Edge, since Web Serial is not available in Firefox, Safari, or mobile browsers.

## Firmware Version

`scripts/version.py` runs as a PlatformIO pre-build script and stamps `FIRMWARE_VERSION` from `git describe --tags --always --dirty`, so the release tag is the single source of truth and no constant in the source can drift from it. A tagged commit yields exactly the tag (`v1.0.0`); anything else is descriptive (`v1.0.0-3-gabc1234-dirty`), which keeps a hand-built image from being mistaken for a release.

Both workflows check out with `fetch-depth: 0`. The default shallow checkout has no tags, which would silently publish a release binary reporting `unknown`.

The dashboard's update check compares that version against the latest GitHub release **in the browser**, not on the device. The API sends `Access-Control-Allow-Origin: *` and needs no credentials. Keeping it client-side avoids a TLS stack and certificate bundle on the ESP32, keeps a blocking network call out of `loop()` where it would stall Matter, and spends the 60-per-hour rate limit per viewer rather than per device. It fails quietly by design: a failed fetch, a rate-limit response, or a non-release version leaves only the installed version shown.

## Toolchain Gotchas

`platformio.ini` sets `check_skip_packages = yes`. Without it, `cppcheck` tries to parse the toolchain's libstdc++ headers, cannot evaluate their `__has_builtin()` use, and aborts the entire check. Keep this setting.

## Documentation Responsibilities

- Update `README.md` for user-facing setup, usage, hardware summary, and project consumption changes.
- Update this file for build, validation, extension, or workflow changes.
- Update `docs/hardware.md` for wiring, bill-of-materials, or physical-assembly changes.
- Update `docs/architecture.md` for durable layer, ownership, or source-layout changes.
- Update `docs/pairing.md` for the Somfy add-a-remote procedure and button behavior.
- Update `docs/commissioning.md` for Matter commissioning, reconnection, and multi-admin changes.

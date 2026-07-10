# Agent Instructions

Guidance for AI assistants (and humans) working in this repository. Read this, then `CONTRIBUTING.md`, before making changes.

## What This Project Is

An ESP32 firmware that presents a Matter Window Covering to the home network and translates open/close/stop into Somfy RTS radio frames on 433.42 MHz via a CC1101 transceiver. It lets a Somfy-motorized awning be controlled by Google Home with no hub, cloud account, or server. See `README.md` for the user-facing overview and `docs/architecture.md` for the design.

## Core Mandates

- **Technology stack:** ESP32 (Elegoo DevKit V1), PlatformIO with the `pioarduino` platform (Arduino-ESP32 3.x core with built-in `Matter`), the `Somfy_Remote_Lib` and `SmartRC-CC1101-Driver-Lib` libraries.
- **Architecture:** Layered decoupling. The radio layer (`src/rf/`) never knows about Matter, the Matter layer (`src/matter/`) never touches radio registers, and persistence lives in `src/storage/`. See `docs/architecture.md`.
- **Documentation first:** Any change to functionality, hardware, or tooling must update the appropriate doc. `README.md` for user-facing consumption, `CONTRIBUTING.md` for build and workflow, `docs/` for durable hardware, architecture, pairing, and commissioning references.
- **Validation:** Always run `pre-commit run --all-files` before finishing. Firmware changes must build with `platformio run` and pass `platformio check --fail-on-defect=low`.
- **Safety:** Never change `REMOTE_ID` in `src/config.h` once a device is paired; the motor would treat the device as an unknown remote. Never hardcode secrets.

## Hardware-Gated Work

Radio pairing, Matter commissioning, direction confirmation, and range checks require the physical CC1101 and awning. Code these paths against the library APIs and mark validation steps clearly, but do not claim them verified without hardware. `docs/pairing.md` and `docs/commissioning.md` describe the on-hardware procedures.

## Markdown Authoring Rules

Apply these when writing or editing `.md` files. Baseline is CommonMark and GitHub Flavored Markdown; `markdownlint` enforces the mechanical rules.

- Use ATX headings (`#` plus a space) in Title Case. Do not skip heading levels. Start at `#` only for the document title; otherwise start at `##`.
- Do not use bold text as a substitute for a heading, and avoid a sub-heading level that holds only one heading.
- Use `-` for bullet lists and `1.` for ordered lists. Capitalize the first word of every list item. Convert single-item lists to prose.
- Use fenced code blocks with a language identifier. Use tables for tabular data.
- Keep each paragraph on a single line; do not soft-wrap prose.
- Use descriptive link text, never "here" or "this". Provide meaningful image alt text.
- Prefer short sentences and active voice. Format code identifiers, settings, and labels as code. Capitalize "ID" in prose. Spell out numbers below 10 unless they are a literal value. Do not use `&` for "and" outside established names. Use emoji sparingly and never as the only carrier of meaning.

# Somfy Pairing Procedure

- [What Pairing Does](#what-pairing-does)
- [Register The Virtual Remote](#register-the-virtual-remote)
- [Confirm Control](#confirm-control)
- [Button Reference](#button-reference)
- [Troubleshooting](#troubleshooting)

## What Pairing Does

This procedure registers the ESP32's virtual remote as an additional remote on the awning motor. It uses the existing physical Telis remote to put the motor into programming mode. The physical remote keeps working afterward.

The ESP32 does not clone the physical remote. It transmits its own 24-bit remote ID (set by `REMOTE_ID` in `src/config.h`) and is added to the motor as a new remote through the normal Somfy add-a-remote process. Choose the remote ID once and never change it; the motor tracks each remote by ID and rolling code, so a changed ID reads as an unknown remote.

This is hardware-gated work. It cannot be validated without the physical CC1101, antenna, and awning motor.

## Register The Virtual Remote

Locate the Prog button on the back of the physical Telis remote before starting; it is often behind the battery cover or a small pinhole.

1. Press and hold the Prog button on the physical remote until the awning motor jogs briefly, a short back-and-forth movement. The motor is now in programming mode for a few seconds.
2. Within that window, send the pair command from the ESP32. On a bench, type `pair` in the serial monitor (Somfy calls this `Prog`). On an assembled unit, hold the panel-mount button until the status LED gives two quick blinks, at about three seconds, then release.
3. The awning should jog again to confirm the new remote is registered.

## Confirm Control

1. Send `retract`, `extend`, and `stop` from the serial monitor (Somfy calls these `Up`, `Down`, and `My`), or tap the panel-mount button for stop.
2. Confirm the awning rolls up on `retract`, unrolls on `extend`, and halts on `stop`. These name the physical motion directly, so they are unaffected by which way `INVERT_DIRECTION` maps open and close.
3. Power-cycle the ESP32, then send more commands. They must keep working after the reboot. If they do, the rolling code is persisting correctly in NVS.

Validate from the intended mounting spot, not just next to the motor, so the range check reflects real placement.

## Button Reference

The panel-mount button on GPIO32 covers installation and recovery without a laptop. The action is chosen by how long the button is held, and the status LED on GPIO33 marks each threshold as it is crossed, so the button is released on a cue rather than by estimating elapsed time (see [the LED status codes in the hardware reference](hardware.md#button-and-led-wiring)).

| Hold until | LED cue | Release effect |
| --- | --- | --- |
| Press registers | Blinks off and back on once | Stop the awning (Somfy calls this `My`) |
| Pairing threshold, about 3 seconds | Two quick blinks | Pair with the awning (Somfy calls this `Prog`) |
| Reset threshold, about 10 seconds | Four slow, heavy blinks | Full factory reset, fired while still held: decommissions Matter and clears stored Wi-Fi credentials; device reopens `Awning-Setup` portal on next boot |

The onboard BOOT button is deliberately not used at runtime, because it ends up sealed inside the enclosure.

## Troubleshooting

If the awning jogs on pair but then ignores open and close, the rolling code is almost certainly not persisting. Type `status` in the serial monitor to read the current rolling code, send a command, and check `status` again; the value should increase each time and survive a reboot. If it does not, confirm the NVS storage is working and that the storage key length is within the ESP32 NVS limits; the Somfy library documentation warns about long keys, which is why the namespace and key in `src/config.h` are kept short.

If nothing happens on pair at all, re-check the frequency and wiring. The single most common first-build failure is transmitting on 433.92 MHz instead of the North American Somfy 433.42 MHz, but this firmware sets 433.42 MHz in `src/config.h`, so a no-response result more likely points to the `GDO0` data line, the antenna, or 3V3 power to the CC1101.

If commands work up close but not from the mounting location, improve the antenna or move the device. See [the radio range notes](hardware.md#radio-range).

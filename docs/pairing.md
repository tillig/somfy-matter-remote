# Somfy Pairing Procedure

- [What Pairing Does](#what-pairing-does)
- [Before You Start](#before-you-start)
- [Register The Virtual Remote](#register-the-virtual-remote)
- [Confirm Control](#confirm-control)
- [Button Reference](#button-reference)
- [Troubleshooting](#troubleshooting)

## What Pairing Does

This procedure registers the ESP32's virtual remote as an additional remote on the awning motor. It uses the existing physical Telis remote to put the motor into programming mode. The physical remote keeps working afterward.

The ESP32 does not clone the physical remote. It transmits its own 24-bit remote ID (set by `REMOTE_ID` in `src/config.h`) and is added to the motor as a new remote through the normal Somfy add-a-remote process. Choose the remote ID once and never change it; the motor tracks each remote by ID and rolling code, so a changed ID reads as an unknown remote.

This is hardware-gated work. It cannot be validated without the physical CC1101, antenna, and awning motor.

## Before You Start

1. Complete the wiring and pre-power checks in [the hardware reference](hardware.md), including confirming the antenna is attached.
2. Flash the firmware and open the serial monitor at 115200 baud (see [the build and flash steps in the README](../README.md)).
3. Confirm the boot log reports that the CC1101 initialized at 433.42 MHz. If it reports that the radio was not detected, fix the wiring before continuing.
4. Locate the Prog button on the back of the physical Telis remote. It is often behind the battery cover or a small pinhole.

## Register The Virtual Remote

1. Press and hold the Prog button on the physical remote until the awning motor jogs briefly, a short back-and-forth movement. The motor is now in programming mode for a few seconds.
2. Within that window, send the Somfy Prog command from the ESP32. On a bench, type `Prog` in the serial monitor. On an assembled unit, hold the panel-mount pairing button for about 3 seconds until the status LED blinks its pairing pattern.
3. The awning should jog again to confirm the new remote is registered.

## Confirm Control

1. Send `Up`, `Down`, and `My` from the serial monitor, or use the panel-mount button short press for `My`.
2. Confirm the awning retracts on `Up`, extends on `Down`, and stops on `My`.
3. Power-cycle the ESP32, then send more commands. They must keep working after the reboot. If they do, the rolling code is persisting correctly in NVS.

Validate from the intended mounting spot, not just next to the motor, so the range check reflects real placement.

## Button Reference

The panel-mount button on GPIO32 covers installation and recovery without a laptop. The action is chosen by how long the button is held, and the status LED on GPIO33 confirms each action.

| Action | Hold duration | Somfy or system effect | LED feedback |
| --- | --- | --- | --- |
| Short press | Under 1 second | My (stop) | Single short blink |
| Medium press | About 3 seconds | Prog (enter add-a-remote mode) | Rapid six-blink flurry |
| Long press | About 10 seconds | Full factory reset: decommissions Matter and clears stored Wi-Fi credentials; device reopens `Awning-Setup` portal on next boot | Slow four-blink pattern |

The onboard BOOT button is deliberately not used at runtime, because it ends up sealed inside the enclosure.

## Troubleshooting

If the awning jogs on Prog but then ignores Up and Down, the rolling code is almost certainly not persisting. Type `status` in the serial monitor to read the current rolling code, send a command, and check `status` again; the value should increase each time and survive a reboot. If it does not, confirm the NVS storage is working and that the storage key length is within the ESP32 NVS limits; the Somfy library documentation warns about long keys, which is why the namespace and key in `src/config.h` are kept short.

If nothing happens on Prog at all, re-check the frequency and wiring. The single most common first-build failure is transmitting on 433.92 MHz instead of the North American Somfy 433.42 MHz, but this firmware sets 433.42 MHz in `src/config.h`, so a no-response result more likely points to the `GDO0` data line, the antenna, or 3V3 power to the CC1101.

If commands work up close but not from the mounting location, improve the antenna or move the device. See [the radio range notes](hardware.md#radio-range).

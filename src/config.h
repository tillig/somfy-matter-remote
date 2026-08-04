#pragma once

#include <Arduino.h>

// Central compile-time configuration for the Somfy Awning Matter Remote.
// Values here are stable for the life of a given physical build. In
// particular, REMOTE_ID must never change once the device is paired with the
// awning motor, or the motor will treat it as an unknown remote.

// Somfy virtual remote identity. Any arbitrary 24-bit constant. This is NOT a
// clone of the physical Telis remote; it is a brand-new remote registered as an
// additional remote on the motor. Choose once, then never change.
static constexpr uint32_t REMOTE_ID = 0x100001;

// Radio frequency. North American Somfy RTS is 433.42 MHz, not the 433.92 MHz
// that most generic 433 MHz gear uses. This precision is why a CC1101 is
// required rather than a fixed-frequency transmitter.
static constexpr float FREQUENCY_MHZ = 433.42f;

// CC1101 wiring. The emitter pin is the data line the Somfy library bit-bangs;
// it must be physically wired to the CC1101 GDO0 pin.
//
// The SPI pins are passed to the driver explicitly, and the ESP32 routes SPI
// through its GPIO matrix, so these are not fixed to the default VSPI pins.
// Any free bidirectional GPIO works; MOSI uses 21 rather than the default 23
// purely because it is easier to reach on the board. Avoid the input-only pins
// (34 to 39), the strapping pins, and 6 to 11, which are wired to flash.
//
// The emitter must not be a strapping pin. It was originally GPIO2, which is
// one: the CC1101 drives GDO0 as an output by default, so a powered radio held
// GPIO2 high and the ESP32 could no longer be put into download mode, making
// uploads fail with "Wrong boot mode detected" until the data wire was pulled.
// GPIO4 is not a strapping pin, so the radio can stay connected while flashing.
static constexpr uint8_t EMITTER_GPIO = 4; // -> CC1101 GDO0 (OOK data out)
static constexpr uint8_t CC1101_SCK = 18;  // -> CC1101 SCK
static constexpr uint8_t CC1101_MISO = 19; // -> CC1101 MISO / SO
static constexpr uint8_t CC1101_MOSI = 21; // -> CC1101 MOSI / SI
static constexpr uint8_t CC1101_CSN = 5;   // -> CC1101 CSN / CS

// Dedicated panel-mount controls. The onboard BOOT button is deliberately not
// used at runtime because it ends up sealed inside the enclosure.
static constexpr uint8_t PAIR_BUTTON_GPIO = 32; // to ground, uses internal pull-up
static constexpr uint8_t STATUS_LED_GPIO = 33;  // to LED anode via ~330 ohm resistor

// NVS namespace and key for the rolling code. The Somfy library warns that NVS
// key length is limited, so both strings are kept short.
static constexpr const char* NVS_ROLLING_CODE_NAMESPACE = "somfy";
static constexpr const char* NVS_ROLLING_CODE_KEY = "awning";

// Direction mapping. Matter Window Covering treats 0% lift as fully open and
// 100% as fully closed, and for a roller-style covering that means open =
// retracted. An awning is spoken about the other way round: "open the awning"
// means unroll it for shade, and "closed" means rolled up and put away.
//
// INVERT_DIRECTION=1 (the default for this build) adopts the awning sense, so
// Matter Open sends Somfy Down to extend and Matter Close sends Somfy Up to
// retract. Set it to 0 for the literal Matter reading, where Open retracts.
//
// Either way the reported lift percentage stays consistent with the command
// that was issued, so a controller tile never contradicts itself: after Open it
// reads open, after Close it reads closed.
#ifndef INVERT_DIRECTION
#define INVERT_DIRECTION 1
#endif

// Button hold-duration thresholds, in milliseconds. The LED marks each
// threshold as it is crossed, so the button is released on a cue rather than by
// guessing at elapsed time:
//   - Released before PRESS_PAIR_MS: stop the awning.
//   - Released after PRESS_PAIR_MS:  send the pairing command.
//   - Still held at PRESS_RESET_MS:  factory reset, fired without waiting for
//     release so the LED confirmation is not mistaken for a further threshold.
// There is deliberately no gap between the windows: every release maps to an
// action the LED has already announced.
static constexpr uint32_t PRESS_PAIR_MS = 3000;
static constexpr uint32_t PRESS_RESET_MS = 10000;

// Button debounce interval in milliseconds.
static constexpr uint32_t BUTTON_DEBOUNCE_MS = 30;

// Status LED timing. The idle indication is a repeating count of short pulses,
// so the number of pulses identifies the device state (see StatusCode in
// main.cpp). Counting pulses separated by a clear gap is far more reliable to
// read than judging blink rates.
// A shorter lit phase than gap makes the pulses read as distinct blips, which is
// easier to count than an even on/off square wave.
static constexpr uint16_t LED_PULSE_ON_MS = 120;
static constexpr uint16_t LED_PULSE_OFF_MS = 220;
// Gap between the end of one count and the start of the next.
static constexpr uint16_t LED_CODE_GAP_MS = 1500;

// Network and web interface. This ESP32 Matter build has no over-BLE
// commissioning, so the firmware joins Wi-Fi itself using credentials entered
// through the SoftAP setup portal. When no credentials are stored, the device
// hosts an open access point so a phone or laptop can browse to the setup form.
//
// Both names below are base names. WiFiConnection appends a per-device suffix
// derived from the chip MAC (for example "Awning-Setup-a4c1" and
// "somfy-awning-a4c1"), so two units never collide on the setup SSID or the
// mDNS hostname.
static constexpr const char* SETUP_AP_SSID_BASE = "Awning-Setup";
static constexpr const char* DEVICE_HOSTNAME_BASE = "somfy-awning";
// How long to wait for a Wi-Fi station connection before giving up on a boot
// attempt, in milliseconds.
static constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 20000;
// Consecutive authentication failures before the device concludes its stored
// password is stale (for example the network password was changed) and falls
// back to the setup portal so a new one can be entered without a factory reset.
// A transient disconnect does not count; only genuine auth rejections do.
static constexpr uint8_t WIFI_AUTH_FAIL_LIMIT = 3;

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

// CC1101 wiring (VSPI). The emitter pin is the data line the Somfy library
// bit-bangs; it must be physically wired to the CC1101 GDO0 pin.
static constexpr uint8_t EMITTER_GPIO = 2; // -> CC1101 GDO0 (OOK data out)
static constexpr uint8_t CC1101_SCK = 18;  // -> CC1101 SCK
static constexpr uint8_t CC1101_MISO = 19; // -> CC1101 MISO / SO
static constexpr uint8_t CC1101_MOSI = 23; // -> CC1101 MOSI / SI
static constexpr uint8_t CC1101_CSN = 5;   // -> CC1101 CSN / CS

// Dedicated panel-mount controls. The onboard BOOT button is deliberately not
// used at runtime because it ends up sealed inside the enclosure.
static constexpr uint8_t PAIR_BUTTON_GPIO = 32; // to ground, uses internal pull-up
static constexpr uint8_t STATUS_LED_GPIO = 33;  // to LED anode via ~330 ohm resistor

// NVS namespace and key for the rolling code. The Somfy library warns that NVS
// key length is limited, so both strings are kept short.
static constexpr const char* NVS_ROLLING_CODE_NAMESPACE = "somfy";
static constexpr const char* NVS_ROLLING_CODE_KEY = "awning";

// Direction mapping. Matter Window Covering treats 0% lift as fully open
// (retracted) and 100% as fully closed (extended). Somfy uses Up to retract and
// Down to extend. With INVERT_DIRECTION=0 (the convention-correct default),
// Matter Open maps to Somfy Up and Matter Close maps to Somfy Down. Flip the
// build flag if "open" and "close" feel backward in daily use.
#ifndef INVERT_DIRECTION
#define INVERT_DIRECTION 0
#endif

// Button press-duration thresholds, in milliseconds. A press is classified by
// how long the button is held before release.
static constexpr uint32_t PRESS_SHORT_MAX_MS = 1000;  // < 1s  -> My (stop)
static constexpr uint32_t PRESS_MEDIUM_MAX_MS = 5000; // ~3s   -> Prog (pair)
// Anything held at least this long triggers a Matter factory reset.
static constexpr uint32_t PRESS_LONG_MS = 10000; // ~10s  -> decommission

// Button debounce interval in milliseconds.
static constexpr uint32_t BUTTON_DEBOUNCE_MS = 30;

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

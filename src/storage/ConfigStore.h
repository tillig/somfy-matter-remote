#pragma once

#include <Arduino.h>

// ConfigStore is the persistence layer for firmware-owned state that must
// survive reboots. It uses a dedicated NVS namespace via the Preferences API.
//
// The Somfy rolling code is deliberately NOT stored here: the Somfy library's
// NVSRollingCodeStorage owns that in its own namespace. ConfigStore instead
// persists the estimated lift position, because a Somfy awning gives no motor
// feedback and the Matter tile should still show a sensible last-known state
// after a power cycle.
class ConfigStore {
public:
    ConfigStore();

    // Open the NVS namespace. Call once during setup().
    void begin();

    // Last-known lift percentage using Matter's convention: 0 = fully open
    // (retracted), 100 = fully closed (extended). Defaults to 0 (open) on a
    // fresh device. getLiftPercent() returns the cached value and never
    // touches flash.
    uint8_t getLiftPercent() const;
    void setLiftPercent(uint8_t percent);

private:
    uint8_t liftPercent = 0;
};

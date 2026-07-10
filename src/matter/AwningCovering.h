#pragma once

#include <Matter.h>
#include <MatterEndpoints/MatterWindowCovering.h>

class SomfyController;
class ConfigStore;

// AwningCovering is the Matter presentation layer. It owns the Matter Window
// Covering endpoint and maps its commands onto the SomfyController, honoring the
// INVERT_DIRECTION build flag. It never touches CC1101 registers directly.
//
// Because a Somfy awning gives no position feedback, the endpoint reports only
// the two end states (fully open / fully closed): an open/close/stop command
// drives the motor and then snaps the reported lift to the corresponding end
// stop, which is persisted through ConfigStore.
class AwningCovering {
public:
    AwningCovering(SomfyController& rf, ConfigStore& store);

    // Bring up Matter and the Window Covering endpoint, restore the last-known
    // position, and register command callbacks. Call after SomfyController and
    // ConfigStore are initialized. Prints commissioning info to Serial.
    void begin();

    // Service Matter housekeeping and report any pending state to controllers.
    // Call from loop().
    void loop();

    // True once Google Home (or another admin) has commissioned the device.
    bool isCommissioned() const;

    // Remove all Matter fabrics and factory-reset the Matter data, so the
    // device can be re-commissioned. Triggered by the long button press.
    void decommission();

private:
    // Drive the awning to an end stop and remember it. `liftPercent` uses the
    // Matter convention: 0 = open (retracted), 100 = closed (extended).
    void moveToEndStop(uint8_t liftPercent);

    SomfyController& rf;
    ConfigStore& store;
    MatterWindowCovering covering;
    bool wasCommissioned = false;
};

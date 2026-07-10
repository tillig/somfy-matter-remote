#include "ConfigStore.h"

#include <Preferences.h>

// NVS namespace and key for firmware-owned state. Kept short to stay within the
// ESP32 NVS key-length limit, and distinct from the Somfy rolling-code
// namespace so the two never collide.
static constexpr const char* CONFIG_NAMESPACE = "awningcfg";
static constexpr const char* KEY_LIFT_PERCENT = "lift";

static Preferences preferences;

ConfigStore::ConfigStore() {}

void ConfigStore::begin() {
    // Read the persisted position once into the in-memory cache. Writes happen
    // on change; reads are served from the cache to avoid flash wear.
    preferences.begin(CONFIG_NAMESPACE, /*readOnly=*/false);
    liftPercent = preferences.getUChar(KEY_LIFT_PERCENT, 0);
    preferences.end();
}

uint8_t ConfigStore::getLiftPercent() const {
    return liftPercent;
}

void ConfigStore::setLiftPercent(uint8_t percent) {
    if (percent > 100) {
        percent = 100;
    }
    if (percent == liftPercent) {
        return; // no change, skip the flash write
    }
    liftPercent = percent;
    preferences.begin(CONFIG_NAMESPACE, /*readOnly=*/false);
    preferences.putUChar(KEY_LIFT_PERCENT, liftPercent);
    preferences.end();
}

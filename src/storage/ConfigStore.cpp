#include "ConfigStore.h"

#include <Preferences.h>

// NVS namespace and key for firmware-owned state. Kept short to stay within the
// ESP32 NVS key-length limit, and distinct from the Somfy rolling-code
// namespace so the two never collide.
static constexpr const char* CONFIG_NAMESPACE = "awningcfg";
static constexpr const char* KEY_LIFT_PERCENT = "lift";
static constexpr const char* KEY_WIFI_SSID = "ssid";
static constexpr const char* KEY_WIFI_PASS = "pass";

static Preferences preferences;

ConfigStore::ConfigStore() {}

void ConfigStore::begin() {
    // Read the persisted state once into the in-memory cache. Writes happen on
    // change; reads are served from the cache to avoid flash wear.
    preferences.begin(CONFIG_NAMESPACE, /*readOnly=*/false);
    liftPercent = preferences.getUChar(KEY_LIFT_PERCENT, 0);
    wifiSsid = preferences.getString(KEY_WIFI_SSID, "");
    wifiPassword = preferences.getString(KEY_WIFI_PASS, "");
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

bool ConfigStore::hasWiFiCredentials() const {
    return wifiSsid.length() > 0;
}

String ConfigStore::getWiFiSsid() const {
    return wifiSsid;
}

String ConfigStore::getWiFiPassword() const {
    return wifiPassword;
}

void ConfigStore::setWiFiCredentials(const String& ssid, const String& password) {
    wifiSsid = ssid;
    wifiPassword = password;
    preferences.begin(CONFIG_NAMESPACE, /*readOnly=*/false);
    preferences.putString(KEY_WIFI_SSID, wifiSsid);
    preferences.putString(KEY_WIFI_PASS, wifiPassword);
    preferences.end();
}

void ConfigStore::clearWiFiCredentials() {
    wifiSsid = "";
    wifiPassword = "";
    preferences.begin(CONFIG_NAMESPACE, /*readOnly=*/false);
    preferences.remove(KEY_WIFI_SSID);
    preferences.remove(KEY_WIFI_PASS);
    preferences.end();
}

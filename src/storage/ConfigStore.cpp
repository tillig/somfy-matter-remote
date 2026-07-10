#include "ConfigStore.h"

#include <Preferences.h>

// NVS namespace and key for firmware-owned state. Kept short to stay within the
// ESP32 NVS key-length limit, and distinct from the Somfy rolling-code
// namespace so the two never collide.
static constexpr const char* CONFIG_NAMESPACE = "awningcfg";
static constexpr const char* KEY_LIFT_PERCENT = "lift";
static constexpr const char* KEY_WIFI_SSID = "ssid";
static constexpr const char* KEY_WIFI_PASS = "pass";
static constexpr const char* KEY_PEND_SSID = "pssid";
static constexpr const char* KEY_PEND_PASS = "ppass";

static Preferences preferences;

ConfigStore::ConfigStore() {}

void ConfigStore::begin() {
    // Read the persisted state once into the in-memory cache. Writes happen on
    // change; reads are served from the cache to avoid flash wear.
    preferences.begin(CONFIG_NAMESPACE, /*readOnly=*/false);
    liftPercent = preferences.getUChar(KEY_LIFT_PERCENT, 0);
    wifiSsid = preferences.getString(KEY_WIFI_SSID, "");
    wifiPassword = preferences.getString(KEY_WIFI_PASS, "");
    pendingSsid = preferences.getString(KEY_PEND_SSID, "");
    pendingPassword = preferences.getString(KEY_PEND_PASS, "");
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
    pendingSsid = "";
    pendingPassword = "";
    preferences.begin(CONFIG_NAMESPACE, /*readOnly=*/false);
    preferences.remove(KEY_WIFI_SSID);
    preferences.remove(KEY_WIFI_PASS);
    preferences.remove(KEY_PEND_SSID);
    preferences.remove(KEY_PEND_PASS);
    preferences.end();
}

bool ConfigStore::hasPendingWiFiCredentials() const {
    return pendingSsid.length() > 0;
}

String ConfigStore::getPendingWiFiSsid() const {
    return pendingSsid;
}

String ConfigStore::getPendingWiFiPassword() const {
    return pendingPassword;
}

void ConfigStore::setPendingWiFiCredentials(const String& ssid, const String& password) {
    pendingSsid = ssid;
    pendingPassword = password;
    preferences.begin(CONFIG_NAMESPACE, /*readOnly=*/false);
    preferences.putString(KEY_PEND_SSID, pendingSsid);
    preferences.putString(KEY_PEND_PASS, pendingPassword);
    preferences.end();
}

void ConfigStore::clearPendingWiFiCredentials() {
    pendingSsid = "";
    pendingPassword = "";
    preferences.begin(CONFIG_NAMESPACE, /*readOnly=*/false);
    preferences.remove(KEY_PEND_SSID);
    preferences.remove(KEY_PEND_PASS);
    preferences.end();
}

void ConfigStore::promotePendingWiFiCredentials() {
    if (pendingSsid.length() == 0) {
        return;
    }
    wifiSsid = pendingSsid;
    wifiPassword = pendingPassword;
    pendingSsid = "";
    pendingPassword = "";
    preferences.begin(CONFIG_NAMESPACE, /*readOnly=*/false);
    preferences.putString(KEY_WIFI_SSID, wifiSsid);
    preferences.putString(KEY_WIFI_PASS, wifiPassword);
    preferences.remove(KEY_PEND_SSID);
    preferences.remove(KEY_PEND_PASS);
    preferences.end();
}

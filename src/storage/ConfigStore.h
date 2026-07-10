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

    // Active Wi-Fi station credentials. This ESP32 Matter build has no over-BLE
    // commissioning, so the firmware must join Wi-Fi itself before Google Home
    // can commission it. Credentials are entered through the SoftAP setup
    // portal or the dashboard, never hardcoded in source. The getters return
    // cached values and never touch flash.
    bool hasWiFiCredentials() const;
    String getWiFiSsid() const;
    String getWiFiPassword() const;
    void setWiFiCredentials(const String& ssid, const String& password);
    void clearWiFiCredentials();

    // Pending Wi-Fi credentials are a candidate network submitted from the
    // dashboard while the device is already connected. The browser cannot be
    // told live whether the new network works (testing it means leaving the
    // current one), so the candidate is stored as pending and tried on the next
    // boot. If it connects it is promoted to active; if it fails the device
    // reverts to the active credentials, so a typo never strands the device.
    bool hasPendingWiFiCredentials() const;
    String getPendingWiFiSsid() const;
    String getPendingWiFiPassword() const;
    void setPendingWiFiCredentials(const String& ssid, const String& password);
    void clearPendingWiFiCredentials();
    // Copy the pending credentials into the active slot and clear pending.
    void promotePendingWiFiCredentials();

private:
    uint8_t liftPercent = 0;
    String wifiSsid;
    String wifiPassword;
    String pendingSsid;
    String pendingPassword;
};

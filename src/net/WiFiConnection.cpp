#include "WiFiConnection.h"

#include <ESPmDNS.h>
#include <WiFi.h>

#include "../config.h"
#include "../storage/ConfigStore.h"

// Interval between station reconnection attempts while disconnected, in
// milliseconds.
static constexpr uint32_t RECONNECT_INTERVAL_MS = 10000;

WiFiConnection::WiFiConnection(ConfigStore& store) : store(store) {}

String WiFiConnection::deviceSuffix() {
    // Last two bytes of the factory-programmed chip MAC, stable across reboots.
    const uint64_t mac = ESP.getEfuseMac();
    char buf[5];
    snprintf(buf, sizeof(buf), "%02x%02x", static_cast<uint8_t>(mac >> 8), static_cast<uint8_t>(mac));
    return String(buf);
}

// cppcheck-suppress functionStatic ; part of the WiFiConnection instance API
// (callers use net.getHostname()), even though the suffix source is static.
String WiFiConnection::getHostname() const {
    return String(DEVICE_HOSTNAME_BASE) + "-" + deviceSuffix();
}

// cppcheck-suppress functionStatic ; part of the WiFiConnection instance API,
// even though the suffix source is static.
String WiFiConnection::getSetupApSsid() const {
    return String(SETUP_AP_SSID_BASE) + "-" + deviceSuffix();
}

WiFiConnection::Mode WiFiConnection::begin() {
    WiFi.persistent(false); // credentials live in ConfigStore, not the WiFi lib's own NVS
    WiFi.setHostname(getHostname().c_str());

    if (store.hasWiFiCredentials() && connectStation()) {
        mode = Mode::Station;
    } else if (store.hasWiFiCredentials()) {
        // Credentials exist but the network was unreachable this boot. Stay in
        // station mode and let loop() keep retrying rather than dropping into
        // setup, so a temporary router outage does not strand the device.
        mode = Mode::Station;
    } else {
        startSetupAp();
        mode = Mode::SetupAp;
    }
    return mode;
}

bool WiFiConnection::connectStation() {
    WiFi.mode(WIFI_STA);
    Serial.print("[net] Connecting to Wi-Fi SSID: ");
    Serial.println(store.getWiFiSsid());
    WiFi.begin(store.getWiFiSsid().c_str(), store.getWiFiPassword().c_str());

    const uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - start) < WIFI_CONNECT_TIMEOUT_MS) {
        delay(250);
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.print("[net] Connected. IP address: ");
        Serial.println(WiFi.localIP());
        // Publish an mDNS responder so the dashboard is reachable by name; the
        // Matter stack runs its own commissioning mDNS separately from this.
        if (MDNS.begin(getHostname().c_str())) {
            MDNS.addService("http", "tcp", 80);
            Serial.print("[net] Dashboard at http://");
            Serial.print(getHostname());
            Serial.println(".local");
        } else {
            Serial.println("[net] Warning: mDNS responder failed to start; use the IP address.");
        }
        return true;
    }
    Serial.println("[net] Wi-Fi connection timed out; will keep retrying.");
    return false;
}

void WiFiConnection::startSetupAp() {
    WiFi.mode(WIFI_AP);
    // Open network so a phone can join without a password and reach the portal.
    // The SSID carries a per-device suffix so two units do not present the same
    // network name.
    const String ssid = getSetupApSsid();
    WiFi.softAP(ssid.c_str());
    Serial.print("[net] No Wi-Fi credentials. Hosting setup access point: ");
    Serial.println(ssid);
    Serial.print("[net] Browse to http://");
    Serial.print(WiFi.softAPIP());
    Serial.println(" to configure Wi-Fi.");
}

void WiFiConnection::loop() {
    if (mode != Mode::Station) {
        return;
    }
    if (WiFi.status() == WL_CONNECTED) {
        return;
    }
    const uint32_t now = millis();
    if (now - lastReconnectAttempt < RECONNECT_INTERVAL_MS) {
        return;
    }
    lastReconnectAttempt = now;
    Serial.println("[net] Wi-Fi disconnected; attempting reconnect.");
    WiFi.reconnect();
}

bool WiFiConnection::isStationConnected() const {
    return mode == Mode::Station && WiFi.status() == WL_CONNECTED;
}

IPAddress WiFiConnection::getIP() const {
    if (mode == Mode::SetupAp) {
        return WiFi.softAPIP();
    }
    return WiFi.localIP();
}

int WiFiConnection::getRssi() const {
    return isStationConnected() ? WiFi.RSSI() : 0;
}

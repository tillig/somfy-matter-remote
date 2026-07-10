#include "WiFiConnection.h"

#include <ESPmDNS.h>
#include <WiFi.h>

#include "../config.h"
#include "../storage/ConfigStore.h"

// Interval between station reconnection attempts while disconnected, in
// milliseconds.
static constexpr uint32_t RECONNECT_INTERVAL_MS = 10000;

// The disconnect-reason event handler runs on the Wi-Fi event task, so the
// state it touches is file-scope and read on the main task. Only the reason
// code and a counter are shared, both single-word writes.
//
// consecutiveAuthFailures counts genuine password rejections. The reason
// arrives asynchronously after a connect attempt fails, which is why it is
// tracked through the event rather than inferred from WiFi.status().
static volatile uint8_t consecutiveAuthFailures = 0;
static volatile bool eventHandlerInstalled = false;

// Reason codes that mean "the password was rejected" rather than "the network
// was not reachable". A changed network password shows up as one of these.
static bool isAuthFailureReason(uint8_t reason) {
    switch (reason) {
        case WIFI_REASON_AUTH_FAIL:
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
        case WIFI_REASON_HANDSHAKE_TIMEOUT:
        case WIFI_REASON_MIC_FAILURE:
        case WIFI_REASON_802_1X_AUTH_FAILED:
            return true;
        default:
            // NO_AP_FOUND, BEACON_TIMEOUT, ASSOC_EXPIRE, and similar mean the
            // network is simply unreachable; those are transient, not bad
            // credentials.
            return false;
    }
}

WiFiConnection::WiFiConnection(ConfigStore& store) : store(store) {}

// cppcheck-suppress functionStatic ; part of the WiFiConnection instance
// lifecycle, even though the shared state it wires up is file-scope.
void WiFiConnection::installEventHandler() {
    if (eventHandlerInstalled) {
        return;
    }
    eventHandlerInstalled = true;
    WiFi.onEvent(
        [](arduino_event_id_t /*event*/, arduino_event_info_t info) {
            uint8_t reason = info.wifi_sta_disconnected.reason;
            if (isAuthFailureReason(reason)) {
                if (consecutiveAuthFailures < 255) {
                    consecutiveAuthFailures++;
                }
                Serial.printf("[net] Wi-Fi auth failure (reason %u), count %u.\n", reason, consecutiveAuthFailures);
            } else {
                Serial.printf("[net] Wi-Fi disconnected (reason %u).\n", reason);
            }
        },
        ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
}

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
    installEventHandler();

    // A pending network was submitted from the dashboard. Try it first; promote
    // it on success, or discard it and fall back to the active network on
    // failure, so a dashboard typo cannot strand the device.
    if (store.hasPendingWiFiCredentials()) {
        Serial.println("[net] Trying pending Wi-Fi credentials from the dashboard.");
        if (connectStation(store.getPendingWiFiSsid(), store.getPendingWiFiPassword())) {
            store.promotePendingWiFiCredentials();
            Serial.println("[net] Pending network connected and promoted to active.");
            mode = Mode::Station;
            return mode;
        }
        store.clearPendingWiFiCredentials();
        Serial.println("[net] Pending network failed; reverting to the previous network.");
    }

    if (!store.hasWiFiCredentials()) {
        startSetupAp();
        mode = Mode::SetupAp;
        return mode;
    }

    // Try the active credentials.
    consecutiveAuthFailures = 0;
    if (connectStation(store.getWiFiSsid(), store.getWiFiPassword())) {
        mode = Mode::Station;
        return mode;
    }

    // The connect failed. If it was rejected as an auth failure, the stored
    // password is almost certainly stale (for example the network password was
    // changed), so fall back to the setup portal to collect a new one without a
    // factory reset. Any other reason is treated as a transient outage: stay in
    // station mode and keep retrying so the device does not drop out of Google
    // Home over a brief router reboot.
    if (consecutiveAuthFailures > 0) {
        Serial.println("[net] Stored Wi-Fi password was rejected; opening setup portal to re-enter it.");
        authRecovery = true;
        startSetupAp();
        mode = Mode::SetupAp;
    } else {
        mode = Mode::Station;
    }
    return mode;
}

bool WiFiConnection::connectStation(const String& ssid, const String& password) {
    WiFi.mode(WIFI_STA);
    Serial.print("[net] Connecting to Wi-Fi SSID: ");
    Serial.println(ssid);
    WiFi.begin(ssid.c_str(), password.c_str());

    const uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - start) < WIFI_CONNECT_TIMEOUT_MS) {
        delay(250);
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.print("[net] Connected. IP address: ");
        Serial.println(WiFi.localIP());
        startMdns();
        return true;
    }
    Serial.println("[net] Wi-Fi connection timed out; will keep retrying.");
    return false;
}

void WiFiConnection::startMdns() {
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
}

// cppcheck-suppress functionStatic ; part of the WiFiConnection instance API,
// even though it only drives the WiFi singleton.
bool WiFiConnection::testCredentials(const String& ssid, const String& password) {
    // Keep the setup access point up while testing (AP+STA) so the caller's
    // browser stays connected to receive the result.
    consecutiveAuthFailures = 0;
    WiFi.mode(WIFI_AP_STA);
    Serial.print("[net] Testing Wi-Fi SSID: ");
    Serial.println(ssid);
    WiFi.begin(ssid.c_str(), password.c_str());

    const uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - start) < WIFI_CONNECT_TIMEOUT_MS) {
        delay(250);
    }

    const bool ok = WiFi.status() == WL_CONNECTED;
    if (ok) {
        Serial.println("[net] Test connection succeeded.");
    } else {
        // Drop the failed station attempt but leave the access point running so
        // the portal stays reachable for another try.
        Serial.println("[net] Test connection failed.");
        WiFi.disconnect(/*wifioff=*/false);
    }
    return ok;
}

void WiFiConnection::startSetupAp() {
    WiFi.mode(WIFI_AP);
    // Open network so a phone can join without a password and reach the portal.
    // The SSID carries a per-device suffix so two units do not present the same
    // network name.
    const String ssid = getSetupApSsid();
    WiFi.softAP(ssid.c_str());
    Serial.print("[net] Hosting setup access point: ");
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
        // A healthy connection clears any accumulated auth-failure count so a
        // later isolated glitch does not tip the device into recovery.
        consecutiveAuthFailures = 0;
        return;
    }

    // Repeated password rejections after we were configured mean the stored
    // password went stale (for example the network password was changed).
    // Reboot: begin() will see the auth failure again and land in the setup
    // portal so a new password can be entered without a factory reset. A
    // transient outage (network down) never reaches this because those
    // disconnects are not counted as auth failures.
    if (consecutiveAuthFailures >= WIFI_AUTH_FAIL_LIMIT) {
        Serial.println("[net] Stored Wi-Fi password repeatedly rejected; restarting into setup portal.");
        delay(200); // let the serial line flush
        ESP.restart();
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

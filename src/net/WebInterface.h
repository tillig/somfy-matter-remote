#pragma once

#include <Arduino.h>

class WiFiConnection;
class ConfigStore;
class AwningCovering;

// WebInterface is a single small web server that plays two roles depending on
// how the device booted, reported by WiFiConnection:
//
// - SetupAp mode: serves a Wi-Fi setup form and runs a captive-portal DNS
//   redirect so joining the setup access point pops the form automatically.
//   Saving credentials stores them and reboots into station mode.
// - Station mode: serves a diagnostics dashboard showing network status and
//   the Matter pairing information (device state, manual code, and QR link),
//   which is useful for commissioning and for multi-admin sharing later.
//
// It presents state and collects the setup form; it does not own Wi-Fi or
// Matter. Those live in WiFiConnection and AwningCovering.
class WebInterface {
public:
    WebInterface(WiFiConnection& net, ConfigStore& store, AwningCovering& awning);
    ~WebInterface();

    // Start the HTTP server (and, in SetupAp mode, the captive-portal DNS
    // server). Call after WiFiConnection::begin().
    void begin();

    // Service HTTP and DNS requests. Call from loop().
    void loop();

    // True once the setup form has saved credentials and the device should
    // reboot to apply them.
    bool shouldReboot() const {
        return rebootRequested;
    }

private:
    void handleRoot();
    void handleSave();
    void handleNotFound();
    // The setup page depends only on its argument, not on device state.
    static String renderSetupPage(const String& message);
    String renderDashboardPage() const;

    WiFiConnection& net;
    ConfigStore& store;
    AwningCovering& awning;
    bool rebootRequested = false;
    uint32_t rebootAt = 0;
};

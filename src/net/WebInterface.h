#pragma once

#include <Arduino.h>

class WiFiConnection;
class ConfigStore;
class AwningCovering;
class SomfyController;
class BootLog;

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
    WebInterface(
        WiFiConnection& net, ConfigStore& store, AwningCovering& awning, SomfyController& rf, BootLog& bootLog);
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
    // prefillSsid seeds the SSID field (used in auth recovery, where the network
    // name is usually unchanged and only the password is stale). The setup page
    // also needs the hostname it will be reachable at after joining, so people
    // know where to go next without watching the serial monitor.
    String renderSetupPage(const String& message, const String& prefillSsid) const;
    static String renderDashboardMessagePage(const String& message);
    String renderDashboardPage() const;
    // Shared markup: a password field with a show/hide toggle, and the startup
    // log rendered as a block.
    static String renderPasswordField(const String& id, const String& label);
    String renderBootLog() const;
    // Client-side update check: compares the installed version against the
    // latest GitHub release from the browser, so the device makes no outbound
    // network call. Fails quietly, leaving only the installed version shown.
    static String renderUpdateCheck();

    WiFiConnection& net;
    ConfigStore& store;
    AwningCovering& awning;
    SomfyController& rf;
    BootLog& bootLog;
    bool rebootRequested = false;
    uint32_t rebootAt = 0;
};

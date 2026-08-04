#include "WebInterface.h"

#include <DNSServer.h>
#include <Matter.h>
#include <WebServer.h>
#include <WiFi.h>

#include "../config.h"
#include "../diag/BootLog.h"
#include "../matter/AwningCovering.h"
#include "../rf/SomfyController.h"
#include "../storage/ConfigStore.h"
#include "WiFiConnection.h"

// The web and DNS servers are file-scope singletons: there is one HTTP surface
// on the device, and WebServer/DNSServer are not copyable.
static WebServer server(80);
static DNSServer dnsServer;

// Standard captive-portal DNS port, and the delay before rebooting after the
// setup form is saved so the confirmation page can be delivered first.
static constexpr uint16_t DNS_PORT = 53;
static constexpr uint32_t REBOOT_DELAY_MS = 1500;

WebInterface::WebInterface(
    WiFiConnection& net, ConfigStore& store, AwningCovering& awning, SomfyController& rf, BootLog& bootLog)
    : net(net), store(store), awning(awning), rf(rf), bootLog(bootLog) {}

WebInterface::~WebInterface() {
    server.stop();
    dnsServer.stop();
}

void WebInterface::begin() {
    server.on("/", HTTP_GET, [this]() { handleRoot(); });
    server.on("/save", HTTP_POST, [this]() { handleSave(); });
    server.onNotFound([this]() { handleNotFound(); });
    server.begin();

    if (net.getMode() == WiFiConnection::Mode::SetupAp) {
        // Redirect every hostname to the device so joining the AP pops the form.
        dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
    }
}

void WebInterface::loop() {
    if (net.getMode() == WiFiConnection::Mode::SetupAp) {
        dnsServer.processNextRequest();
    }
    server.handleClient();

    if (rebootRequested && millis() >= rebootAt) {
        Serial.println("[web] Rebooting to apply Wi-Fi credentials.");
        ESP.restart();
    }
}

void WebInterface::handleRoot() {
    if (net.getMode() == WiFiConnection::Mode::SetupAp) {
        // Explain an auth-recovery fallback differently from first-time setup.
        const String message = net.isAuthRecovery()
                                   ? "The saved Wi-Fi password was rejected, which usually means the "
                                     "network password changed. Enter the current password to reconnect."
                                   : "";
        // In auth recovery the network name is usually unchanged, so prefill it.
        server.send(200, "text/html", renderSetupPage(message, store.getWiFiSsid()));
    } else {
        server.send(200, "text/html", renderDashboardPage());
    }
}

void WebInterface::handleSave() {
    const String ssid = server.arg("ssid");
    const String password = server.arg("password");

    if (ssid.length() == 0) {
        server.send(200, "text/html", renderSetupPage("SSID cannot be empty.", ""));
        return;
    }

    if (net.getMode() == WiFiConnection::Mode::SetupAp) {
        // First-time setup: the phone is on our access point, which stays up
        // during the test, so we can validate the network live and only save on
        // success. A typo just shows an error and lets the user try again.
        if (net.testCredentials(ssid, password)) {
            store.setWiFiCredentials(ssid, password);
            server.send(
                200, "text/html", renderSetupPage("Connected to \"" + ssid + "\". Saving and restarting.", ssid));
            rebootRequested = true;
            rebootAt = millis() + REBOOT_DELAY_MS;
        } else {
            server.send(200,
                        "text/html",
                        renderSetupPage("Could not connect to \"" + ssid +
                                            "\". Check the network name and password, then try again.",
                                        ssid));
        }
        return;
    }

    // Dashboard change while already connected: the browser reaches us over the
    // current network, so we cannot report the result of a live test (testing
    // means leaving that network). Store the new network as pending and reboot;
    // the device tries it on boot and reverts to the current network if it
    // fails, so a typo never strands the device.
    store.setPendingWiFiCredentials(ssid, password);
    server.send(
        200,
        "text/html",
        renderDashboardMessagePage("Restarting to join \"" + ssid +
                                   "\". If it cannot connect, the device returns to the current network automatically. "
                                   "Reload this page in a moment."));
    rebootRequested = true;
    rebootAt = millis() + REBOOT_DELAY_MS;
}

void WebInterface::handleNotFound() {
    // In setup mode, send unknown paths to the form so the captive portal
    // detectors on phones open it. Otherwise return a plain 404.
    if (net.getMode() == WiFiConnection::Mode::SetupAp) {
        server.sendHeader("Location", "/", true);
        server.send(302, "text/plain", "");
        return;
    }
    server.send(404, "text/plain", "Not found");
}

// Minimal inline styling keeps the pages readable on a phone without external
// assets, which matters because the setup portal runs before the device has
// any internet access.
static const char* PAGE_STYLE = "<style>body{font-family:system-ui,sans-serif;margin:0;padding:2rem;"
                                "background:#111;color:#eee}h1{font-size:1.3rem}label{display:block;"
                                "margin:1rem 0 .3rem}input{width:100%;padding:.6rem;font-size:1rem;"
                                "border:1px solid #555;border-radius:6px;background:#222;color:#eee;"
                                "box-sizing:border-box}button{margin-top:1.5rem;padding:.7rem 1.2rem;"
                                "font-size:1rem;border:0;border-radius:6px;background:#3a7;color:#fff}"
                                "table{border-collapse:collapse;margin-top:1rem;width:100%}"
                                "td,th{text-align:left;padding:.4rem .6rem;border-bottom:1px solid #333}"
                                "a{color:#6cf}.msg{margin-top:1rem;padding:.6rem .8rem;"
                                "border-radius:6px;background:#223;border:1px solid #456}"
                                ".pw{display:flex;gap:.5rem;align-items:stretch}"
                                ".pw button{margin:0;padding:.6rem .8rem;background:#444;font-size:.9rem;"
                                "white-space:nowrap}"
                                "pre{background:#0b0b0b;border:1px solid #333;border-radius:6px;"
                                "padding:.7rem;overflow-x:auto;font-size:.8rem;line-height:1.4}"
                                ".ok{color:#7d7}.warn{color:#fd6}.bad{color:#f77}</style>";

// A password input with a show/hide toggle, so a typed network password can be
// checked before submitting. Uses a tiny inline script rather than a library.
String WebInterface::renderPasswordField(const String& id, const String& label) {
    String html = "<label for='" + id + "'>" + label + "</label>";
    html += "<div class='pw'>";
    html += "<input id='" + id + "' name='" + id + "' type='password' autocomplete='off'>";
    html += "<button type='button' onclick=\"(function(b){var f=document.getElementById('" + id +
            "');var s=f.type==='password';f.type=s?'text':'password';b.textContent=s?'Hide':'Show';})(this)\">Show"
            "</button>";
    html += "</div>";
    return html;
}

String WebInterface::renderBootLog() const {
    const uint8_t count = bootLog.count();
    if (count == 0) {
        return String();
    }
    String html = "<h2>Startup Log</h2>";
    html += "<p>What the device reported while starting up, so it can be checked "
            "without a serial monitor.</p><pre>";
    for (uint8_t i = 0; i < count; i++) {
        String entry = bootLog.line(i);
        // Escape the few characters that would break out of the pre block.
        entry.replace("&", "&amp;");
        entry.replace("<", "&lt;");
        entry.replace(">", "&gt;");
        html += entry + "\n";
    }
    html += "</pre>";
    return html;
}

String WebInterface::renderSetupPage(const String& message, const String& prefillSsid) const {
    const String dashboardUrl = "http://" + net.getHostname() + ".local";

    String html = "<!DOCTYPE html><html><head><meta charset='utf-8'>";
    html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
    html += "<title>Awning Wi-Fi Setup</title>";
    html += PAGE_STYLE;
    html += "</head><body><h1>Somfy Awning Wi-Fi Setup</h1>";
    html += "<p>Enter the Wi-Fi network this device should join. The password is "
            "checked before it is saved, so a mistyped password is reported here "
            "rather than silently stored.</p>";
    if (message.length() > 0) {
        html += "<p class='msg'>" + message + "</p>";
    }
    html += "<form method='POST' action='/save'>";
    html += "<label for='ssid'>Network name (SSID)</label>";
    html += "<input id='ssid' name='ssid' autocomplete='off' value='" + prefillSsid + "' required>";
    html += renderPasswordField("password", "Password");
    html += "<button type='submit'>Save and Restart</button></form>";

    // Tell people where the device moves to, so they are not left guessing after
    // this access point disappears.
    html += "<h2>What Happens Next</h2>";
    html += "<p>After saving, this setup network shuts down and the device joins "
            "your Wi-Fi. Its status page, including the Matter pairing code for "
            "Google Home, is then at:</p>";
    html += "<p class='msg'><strong>" + dashboardUrl + "</strong></p>";
    html += "<p>If that address does not resolve on your network, the device also "
            "prints its IP address to the serial monitor, and your router will "
            "list it as <code>" +
            net.getHostname() + "</code>.</p>";
    html += "</body></html>";
    return html;
}

String WebInterface::renderDashboardMessagePage(const String& message) {
    String html = "<!DOCTYPE html><html><head><meta charset='utf-8'>";
    html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
    html += "<title>Somfy Awning</title>";
    html += PAGE_STYLE;
    html += "</head><body><h1>Somfy Awning</h1>";
    html += "<p class='msg'>" + message + "</p>";
    html += "<p><a href='/'>Back to dashboard</a></p>";
    html += "</body></html>";
    return html;
}

String WebInterface::renderDashboardPage() const {
    const bool commissioned = awning.isCommissioned();

    String html = "<!DOCTYPE html><html><head><meta charset='utf-8'>";
    html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
    html += "<title>Somfy Awning</title>";
    html += PAGE_STYLE;
    html += "</head><body><h1>Somfy Awning</h1>";

    const bool radioReady = rf.isRadioReady();
    const uint16_t rollingCode = rf.peekRollingCode();

    html += "<h2>Status</h2>";
    html += "<table>";
    html += "<tr><th>Radio</th><td>";
    html += radioReady ? "<span class='ok'>CC1101 detected</span>"
                       : "<span class='bad'>CC1101 not detected. Check the SPI wiring and 3V3 power.</span>";
    html += "</td></tr>";
    html += "<tr><th>Frequency</th><td>433.42 MHz</td></tr>";
    html += "<tr><th>Awning commands sent</th><td>";
    if (rollingCode == 0) {
        html += "<span class='warn'>None yet. The awning still needs the pairing step.</span>";
    } else {
        html += "Yes, rolling code is at " + String(rollingCode);
    }
    html += "</td></tr>";
    html += "<tr><th>Matter</th><td>";
    html += commissioned ? "<span class='ok'>Commissioned</span>"
                         : "<span class='warn'>Not commissioned. Add it in Google Home using the code below.</span>";
    html += "</td></tr>";
    html += "<tr><th>Wi-Fi</th><td>" + store.getWiFiSsid() + " (" + String(net.getRssi()) + " dBm)</td></tr>";
    html += "<tr><th>Hostname</th><td>" + net.getHostname() + ".local</td></tr>";
    html += "<tr><th>IP address</th><td>" + net.getIP().toString() + "</td></tr>";
    html += "</table>";

    // The device cannot know whether the motor accepted the pairing, because
    // Somfy RTS is transmit-only with no acknowledgement. Say so plainly instead
    // of implying a confirmed state.
    html += "<h2>Awning Pairing</h2>";
    html += "<p>Pairing registers this device as an extra remote on the awning "
            "motor. Your existing remote keeps working. The motor only accepts a "
            "new remote while it is in programming mode, which the existing "
            "remote puts it into.</p>";
    html += "<ol>";
    html += "<li>Find the <code>Prog</code> button on the back of the physical "
            "Telis remote, often behind the battery cover or in a small "
            "pinhole.</li>";
    html += "<li>Press and hold <code>Prog</code> until the awning jogs, a short "
            "back-and-forth movement. The motor is now in programming mode for a "
            "few seconds.</li>";
    html += "<li>Within that window, hold this device's panel-mount button until "
            "the status LED gives two quick blinks, at about three seconds, then "
            "release. Keeping it held past that leads to a factory reset.</li>";
    html += "<li>The awning should jog again, which means the new remote was "
            "registered.</li>";
    html += "</ol>";
    html += "<p>Somfy radio is one-way, so this device cannot detect whether the "
            "awning accepted it. The only proof is the awning moving on command. "
            "There are no awning controls on this page: tap the panel-mount "
            "button to send a stop, and try open and close from Google Home once "
            "the device is commissioned.</p>";

    if (commissioned) {
        html += "<h2>Matter</h2>";
        html += "<p>This device is commissioned. To share it with another "
                "ecosystem (Alexa, Apple Home, SmartThings) use multi-admin "
                "sharing from Google Home.</p>";
    } else {
        html += "<h2>Matter Pairing</h2>";
        html += "<p>Add this device in Google Home as a Matter device, then "
                "scan the QR code or enter the manual code. Accept the "
                "\"uncertified device\" warning; that is expected here.</p>";
        html += "<table>";
        html += "<tr><th>Manual code</th><td>" + Matter.getManualPairingCode() + "</td></tr>";
        html += "<tr><th>QR code</th><td><a href='" + Matter.getOnboardingQRCodeUrl() + "'>Open QR code</a></td></tr>";
        html += "</table>";
    }

    html += renderBootLog();

    // Wi-Fi change form. Submitting it stores new credentials and reboots onto
    // the new network, leaving Matter commissioning intact (unlike a factory
    // reset). The SSID field is prefilled with the current network.
    html += "<h2>Change Wi-Fi</h2>";
    html += "<p>Update the network this device joins. It will restart to apply "
            "the change, keeping its Matter pairing. If the new network cannot "
            "be reached (for example a mistyped password), it automatically "
            "returns to the current network, so a typo will not lock you out.</p>";
    html += "<form method='POST' action='/save'>";
    html += "<label for='ssid'>Network name (SSID)</label>";
    html += "<input id='ssid' name='ssid' autocomplete='off' value='" + store.getWiFiSsid() + "' required>";
    html += renderPasswordField("password", "Password");
    html += "<button type='submit'>Save and Restart</button></form>";

    html += "</body></html>";
    return html;
}

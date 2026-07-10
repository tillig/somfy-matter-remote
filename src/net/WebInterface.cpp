#include "WebInterface.h"

#include <DNSServer.h>
#include <Matter.h>
#include <WebServer.h>
#include <WiFi.h>

#include "../config.h"
#include "../matter/AwningCovering.h"
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

WebInterface::WebInterface(WiFiConnection& net, ConfigStore& store, AwningCovering& awning)
    : net(net), store(store), awning(awning) {}

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
        server.send(200, "text/html", renderSetupPage(""));
    } else {
        server.send(200, "text/html", renderDashboardPage());
    }
}

void WebInterface::handleSave() {
    const String ssid = server.arg("ssid");
    const String password = server.arg("password");

    if (ssid.length() == 0) {
        server.send(200, "text/html", renderSetupPage("SSID cannot be empty."));
        return;
    }

    if (net.getMode() == WiFiConnection::Mode::SetupAp) {
        // First-time setup: the phone is on our access point, which stays up
        // during the test, so we can validate the network live and only save on
        // success. A typo just shows an error and lets the user try again.
        if (net.testCredentials(ssid, password)) {
            store.setWiFiCredentials(ssid, password);
            server.send(200, "text/html", renderSetupPage("Connected to \"" + ssid + "\". Saving and restarting."));
            rebootRequested = true;
            rebootAt = millis() + REBOOT_DELAY_MS;
        } else {
            server.send(200,
                        "text/html",
                        renderSetupPage("Could not connect to \"" + ssid +
                                        "\". Check the network name and password, then try again."));
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
                                "border-radius:6px;background:#223;border:1px solid #456}</style>";

String WebInterface::renderSetupPage(const String& message) {
    String html = "<!DOCTYPE html><html><head><meta charset='utf-8'>";
    html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
    html += "<title>Awning Wi-Fi Setup</title>";
    html += PAGE_STYLE;
    html += "</head><body><h1>Somfy Awning Wi-Fi Setup</h1>";
    html += "<p>Enter the Wi-Fi network this device should join. After it "
            "connects, add it to Google Home as a Matter device.</p>";
    if (message.length() > 0) {
        html += "<p class='msg'>" + message + "</p>";
    }
    html += "<form method='POST' action='/save'>";
    html += "<label for='ssid'>Network name (SSID)</label>";
    html += "<input id='ssid' name='ssid' autocomplete='off' required>";
    html += "<label for='password'>Password</label>";
    html += "<input id='password' name='password' type='password' autocomplete='off'>";
    html += "<button type='submit'>Save and Restart</button></form></body></html>";
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

    html += "<table>";
    html += "<tr><th>Hostname</th><td>" + net.getHostname() + ".local</td></tr>";
    html += "<tr><th>IP address</th><td>" + net.getIP().toString() + "</td></tr>";
    html += "<tr><th>Wi-Fi</th><td>" + store.getWiFiSsid() + " (" + String(net.getRssi()) + " dBm)</td></tr>";
    html += "<tr><th>Matter</th><td>" + String(commissioned ? "Commissioned" : "Not commissioned") + "</td></tr>";
    html += "</table>";

    if (commissioned) {
        html += "<p>This device is commissioned. To share it with another "
                "ecosystem (Alexa, Apple Home, SmartThings) use multi-admin "
                "sharing from Google Home.</p>";
    } else {
        html += "<h2>Matter Pairing</h2>";
        html += "<p>Add this device in Google Home as a Matter device, then "
                "scan the QR code or enter the manual code.</p>";
        html += "<table>";
        html += "<tr><th>Manual code</th><td>" + Matter.getManualPairingCode() + "</td></tr>";
        html += "<tr><th>QR code</th><td><a href='" + Matter.getOnboardingQRCodeUrl() + "'>Open QR code</a></td></tr>";
        html += "</table>";
    }

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
    html += "<label for='password'>Password</label>";
    html += "<input id='password' name='password' type='password' autocomplete='off'>";
    html += "<button type='submit'>Save and Restart</button></form>";

    html += "</body></html>";
    return html;
}

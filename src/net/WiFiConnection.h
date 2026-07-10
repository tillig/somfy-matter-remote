#pragma once

#include <Arduino.h>
#include <IPAddress.h>

class ConfigStore;

// WiFiConnection owns Wi-Fi bring-up. This ESP32 Matter build has no over-BLE
// commissioning, so the device must join Wi-Fi itself before Google Home can
// commission it over the local network.
//
// On boot it chooses a mode: if station credentials are stored it connects as a
// Wi-Fi station; otherwise it hosts an open SoftAP so the setup portal can
// collect credentials. The web interface and Matter live in other layers; this
// class only manages the radio and reports state.
class WiFiConnection {
public:
    enum class Mode {
        Booting, // before begin() has run
        Station, // connected (or connecting) to the home network
        SetupAp, // hosting the setup access point, no credentials yet
    };

    explicit WiFiConnection(ConfigStore& store);

    // Decide the mode and bring up Wi-Fi. In Station mode this blocks up to
    // WIFI_CONNECT_TIMEOUT_MS waiting for a connection. Returns the resulting
    // mode.
    Mode begin();

    // Service reconnection while in Station mode. Call from loop(). No-op in
    // SetupAp mode.
    void loop();

    Mode getMode() const {
        return mode;
    }

    // True when in Station mode and currently associated with an IP address.
    bool isStationConnected() const;

    // The device's current address: the station IP in Station mode, or the
    // SoftAP gateway IP in SetupAp mode.
    IPAddress getIP() const;

    // Signal strength in dBm while connected as a station; 0 otherwise.
    int getRssi() const;

private:
    bool connectStation();
    void startSetupAp();

    ConfigStore& store;
    Mode mode = Mode::Booting;
    uint32_t lastReconnectAttempt = 0;
};

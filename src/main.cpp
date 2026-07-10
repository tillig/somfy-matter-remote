// Somfy Awning Matter Remote
//
// An ESP32 that presents itself to the home network as a Matter Window Covering
// and translates open/close/stop into Somfy RTS radio frames on 433.42 MHz via
// a CC1101 transceiver. See docs/architecture.md for the layer design.
//
// This ESP32 Matter build has no over-BLE commissioning, so the device must
// join Wi-Fi itself before Google Home can commission it. Boot therefore
// branches on stored credentials:
//   - No credentials: host the "Awning-Setup" access point and serve the
//     Wi-Fi setup portal. Matter does not start; the device reboots once
//     credentials are saved.
//   - Credentials present: join Wi-Fi, then bring up radio -> Matter, and
//     serve the diagnostics/pairing dashboard. loop() services Matter, the
//     web interface, the serial command interface, and the button and LED.

#include <Arduino.h>

#include "config.h"
#include "matter/AwningCovering.h"
#include "net/WiFiConnection.h"
#include "net/WebInterface.h"
#include "rf/SomfyController.h"
#include "storage/ConfigStore.h"

static ConfigStore configStore;
static SomfyController somfy;
static AwningCovering awning(somfy, configStore);
static WiFiConnection network(configStore);
static WebInterface web(network, configStore, awning);

// True when the device booted without Wi-Fi credentials and is running the
// setup portal instead of the normal Matter runtime.
static bool setupMode = false;

// --- Status LED --------------------------------------------------------------
// Non-blocking blink patterns so loop() never stalls (Matter must keep running).

namespace {

struct BlinkPattern {
    uint16_t onMs;
    uint16_t offMs;
};

BlinkPattern activePattern = {0, 0};
uint8_t blinksRemaining = 0;
bool ledOn = false;
uint32_t ledPhaseStartedAt = 0;

void startBlink(uint16_t onMs, uint16_t offMs, uint8_t count) {
    activePattern = {onMs, offMs};
    blinksRemaining = count;
    ledOn = true;
    digitalWrite(STATUS_LED_GPIO, HIGH);
    ledPhaseStartedAt = millis();
}

void serviceLed() {
    if (blinksRemaining == 0 && !ledOn) {
        return;
    }
    const uint32_t now = millis();
    const uint16_t phaseLen = ledOn ? activePattern.onMs : activePattern.offMs;
    if (now - ledPhaseStartedAt < phaseLen) {
        return;
    }
    ledPhaseStartedAt = now;
    if (ledOn) {
        // Finished an on-phase; drop low and count the completed blink.
        ledOn = false;
        digitalWrite(STATUS_LED_GPIO, LOW);
        if (blinksRemaining > 0) {
            blinksRemaining--;
        }
    } else if (blinksRemaining > 0) {
        // Start the next on-phase.
        ledOn = true;
        digitalWrite(STATUS_LED_GPIO, HIGH);
    }
}

// Blink vocabulary, chosen to be distinguishable when the device is sealed in
// an enclosure with no serial monitor attached.
void blinkAck() {
    startBlink(120, 0, 1); // single short blink: press acknowledged
}
void blinkPairing() {
    startBlink(150, 150, 6); // rapid flurry: Prog / add-a-remote sent
}
void blinkReset() {
    startBlink(600, 300, 4); // slow heavy pattern: factory reset
}

// --- Pairing button ----------------------------------------------------------
// One button, debounced, with the action chosen by hold duration on release.

bool buttonWasDown = false;
uint32_t buttonDownAt = 0;
uint32_t lastButtonEdgeAt = 0;
bool longPressFired = false;

void handleShortPress() {
    Serial.println("[button] Short press -> My (stop).");
    somfy.stop();
    blinkAck();
}

void handleMediumPress() {
    Serial.println("[button] Medium press -> Prog (enter add-a-remote mode).");
    somfy.pair();
    blinkPairing();
}

void handleLongPress() {
    Serial.println("[button] Long press -> factory reset (Matter + Wi-Fi credentials).");
    blinkReset();
    // Clear the stored Wi-Fi credentials so the device reopens the setup access
    // point on the next boot, then decommission Matter. decommission() may
    // restart the device, so clear credentials first.
    configStore.clearWiFiCredentials();
    awning.decommission();
}

void serviceButton() {
    const bool down = digitalRead(PAIR_BUTTON_GPIO) == LOW; // active-low with pull-up
    const uint32_t now = millis();

    if (down != buttonWasDown) {
        // Debounce state changes.
        if (now - lastButtonEdgeAt < BUTTON_DEBOUNCE_MS) {
            return;
        }
        lastButtonEdgeAt = now;

        if (down) {
            buttonWasDown = true;
            buttonDownAt = now;
            longPressFired = false;
        } else {
            buttonWasDown = false;
            if (longPressFired) {
                return; // long-press action already fired while held
            }
            const uint32_t held = now - buttonDownAt;
            if (held < PRESS_SHORT_MAX_MS) {
                handleShortPress();
            } else if (held < PRESS_MEDIUM_MAX_MS) {
                handleMediumPress();
            } else {
                handleLongPress();
            }
        }
        return;
    }

    // Fire the factory reset as soon as the long-press threshold is reached,
    // rather than waiting for release, so the user gets immediate feedback.
    if (down && !longPressFired && (now - buttonDownAt) >= PRESS_LONG_MS) {
        longPressFired = true;
        handleLongPress();
    }
}

// --- Serial command interface ------------------------------------------------
// Type a command at 115200 baud. The radio commands (Up, Down, My, Prog) mirror
// the Somfy library examples and are invaluable for radio bring-up before
// Matter. The `help` and `status` commands aid diagnostics, especially when the
// device is on the bench during hardware bring-up.

const char* wifiModeName(WiFiConnection::Mode mode) {
    switch (mode) {
        case WiFiConnection::Mode::Station:
            return "station";
        case WiFiConnection::Mode::SetupAp:
            return "setup access point";
        default:
            return "booting";
    }
}

void printHelp() {
    Serial.println(F("[serial] Commands:"));
    Serial.println(F("  Up      - retract the awning (Somfy Up)"));
    Serial.println(F("  Down    - extend the awning (Somfy Down)"));
    Serial.println(F("  My      - stop / favorite position (Somfy My)"));
    Serial.println(F("  Prog    - enter add-a-remote pairing mode (Somfy Prog)"));
    Serial.println(F("  status  - print radio, network, and Matter state"));
    Serial.println(F("  help    - show this list"));
}

void printStatus() {
    Serial.println(F("[status] Somfy Awning Matter Remote"));

    // Radio.
    Serial.printf(
        "  Radio:      %s at %.2f MHz\n", somfy.isRadioReady() ? "CC1101 ready" : "CC1101 NOT detected", FREQUENCY_MHZ);
    Serial.printf("  Remote ID:  0x%06X\n", REMOTE_ID);
    const uint16_t code = somfy.peekRollingCode();
    if (code == 0) {
        Serial.println(F("  Rolling code: not yet sent (no commands transmitted)"));
    } else {
        Serial.printf("  Rolling code: %u (next to send)\n", code);
    }

    // Network.
    Serial.printf("  Wi-Fi mode: %s\n", wifiModeName(network.getMode()));
    if (network.getMode() == WiFiConnection::Mode::SetupAp) {
        Serial.printf("  Setup SSID: %s\n", network.getSetupApSsid().c_str());
        Serial.printf("  Setup IP:   %s\n", network.getIP().toString().c_str());
    } else if (network.isStationConnected()) {
        Serial.printf("  Connected:  yes (%d dBm)\n", network.getRssi());
        Serial.printf("  Hostname:   %s.local\n", network.getHostname().c_str());
        Serial.printf("  IP address: %s\n", network.getIP().toString().c_str());
    } else {
        Serial.println(F("  Connected:  no (retrying)"));
    }

    // Matter.
    Serial.printf("  Matter:     %s\n", awning.isCommissioned() ? "commissioned" : "not commissioned");
    Serial.printf("  Direction:  INVERT_DIRECTION=%d\n", INVERT_DIRECTION);
}

// Recognize only the radio commands we intend to support. getSomfyCommand()
// defaults unknown input to Command::My, which would silently transmit a stop;
// gating on this table prevents a typo (or `help`) from keying the radio.
bool isKnownRadioCommand(const String& token) {
    return token.equalsIgnoreCase("Up") || token.equalsIgnoreCase("Down") || token.equalsIgnoreCase("My") ||
           token.equalsIgnoreCase("Prog");
}

void serviceSerial() {
    if (Serial.available() <= 0) {
        return;
    }
    String line = Serial.readStringUntil('\n');
    line.trim(); // drop CR and stray whitespace from terminals
    if (line.length() == 0) {
        return;
    }

    if (line.equalsIgnoreCase("help") || line == "?") {
        printHelp();
        return;
    }
    if (line.equalsIgnoreCase("status")) {
        printStatus();
        return;
    }
    if (!isKnownRadioCommand(line)) {
        Serial.print(F("[serial] Unknown command: "));
        Serial.println(line);
        Serial.println(F("[serial] Type 'help' for the command list."));
        return;
    }

    Serial.print(F("[serial] Sending Somfy command: "));
    Serial.println(line);
    somfy.send(getSomfyCommand(line));
}

} // namespace

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println();
    Serial.println("[boot] Somfy Awning Matter Remote starting.");

    pinMode(STATUS_LED_GPIO, OUTPUT);
    digitalWrite(STATUS_LED_GPIO, LOW);
    pinMode(PAIR_BUTTON_GPIO, INPUT_PULLUP);

    configStore.begin();

    // Bring up Wi-Fi first. Without stored credentials this enters SoftAP setup
    // mode, in which Matter and the radio are left down until the user has
    // provided a network and the device reboots.
    if (network.begin() == WiFiConnection::Mode::SetupAp) {
        setupMode = true;
        web.begin();
        Serial.println("[boot] Setup mode: join the access point and open the portal to configure Wi-Fi.");
        return;
    }

    if (somfy.begin()) {
        Serial.println("[boot] CC1101 initialized at 433.42 MHz (OOK).");
    } else {
        Serial.println("[boot] WARNING: CC1101 not detected. Check SPI wiring and 3V3 power.");
    }

    awning.begin();
    web.begin();

    Serial.println("[boot] Ready. Type 'help' for serial commands.");
}

void loop() {
    // In setup mode only the web portal runs, until credentials are saved and
    // the device reboots into station mode.
    if (setupMode) {
        web.loop();
        serviceLed();
        return;
    }

    network.loop();
    awning.loop();
    web.loop();
    serviceSerial();
    serviceButton();
    serviceLed();
}

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
#include "diag/BootLog.h"
#include "matter/AwningCovering.h"
#include "net/WiFiConnection.h"
#include "net/WebInterface.h"
#include "rf/SomfyController.h"
#include "storage/ConfigStore.h"

BootLog bootLog;

static ConfigStore configStore;
static SomfyController somfy;
static AwningCovering awning(somfy, configStore);
static WiFiConnection network(configStore);
static WebInterface web(network, configStore, awning, somfy, bootLog);

// True when the device booted without Wi-Fi credentials and is running the
// setup portal instead of the normal Matter runtime.
static bool setupMode = false;

// --- Status LED --------------------------------------------------------------
// The LED is the only feedback channel once the device is sealed in an
// enclosure, so it reports device state as a repeating count of short pulses:
// the number of pulses is the state code. Counting pulses separated by a clear
// gap reads far more reliably than judging blink rates.
//
// Codes are ordered most-blocking first, so the LED always shows the thing to
// fix next. A transient acknowledgment (button press, pairing sent, factory
// reset) briefly overrides the idle code and then falls back to it.
//
// Everything here is non-blocking so loop() never stalls; Matter must keep
// running.

namespace {

enum class StatusCode : uint8_t {
    Booting = 0,         // solid on, no count yet
    Ready = 1,           // Wi-Fi up, Matter commissioned, radio present
    NeedsWiFiSetup = 2,  // hosting the setup portal, waiting for credentials
    WiFiFailing = 3,     // credentials stored but not connecting
    NotCommissioned = 4, // on Wi-Fi, but no Matter fabric yet
    RadioFault = 5,      // CC1101 did not respond over SPI
};

// Plain-language description of a status code, used by the serial status output
// so the LED can be correlated with what it means.
const char* statusCodeDescription(StatusCode code) {
    switch (code) {
        case StatusCode::Ready:
            return "ready";
        case StatusCode::NeedsWiFiSetup:
            return "waiting for Wi-Fi setup";
        case StatusCode::WiFiFailing:
            return "cannot reach Wi-Fi";
        case StatusCode::NotCommissioned:
            return "not yet added to Google Home";
        case StatusCode::RadioFault:
            return "radio not detected";
        default:
            return "starting up";
    }
}

// Idle indication state.
StatusCode currentCode = StatusCode::Booting;
uint8_t pulsesLeftInCycle = 0;
bool ledOn = false;
uint32_t ledPhaseStartedAt = 0;
bool inCodeGap = false;

// Transient acknowledgment state. While active it takes over the LED.
uint8_t ackPulsesLeft = 0;
uint16_t ackOnMs = 0;
uint16_t ackOffMs = 0;

void ledWrite(bool on) {
    ledOn = on;
    digitalWrite(STATUS_LED_GPIO, on ? HIGH : LOW);
}

// Show a short acknowledgment pattern, then resume the idle code.
void blinkAck(uint8_t count, uint16_t onMs, uint16_t offMs) {
    ackPulsesLeft = count;
    ackOnMs = onMs;
    ackOffMs = offMs;
    inCodeGap = false;
    ledWrite(true);
    ledPhaseStartedAt = millis();
}

// The LED cues the button rather than only reporting after the fact. Each
// pattern is short enough to finish well before the next threshold, so a cue is
// never still playing when the following one is due; that is what previously
// made the pairing hold unusable, because waiting out a six-blink flurry ran
// straight into the factory-reset threshold.
void blinkPressAck() {
    blinkAck(1, 120, 120); // press seen: LED drops out and comes back once
}
void blinkThresholdReached() {
    blinkAck(2, 90, 90); // quick double blink: let go now to take this action
}
void blinkFactoryReset() {
    blinkAck(4, 600, 300); // slow heavy pattern: factory reset has fired
}

void serviceLed() {
    const uint32_t now = millis();

    // Acknowledgments take precedence over the idle code.
    if (ackPulsesLeft > 0) {
        const uint16_t phaseLen = ledOn ? ackOnMs : ackOffMs;
        if (now - ledPhaseStartedAt < phaseLen) {
            return;
        }
        ledPhaseStartedAt = now;
        if (ledOn) {
            ledWrite(false);
            ackPulsesLeft--;
            if (ackPulsesLeft == 0) {
                // Restart the idle cycle cleanly on the next pass.
                pulsesLeftInCycle = 0;
                inCodeGap = true;
            }
        } else {
            ledWrite(true);
        }
        return;
    }

    // Booting is shown as a steady light rather than a count.
    if (currentCode == StatusCode::Booting) {
        if (!ledOn) {
            ledWrite(true);
        }
        return;
    }

    const uint8_t codeCount = static_cast<uint8_t>(currentCode);

    if (inCodeGap) {
        if (now - ledPhaseStartedAt < LED_CODE_GAP_MS) {
            return;
        }
        inCodeGap = false;
        pulsesLeftInCycle = codeCount;
        ledPhaseStartedAt = now;
        ledWrite(true);
        return;
    }

    if (pulsesLeftInCycle == 0) {
        // Nothing pending; open a gap before the next repetition.
        inCodeGap = true;
        ledPhaseStartedAt = now;
        ledWrite(false);
        return;
    }

    const uint16_t phaseLen = ledOn ? LED_PULSE_ON_MS : LED_PULSE_OFF_MS;
    if (now - ledPhaseStartedAt < phaseLen) {
        return;
    }
    ledPhaseStartedAt = now;
    if (ledOn) {
        ledWrite(false);
        pulsesLeftInCycle--;
    } else {
        ledWrite(true);
    }
}

// Recompute the idle status code from live device state. Ordered most-blocking
// first so the LED always indicates the next thing to fix.
StatusCode computeStatusCode() {
    if (!somfy.isRadioReady()) {
        return StatusCode::RadioFault;
    }
    if (network.getMode() == WiFiConnection::Mode::SetupAp) {
        return StatusCode::NeedsWiFiSetup;
    }
    if (!network.isStationConnected()) {
        return StatusCode::WiFiFailing;
    }
    if (!awning.isCommissioned()) {
        return StatusCode::NotCommissioned;
    }
    return StatusCode::Ready;
}

void updateStatusCode() {
    const StatusCode next = computeStatusCode();
    if (next == currentCode) {
        return;
    }
    currentCode = next;
    // Restart the indication so the new code is shown from the start of a cycle
    // rather than mid-count.
    pulsesLeftInCycle = 0;
    inCodeGap = true;
    ledPhaseStartedAt = millis();
    ledWrite(false);
}

// --- Pairing button ----------------------------------------------------------
// One button, debounced, with the action chosen by how long it is held. The LED
// announces each threshold as it is crossed, so the button is released on a cue
// instead of the user having to estimate three seconds in their head.

bool buttonWasDown = false;
uint32_t buttonDownAt = 0;
uint32_t lastButtonEdgeAt = 0;
bool pairThresholdAnnounced = false;
bool resetFired = false;

void handleStop() {
    Serial.println("[button] Released before the pairing threshold -> stop the awning.");
    somfy.stop();
    // No extra pattern here: the press-seen blink already fired on the way down,
    // and a stop is confirmed by the awning itself.
}

void handlePair() {
    Serial.println("[button] Released after the pairing threshold -> send pairing command.");
    somfy.pair();
}

void handleFactoryReset() {
    Serial.println("[button] Held to the reset threshold -> factory reset (clears Wi-Fi and Matter pairing).");
    blinkFactoryReset();
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
            pairThresholdAnnounced = false;
            resetFired = false;
            // Confirm the press immediately, so a press that does nothing is
            // distinguishable from a button or wiring fault.
            blinkPressAck();
        } else {
            buttonWasDown = false;
            if (resetFired) {
                return; // the reset already ran while the button was held
            }
            // Every release maps to an action, chosen by whether the pairing cue
            // had already been shown. Releasing on that cue is the pairing path;
            // releasing before it is a stop.
            if (pairThresholdAnnounced) {
                handlePair();
            } else {
                handleStop();
            }
        }
        return;
    }

    if (!down) {
        return;
    }

    const uint32_t held = now - buttonDownAt;

    // Cue the pairing threshold so the button can be released on the signal.
    if (!pairThresholdAnnounced && held >= PRESS_PAIR_MS) {
        pairThresholdAnnounced = true;
        blinkThresholdReached();
        return;
    }

    // Fire the factory reset on reaching the threshold rather than on release,
    // so continuing to hold cannot be mistaken for a further action.
    if (!resetFired && held >= PRESS_RESET_MS) {
        resetFired = true;
        handleFactoryReset();
    }
}

// --- Serial command interface ------------------------------------------------
// Type a command at 115200 baud. Commands are named for what they do (open,
// close, stop, pair); the Somfy button names (Up, Down, My, Prog) are accepted
// as aliases because the Somfy documentation and library use them. The help and
// status commands aid diagnostics during hardware bring-up.

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
    Serial.println(F("[serial] Commands (raw radio, named for the physical motion):"));
    Serial.println(F("  retract - roll the awning up and away  (Somfy Up)"));
    Serial.println(F("  extend  - unroll the awning for shade  (Somfy Down)"));
    Serial.println(F("  stop    - stop the awning              (Somfy My)"));
    Serial.println(F("  pair    - send the pairing command     (Somfy Prog)"));
    Serial.println(F("  status  - print radio, network, and Matter state"));
    Serial.println(F("  log     - replay the startup log"));
    Serial.println(F("  help    - show this list"));
    // These name the motor direction rather than Matter's open/close, because
    // INVERT_DIRECTION decides which way round those two words map and these
    // commands go straight to the radio without consulting it.
    Serial.println(F("[serial] 'open' and 'close' are accepted in the Matter sense for this build."));
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
    Serial.printf("  Open means: %s\n",
                  INVERT_DIRECTION ? "extend the awning for shade (awning sense)"
                                   : "retract the awning (literal Matter sense)");
    Serial.printf(
        "  LED code:   %u pulse(s) - %s\n", static_cast<uint8_t>(currentCode), statusCodeDescription(currentCode));
}

// Resolve a radio command by intent name, accepting the Somfy button names as
// aliases. Returns false for anything unrecognized: getSomfyCommand() defaults
// unknown input to Command::My, so passing input straight through would let a
// typo (or `help`) silently transmit a stop.
bool resolveRadioCommand(const String& token, Command& command, const char*& description) {
    if (token.equalsIgnoreCase("retract") || token.equalsIgnoreCase("up")) {
        command = Command::Up;
        description = "retract (roll up and away)";
        return true;
    }
    if (token.equalsIgnoreCase("extend") || token.equalsIgnoreCase("down")) {
        command = Command::Down;
        description = "extend (unroll for shade)";
        return true;
    }
    // "open" and "close" follow INVERT_DIRECTION so the serial interface agrees
    // with the controller: whatever Google Home calls open, these do too.
    if (token.equalsIgnoreCase("open")) {
        command = INVERT_DIRECTION ? Command::Down : Command::Up;
        description = INVERT_DIRECTION ? "open (extend for shade)" : "open (retract)";
        return true;
    }
    if (token.equalsIgnoreCase("close")) {
        command = INVERT_DIRECTION ? Command::Up : Command::Down;
        description = INVERT_DIRECTION ? "close (retract and put away)" : "close (extend)";
        return true;
    }
    if (token.equalsIgnoreCase("stop") || token.equalsIgnoreCase("my")) {
        command = Command::My;
        description = "stop";
        return true;
    }
    if (token.equalsIgnoreCase("pair") || token.equalsIgnoreCase("prog")) {
        command = Command::Prog;
        description = "pair (add this remote to the awning)";
        return true;
    }
    return false;
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
    if (line.equalsIgnoreCase("log")) {
        bootLog.print();
        return;
    }

    Command command = Command::My;
    const char* description = "";
    if (!resolveRadioCommand(line, command, description)) {
        Serial.print(F("[serial] Unknown command: "));
        Serial.println(line);
        Serial.println(F("[serial] Type 'help' for the command list."));
        return;
    }

    Serial.print(F("[serial] Sending: "));
    Serial.println(description);
    somfy.send(command);
}

} // namespace

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println();

    pinMode(STATUS_LED_GPIO, OUTPUT);
    pinMode(PAIR_BUTTON_GPIO, INPUT_PULLUP);
    // Solid light while starting up.
    currentCode = StatusCode::Booting;
    ledWrite(true);

    bootLog.addf("[boot] Somfy Awning Matter Remote starting.");

    configStore.begin();

    // Initialize the radio before the network. It does not depend on Wi-Fi, and
    // doing it here means a wiring fault is reported even when the device comes
    // up in Wi-Fi setup mode.
    if (somfy.begin()) {
        bootLog.addf("[boot] Radio ready: CC1101 detected, %.2f MHz OOK.", FREQUENCY_MHZ);
    } else {
        bootLog.addf("[boot] PROBLEM: CC1101 not detected. Check the SPI wiring and that VCC is on 3V3.");
    }

    // Without stored credentials this enters SoftAP setup mode, in which Matter
    // is left down until the user provides a network and the device reboots.
    if (network.begin() == WiFiConnection::Mode::SetupAp) {
        setupMode = true;
        web.begin();
        bootLog.addf("[boot] Wi-Fi setup needed. Join \"%s\" and open http://%s",
                     network.getSetupApSsid().c_str(),
                     network.getIP().toString().c_str());
        updateStatusCode();
        return;
    }

    awning.begin();
    web.begin();

    bootLog.addf("[boot] Ready. Type 'help' for serial commands.");
    updateStatusCode();
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
    updateStatusCode();
    serviceSerial();
    serviceButton();
    serviceLed();
}

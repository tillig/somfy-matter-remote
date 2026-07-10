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
// Mirrors the Somfy library examples: type Up, Down, My, or Prog at 115200 baud
// to transmit that command. Invaluable for radio bring-up before Matter.

void serviceSerial() {
    if (Serial.available() <= 0) {
        return;
    }
    const String line = Serial.readStringUntil('\n');
    const Command command = getSomfyCommand(line);
    Serial.print("[serial] Sending Somfy command from input: ");
    Serial.println(line);
    somfy.send(command);
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

    Serial.println("[boot] Ready. Serial commands: Up, Down, My, Prog.");
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

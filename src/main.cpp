// Somfy Awning Matter Remote
//
// An ESP32 that presents itself to the home network as a Matter Window Covering
// and translates open/close/stop into Somfy RTS radio frames on 433.42 MHz via
// a CC1101 transceiver. See docs/architecture.md for the layer design.
//
// Boot sequence: storage -> radio -> Matter, then loop() services Matter, the
// serial command interface, and the panel-mount pairing button + status LED.

#include <Arduino.h>

#include "config.h"
#include "matter/AwningCovering.h"
#include "rf/SomfyController.h"
#include "storage/ConfigStore.h"

static ConfigStore configStore;
static SomfyController somfy;
static AwningCovering awning(somfy, configStore);

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
    Serial.println("[button] Long press -> Matter factory reset.");
    blinkReset();
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

    if (somfy.begin()) {
        Serial.println("[boot] CC1101 initialized at 433.42 MHz (OOK).");
    } else {
        Serial.println("[boot] WARNING: CC1101 not detected. Check SPI wiring and 3V3 power.");
    }

    awning.begin();

    Serial.println("[boot] Ready. Serial commands: Up, Down, My, Prog.");
}

void loop() {
    awning.loop();
    serviceSerial();
    serviceButton();
    serviceLed();
}

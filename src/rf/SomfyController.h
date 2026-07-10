#pragma once

#include <SomfyRemote.h>

// SomfyController is the radio layer. It owns the CC1101 transceiver and the
// SomfyRemote frame generator, and exposes a small intent-based API (retract,
// extend, stop, pair). It never knows anything about Matter; the presentation
// layer translates commands and calls into this class.
//
// The rolling code is supplied by an NVS-backed store so it survives reboots,
// which is mandatory: the motor rejects stale or repeated codes.
class SomfyController {
public:
    SomfyController();

    // Initialize the CC1101 (SPI, GDO0, OOK modulation, 433.42 MHz) and the
    // Somfy emitter. Returns false if the CC1101 does not respond over SPI,
    // which almost always means a wiring fault.
    bool begin();

    // True once begin() has confirmed the CC1101 is present and initialized.
    bool isRadioReady() const {
        return radioReady;
    }

    // Awning motion, named by physical effect rather than by button.
    void retract(); // awning goes in  (Somfy Up)
    void extend();  // awning comes out (Somfy Down)
    void stop();    // stop / My favorite position (Somfy My)
    void pair();    // enter add-a-remote mode (Somfy Prog)

    // Send a raw Somfy command. Used by the serial command interface so bench
    // testing mirrors the Somfy library examples exactly.
    void send(Command command);

private:
    bool radioReady = false;
};

#pragma once

#include <Arduino.h>

// BootLog keeps a small in-memory copy of the startup and status messages that
// are also written to the serial monitor, so the device can be verified without
// a serial connection. The web dashboard renders it, and the serial `log`
// command replays it.
//
// The buffer is a fixed-size ring so it cannot grow without bound; once full,
// the oldest line is dropped. Lines are truncated rather than wrapped.
class BootLog {
public:
    static constexpr uint8_t MAX_LINES = 24;
    static constexpr uint8_t MAX_LINE_LENGTH = 96;

    // Record a line. Also echoes it to Serial with the given tag, so callers
    // have one call site per message instead of logging twice.
    void addf(const char* format, ...) __attribute__((format(printf, 2, 3)));

    // Replay the buffer to Serial.
    void print() const;

    uint8_t count() const;
    // Oldest-first access, for rendering. Returns an empty string if out of
    // range.
    String line(uint8_t index) const;

private:
    String lines[MAX_LINES];
    uint8_t head = 0;   // next slot to write
    uint8_t stored = 0; // number of valid entries, up to MAX_LINES
};

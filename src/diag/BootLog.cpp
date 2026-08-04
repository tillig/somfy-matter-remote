#include "BootLog.h"

#include <stdarg.h>

void BootLog::addf(const char* format, ...) {
    char buffer[MAX_LINE_LENGTH];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    // Echo to serial so there is a single call site per message.
    Serial.println(buffer);

    lines[head] = String(buffer);
    head = (head + 1) % MAX_LINES;
    if (stored < MAX_LINES) {
        stored++;
    }
}

void BootLog::print() const {
    for (uint8_t i = 0; i < stored; i++) {
        Serial.println(line(i));
    }
}

uint8_t BootLog::count() const {
    return stored;
}

String BootLog::line(uint8_t index) const {
    if (index >= stored) {
        return String();
    }
    // Oldest entry first. Once the ring has wrapped, the oldest sits at head.
    const uint8_t start = (stored == MAX_LINES) ? head : 0;
    return lines[(start + index) % MAX_LINES];
}

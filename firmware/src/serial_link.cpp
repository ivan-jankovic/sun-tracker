#include "serial_link.h"

#include <Arduino.h>

void SerialLink::begin() { Serial.begin(config::SERIAL_BAUD); }

void SerialLink::write_line(const char* line) {
    Serial.print(line);
    Serial.print('\n');
}

void SerialLink::log(const char* text) {
    Serial.print("# ");
    write_line(text);
}

bool SerialLink::read_line(std::string_view& line) {
    if (ready_) {  // the previous line has been handled
        ready_ = false;
        len_ = 0;
    }
    while (Serial.available() > 0) {
        const char c = static_cast<char>(Serial.read());
        if (c == '\r' || c == '\n') {
            if (len_ == 0) continue;
            ready_ = true;
            line = std::string_view(buf_, len_);
            return true;
        }
        if (len_ < config::COMMAND_MAX_CHARS + 1) buf_[len_++] = c;  // the rest is dropped
    }
    return false;
}

// The USB serial link: LineSink for the core's output, and a reader that turns
// incoming bytes into lines without blocking.
#pragma once

#include <cstddef>
#include <string_view>

#include "config.h"
#include "ports.h"

class SerialLink : public core::LineSink {
public:
    void begin();
    void write_line(const char* line) override;
    void log(const char* text);  // a '#' line, ignored by the ground station

    // Reads what has arrived; true when a complete line is in `line`. A line end is
    // CR or LF, and empty lines are skipped, so CRLF counts once. A line too long
    // for any command is passed on cut to one character over the limit, so the
    // parser refuses it as unknown.
    bool read_line(std::string_view& line);

private:
    char buf_[config::COMMAND_MAX_CHARS + 2] = {};
    size_t len_ = 0;
    bool ready_ = false;
};

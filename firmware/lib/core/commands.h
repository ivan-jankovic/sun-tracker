// Telecommand parsing: one text line in, one typed command (or a refusal) out.
// Whether a command is allowed in the current mode is decided elsewhere.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "config.h"
#include "faults.h"

namespace core {

enum class CommandType : uint8_t { Ping, Safe, Search, Clear, Inject, SimSun, Cal };

struct Command {
    CommandType type = CommandType::Ping;
    Injection channel = Injection::None;  // INJECT only
    float sun_deg = 0.0f;                 // SIM SUN only
};

// Unknown: not a command (NACK unknown). BadArg: a known command with a missing,
// extra or invalid argument (NACK bad_arg).
enum class ParseStatus : uint8_t { Ok, Unknown, BadArg };

struct ParseResult {
    ParseStatus status = ParseStatus::Unknown;
    Command command;
};

struct CommandParams {
    size_t max_chars = config::COMMAND_MAX_CHARS;
    float sim_sun_min_deg = config::SIM_SUN_MIN_DEG;
    float sim_sun_max_deg = config::SIM_SUN_MAX_DEG;
};

// Words are upper case and separated by spaces. Leading and trailing whitespace,
// including the CR of a CRLF line end, is ignored.
ParseResult parse_command(std::string_view line, const CommandParams& params = {});

// Makes a received line safe to echo in an ACK or NACK field: trims whitespace,
// replaces commas and unprintable characters with '_', and truncates to fit `out`.
// Always writes a terminating zero; returns the length written.
size_t sanitize_echo(std::string_view line, char* out, size_t out_size);

}  // namespace core

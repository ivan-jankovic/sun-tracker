#include "commands.h"

#include <cmath>
#include <cstdlib>

namespace core {

namespace {

constexpr size_t MAX_WORDS = 4;  // one more than the longest command, to catch extras

bool is_space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

std::string_view trim(std::string_view s) {
    while (!s.empty() && is_space(s.front())) s.remove_prefix(1);
    while (!s.empty() && is_space(s.back())) s.remove_suffix(1);
    return s;
}

// Splits on spaces; returns the number of words, at most MAX_WORDS.
size_t split(std::string_view s, std::string_view (&words)[MAX_WORDS]) {
    size_t n = 0;
    while (!s.empty() && n < MAX_WORDS) {
        const size_t end = s.find(' ');
        const std::string_view word = s.substr(0, end);
        if (!word.empty()) words[n++] = word;
        if (end == std::string_view::npos) break;
        s.remove_prefix(end + 1);
    }
    return n;
}

// Accepts only [+-]digits[.digits], so "1e3", "0x10", "nan" and "inf" are refused.
bool parse_deg(std::string_view word, float& out) {
    constexpr size_t MAX_NUMBER_CHARS = 15;
    if (word.empty() || word.size() > MAX_NUMBER_CHARS) return false;
    size_t i = (word[0] == '+' || word[0] == '-') ? 1 : 0;
    size_t digits = 0, fraction_digits = 0;
    bool point = false;
    for (; i < word.size(); ++i) {
        const char c = word[i];
        if (c >= '0' && c <= '9') {
            (point ? fraction_digits : digits)++;
        } else if (c == '.' && !point) {
            point = true;
        } else {
            return false;
        }
    }
    if (digits == 0 || (point && fraction_digits == 0)) return false;

    char buf[MAX_NUMBER_CHARS + 1];
    word.copy(buf, word.size());
    buf[word.size()] = '\0';
    out = std::strtof(buf, nullptr);
    return std::isfinite(out);
}

ParseResult ok(const Command& command) { return {ParseStatus::Ok, command}; }
ParseResult refused(ParseStatus status) { return {status, Command{}}; }

ParseResult no_args(CommandType type, size_t n_words) {
    if (n_words != 1) return refused(ParseStatus::BadArg);
    Command command;
    command.type = type;
    return ok(command);
}

}  // namespace

ParseResult parse_command(std::string_view line, const CommandParams& params) {
    line = trim(line);
    if (line.empty() || line.size() > params.max_chars) return refused(ParseStatus::Unknown);

    std::string_view w[MAX_WORDS];
    const size_t n = split(line, w);

    if (w[0] == "PING") return no_args(CommandType::Ping, n);
    if (w[0] == "SAFE") return no_args(CommandType::Safe, n);
    if (w[0] == "SEARCH") return no_args(CommandType::Search, n);
    if (w[0] == "CLEAR") return no_args(CommandType::Clear, n);
    if (w[0] == "CAL") return no_args(CommandType::Cal, n);

    if (w[0] == "INJECT") {
        if (n != 2) return refused(ParseStatus::BadArg);
        Command command;
        command.type = CommandType::Inject;
        if (w[1] == "L") {
            command.channel = Injection::L;
        } else if (w[1] == "R") {
            command.channel = Injection::R;
        } else {
            return refused(ParseStatus::BadArg);
        }
        return ok(command);
    }

    if (w[0] == "SIM" && n >= 2 && w[1] == "SUN") {
        Command command;
        command.type = CommandType::SimSun;
        if (n != 3 || !parse_deg(w[2], command.sun_deg)) return refused(ParseStatus::BadArg);
        // Written as "not inside" so that a NaN would also be refused.
        if (!(command.sun_deg >= params.sim_sun_min_deg &&
              command.sun_deg <= params.sim_sun_max_deg)) {
            return refused(ParseStatus::BadArg);
        }
        return ok(command);
    }

    return refused(ParseStatus::Unknown);
}

size_t sanitize_echo(std::string_view line, char* out, size_t out_size) {
    if (out == nullptr || out_size == 0) return 0;
    line = trim(line);
    size_t n = 0;
    for (const char c : line) {
        if (n + 1 >= out_size) break;
        const bool printable = c >= 0x20 && c <= 0x7e;
        out[n++] = (printable && c != ',') ? c : '_';
    }
    out[n] = '\0';
    return n;
}

}  // namespace core

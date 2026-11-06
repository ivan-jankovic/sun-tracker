#include <unity.h>

#include <cstring>
#include <string>

#include "commands.h"

using core::CommandParams;
using core::CommandType;
using core::Injection;
using core::parse_command;
using core::ParseResult;
using core::ParseStatus;
using core::sanitize_echo;

static const CommandParams cp;

void setUp() {}
void tearDown() {}

static void assert_status(ParseStatus expected, const ParseResult& r) {
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(expected), static_cast<uint8_t>(r.status));
}

static void assert_command(CommandType expected, const char* line) {
    const ParseResult r = parse_command(line);
    assert_status(ParseStatus::Ok, r);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(expected), static_cast<uint8_t>(r.command.type));
}

// --- commands without arguments ---

void test_commands_without_arguments_are_parsed() {
    assert_command(CommandType::Ping, "PING");
    assert_command(CommandType::Safe, "SAFE");
    assert_command(CommandType::Search, "SEARCH");
    assert_command(CommandType::Clear, "CLEAR");
    assert_command(CommandType::Cal, "CAL");
}

void test_crlf_line_end_and_surrounding_spaces_are_ignored() {
    assert_command(CommandType::Ping, "PING\r\n");
    assert_command(CommandType::Safe, "  SAFE \t");
}

void test_argument_on_a_command_without_arguments_is_bad_arg() {
    assert_status(ParseStatus::BadArg, parse_command("PING 1"));
    assert_status(ParseStatus::BadArg, parse_command("CAL NOW"));
}

// --- INJECT ---

void test_inject_left_and_right_are_parsed() {
    const ParseResult l = parse_command("INJECT L");
    assert_status(ParseStatus::Ok, l);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CommandType::Inject),
                            static_cast<uint8_t>(l.command.type));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Injection::L),
                            static_cast<uint8_t>(l.command.channel));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Injection::R),
                            static_cast<uint8_t>(parse_command("INJECT R").command.channel));
}

void test_inject_with_missing_wrong_or_extra_argument_is_bad_arg() {
    assert_status(ParseStatus::BadArg, parse_command("INJECT"));
    assert_status(ParseStatus::BadArg, parse_command("INJECT X"));
    assert_status(ParseStatus::BadArg, parse_command("INJECT l"));
    assert_status(ParseStatus::BadArg, parse_command("INJECT L R"));
}

void test_extra_spaces_between_words_are_accepted() {
    assert_status(ParseStatus::Ok, parse_command("INJECT   L"));
}

// --- SIM SUN ---

void test_sim_sun_parses_the_angle() {
    const ParseResult r = parse_command("SIM SUN 42.5");
    assert_status(ParseStatus::Ok, r);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(CommandType::SimSun),
                            static_cast<uint8_t>(r.command.type));
    TEST_ASSERT_EQUAL_FLOAT(42.5f, r.command.sun_deg);
}

void test_sim_sun_accepts_signed_angles() {
    TEST_ASSERT_EQUAL_FLOAT(-90.0f, parse_command("SIM SUN -90").command.sun_deg);
    TEST_ASSERT_EQUAL_FLOAT(10.0f, parse_command("SIM SUN +10").command.sun_deg);
}

void test_sim_sun_accepts_the_range_limits() {
    assert_status(ParseStatus::Ok, parse_command("SIM SUN -180"));
    assert_status(ParseStatus::Ok, parse_command("SIM SUN 180"));
}

void test_sim_sun_outside_the_range_is_bad_arg() {
    assert_status(ParseStatus::BadArg, parse_command("SIM SUN 180.1"));
    assert_status(ParseStatus::BadArg, parse_command("SIM SUN -181"));
}

void test_sim_sun_with_a_malformed_number_is_bad_arg() {
    const char* bad[] = {"SIM SUN abc", "SIM SUN 1e2", "SIM SUN 0x10", "SIM SUN nan",
                         "SIM SUN inf", "SIM SUN 4.",  "SIM SUN .5",   "SIM SUN 4..5",
                         "SIM SUN -",   "SIM SUN 9,5"};
    for (const char* line : bad) {
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(static_cast<uint8_t>(ParseStatus::BadArg),
                                        static_cast<uint8_t>(parse_command(line).status), line);
    }
}

void test_sim_sun_with_missing_or_extra_argument_is_bad_arg() {
    assert_status(ParseStatus::BadArg, parse_command("SIM SUN"));
    assert_status(ParseStatus::BadArg, parse_command("SIM SUN 10 20"));
}

void test_sim_without_sun_is_unknown() {
    assert_status(ParseStatus::Unknown, parse_command("SIM"));
    assert_status(ParseStatus::Unknown, parse_command("SIM MOON 10"));
}

// --- unknown lines ---

void test_unknown_word_is_unknown() {
    assert_status(ParseStatus::Unknown, parse_command("RESET"));
}

void test_commands_are_case_sensitive() {
    assert_status(ParseStatus::Unknown, parse_command("ping"));
}

void test_empty_and_blank_lines_are_unknown() {
    assert_status(ParseStatus::Unknown, parse_command(""));
    assert_status(ParseStatus::Unknown, parse_command(" \r\n"));
}

void test_line_longer_than_the_limit_is_unknown() {
    std::string line = "SIM SUN 1";
    line.append(cp.max_chars, '0');
    assert_status(ParseStatus::Unknown, parse_command(line));
}

void test_line_at_the_limit_is_parsed() {
    std::string line = "SIM SUN";
    line.append(cp.max_chars - line.size() - 1, ' ');  // extra spaces between words count
    line += "5";
    TEST_ASSERT_EQUAL_size_t(cp.max_chars, line.size());
    TEST_ASSERT_EQUAL_FLOAT(5.0f, parse_command(line).command.sun_deg);
}

// --- sanitize_echo ---

void test_echo_keeps_a_normal_command() {
    char out[40];
    TEST_ASSERT_EQUAL_size_t(10, sanitize_echo("SIM SUN 45\r\n", out, sizeof out));
    TEST_ASSERT_EQUAL_STRING("SIM SUN 45", out);
}

void test_echo_replaces_commas_and_unprintable_characters() {
    char out[40];
    sanitize_echo("A,B\tC\x01", out, sizeof out);
    TEST_ASSERT_EQUAL_STRING("A_B_C_", out);
}

void test_echo_is_truncated_to_the_buffer() {
    char out[5];
    TEST_ASSERT_EQUAL_size_t(4, sanitize_echo("SEARCH", out, sizeof out));
    TEST_ASSERT_EQUAL_STRING("SEAR", out);
}

void test_echo_into_a_zero_sized_buffer_writes_nothing() {
    char out[1] = {'x'};
    TEST_ASSERT_EQUAL_size_t(0, sanitize_echo("PING", out, 0));
    TEST_ASSERT_EQUAL_CHAR('x', out[0]);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_commands_without_arguments_are_parsed);
    RUN_TEST(test_crlf_line_end_and_surrounding_spaces_are_ignored);
    RUN_TEST(test_argument_on_a_command_without_arguments_is_bad_arg);
    RUN_TEST(test_inject_left_and_right_are_parsed);
    RUN_TEST(test_inject_with_missing_wrong_or_extra_argument_is_bad_arg);
    RUN_TEST(test_extra_spaces_between_words_are_accepted);
    RUN_TEST(test_sim_sun_parses_the_angle);
    RUN_TEST(test_sim_sun_accepts_signed_angles);
    RUN_TEST(test_sim_sun_accepts_the_range_limits);
    RUN_TEST(test_sim_sun_outside_the_range_is_bad_arg);
    RUN_TEST(test_sim_sun_with_a_malformed_number_is_bad_arg);
    RUN_TEST(test_sim_sun_with_missing_or_extra_argument_is_bad_arg);
    RUN_TEST(test_sim_without_sun_is_unknown);
    RUN_TEST(test_unknown_word_is_unknown);
    RUN_TEST(test_commands_are_case_sensitive);
    RUN_TEST(test_empty_and_blank_lines_are_unknown);
    RUN_TEST(test_line_longer_than_the_limit_is_unknown);
    RUN_TEST(test_line_at_the_limit_is_parsed);
    RUN_TEST(test_echo_keeps_a_normal_command);
    RUN_TEST(test_echo_replaces_commas_and_unprintable_characters);
    RUN_TEST(test_echo_is_truncated_to_the_buffer);
    RUN_TEST(test_echo_into_a_zero_sized_buffer_writes_nothing);
    return UNITY_END();
}

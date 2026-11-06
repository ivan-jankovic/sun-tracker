// The protocol strings are the specification, so these tests spell them out.
#include <unity.h>

#include <cstdint>
#include <string>

#include "protocol.h"

using namespace core;

void setUp() {}
void tearDown() {}

static char line[LINE_MAX_CHARS];

static Telemetry sample_tm() {
    Telemetry t;
    t.t_ms = 52100;
    t.mode = Mode::Tracking;
    t.l_mv = 2215;
    t.r_mv = 2180;
    t.err = 0.008f;
    t.angle_deg = 97.5f;
    return t;
}

static std::string tm_line(const Telemetry& t) {
    LineWriter w;
    w.tm(line, sizeof line, t);
    return line;
}

// --- names on the wire ---

void test_mode_names() {
    TEST_ASSERT_EQUAL_STRING("BOOT", mode_name(Mode::Boot));
    TEST_ASSERT_EQUAL_STRING("SAFE", mode_name(Mode::Safe));
    TEST_ASSERT_EQUAL_STRING("SEARCH", mode_name(Mode::Search));
    TEST_ASSERT_EQUAL_STRING("TRACKING", mode_name(Mode::Tracking));
    TEST_ASSERT_EQUAL_STRING("FAULT", mode_name(Mode::Fault));
}

void test_reason_names() {
    TEST_ASSERT_EQUAL_STRING("boot", reason_name(Reason::Boot));
    TEST_ASSERT_EQUAL_STRING("sun_found", reason_name(Reason::SunFound));
    TEST_ASSERT_EQUAL_STRING("sun_lost", reason_name(Reason::SunLost));
    TEST_ASSERT_EQUAL_STRING("no_sun", reason_name(Reason::NoSun));
    TEST_ASSERT_EQUAL_STRING("cmd_safe", reason_name(Reason::CmdSafe));
    TEST_ASSERT_EQUAL_STRING("cmd_search", reason_name(Reason::CmdSearch));
    TEST_ASSERT_EQUAL_STRING("cmd_clear", reason_name(Reason::CmdClear));
    TEST_ASSERT_EQUAL_STRING("fault", reason_name(Reason::Fault));
}

void test_fault_names() {
    TEST_ASSERT_EQUAL_STRING("-", fault_name(FaultCode::None));
    TEST_ASSERT_EQUAL_STRING("SELFTEST_SHORT", fault_name(FaultCode::SelftestShort));
    TEST_ASSERT_EQUAL_STRING("SENSOR_OPEN_L", fault_name(FaultCode::SensorOpenL));
    TEST_ASSERT_EQUAL_STRING("SENSOR_OPEN_R", fault_name(FaultCode::SensorOpenR));
    TEST_ASSERT_EQUAL_STRING("INJECTED_L", fault_name(FaultCode::InjectedL));
    TEST_ASSERT_EQUAL_STRING("INJECTED_R", fault_name(FaultCode::InjectedR));
}

void test_nack_names() {
    TEST_ASSERT_EQUAL_STRING("unknown", nack_name(NackReason::Unknown));
    TEST_ASSERT_EQUAL_STRING("bad_arg", nack_name(NackReason::BadArg));
    TEST_ASSERT_EQUAL_STRING("fault_latched", nack_name(NackReason::FaultLatched));
    TEST_ASSERT_EQUAL_STRING("not_in_fault", nack_name(NackReason::NotInFault));
    TEST_ASSERT_EQUAL_STRING("not_in_safe", nack_name(NackReason::NotInSafe));
    TEST_ASSERT_EQUAL_STRING("cal_out_of_range", nack_name(NackReason::CalOutOfRange));
    TEST_ASSERT_EQUAL_STRING("not_sim", nack_name(NackReason::NotSim));
    TEST_ASSERT_EQUAL_STRING("selftest_failed", nack_name(NackReason::SelftestFailed));
}

void test_parse_errors_map_to_nack_reasons() {
    TEST_ASSERT_EQUAL_STRING("unknown", nack_name(nack_reason(ParseStatus::Unknown)));
    TEST_ASSERT_EQUAL_STRING("bad_arg", nack_name(nack_reason(ParseStatus::BadArg)));
}

void test_refusals_map_to_nack_reasons() {
    TEST_ASSERT_EQUAL_STRING("fault_latched", nack_name(nack_reason(Refusal::FaultLatched)));
    TEST_ASSERT_EQUAL_STRING("not_in_fault", nack_name(nack_reason(Refusal::NotInFault)));
    TEST_ASSERT_EQUAL_STRING("not_in_safe", nack_name(nack_reason(Refusal::NotInSafe)));
    TEST_ASSERT_EQUAL_STRING("selftest_failed", nack_name(nack_reason(Refusal::SelftestFailed)));
}

// --- TM ---

void test_tm_matches_the_design_example() {
    LineWriter w;
    for (int i = 0; i < 1042; ++i) w.ack(line, sizeof line, "PING");
    w.tm(line, sizeof line, sample_tm());
    TEST_ASSERT_EQUAL_STRING("TM,1042,52100,TRACKING,2215,2180,0.008,97.5,-,-", line);
}

void test_tm_reports_the_fault_code() {
    Telemetry t = sample_tm();
    t.mode = Mode::Fault;
    t.fault = FaultCode::InjectedL;
    TEST_ASSERT_EQUAL_STRING("TM,0,52100,FAULT,2215,2180,0.008,97.5,INJECTED_L,-",
                             tm_line(t).c_str());
}

void test_tm_reports_the_simulated_sun_angle() {
    Telemetry t = sample_tm();
    t.has_sun_deg = true;
    t.sun_deg = -90.0f;
    TEST_ASSERT_EQUAL_STRING("TM,0,52100,TRACKING,2215,2180,0.008,97.5,-,-90.0",
                             tm_line(t).c_str());
}

void test_tm_rounds_error_to_three_and_angle_to_one_decimal() {
    Telemetry t = sample_tm();
    t.err = -0.2504f;
    t.angle_deg = 97.46f;
    TEST_ASSERT_EQUAL_STRING("TM,0,52100,TRACKING,2215,2180,-0.250,97.5,-,-", tm_line(t).c_str());
}

void test_tm_never_prints_negative_zero() {
    Telemetry t = sample_tm();
    t.err = -0.0004f;
    t.angle_deg = -0.04f;
    TEST_ASSERT_EQUAL_STRING("TM,0,52100,TRACKING,2215,2180,0.000,0.0,-,-", tm_line(t).c_str());
}

// --- EV ---

void test_ev_mode_line() {
    LineWriter w;
    Transition t;
    t.changed = true;
    t.from = Mode::Boot;
    t.to = Mode::Search;
    t.reason = Reason::Boot;
    w.ev_mode(line, sizeof line, 5, t);
    TEST_ASSERT_EQUAL_STRING("EV,0,5,MODE,BOOT,SEARCH,boot", line);
}

void test_ev_mode_without_a_change_writes_nothing() {
    LineWriter w;
    TEST_ASSERT_EQUAL_size_t(0, w.ev_mode(line, sizeof line, 5, Transition{}));
    TEST_ASSERT_EQUAL_UINT32(0, w.next_seq());
}

void test_ev_fault_line() {
    LineWriter w;
    w.ev_fault(line, sizeof line, 1200, FaultCode::SensorOpenR);
    TEST_ASSERT_EQUAL_STRING("EV,0,1200,FAULT,SENSOR_OPEN_R", line);
}

void test_ev_fault_without_a_fault_writes_nothing() {
    LineWriter w;
    TEST_ASSERT_EQUAL_size_t(0, w.ev_fault(line, sizeof line, 1200, FaultCode::None));
    TEST_ASSERT_EQUAL_UINT32(0, w.next_seq());
}

void test_ev_cal_line() {
    LineWriter w;
    w.ev_cal(line, sizeof line, 9000, 573, 1173, 873);
    TEST_ASSERT_EQUAL_STRING("EV,0,9000,CAL,573,1173,873", line);
}

// --- ACK and NACK ---

void test_ack_line() {
    LineWriter w;
    w.ack(line, sizeof line, "SIM SUN 45");
    TEST_ASSERT_EQUAL_STRING("ACK,0,SIM SUN 45", line);
}

void test_nack_line() {
    LineWriter w;
    w.nack(line, sizeof line, "CAL", NackReason::NotInSafe);
    TEST_ASSERT_EQUAL_STRING("NACK,0,CAL,not_in_safe", line);
}

// --- sequence counter ---

void test_every_line_type_takes_the_next_sequence_number() {
    LineWriter w;
    Transition t;
    t.changed = true;
    w.ev_mode(line, sizeof line, 0, t);
    w.ev_fault(line, sizeof line, 0, FaultCode::SensorOpenL);
    w.ev_cal(line, sizeof line, 0, 1, 2, 3);
    w.ack(line, sizeof line, "PING");
    w.nack(line, sizeof line, "X", NackReason::Unknown);
    w.tm(line, sizeof line, sample_tm());
    TEST_ASSERT_EQUAL_STRING("TM,5,52100,TRACKING,2215,2180,0.008,97.5,-,-", line);
    TEST_ASSERT_EQUAL_UINT32(6, w.next_seq());
}

void test_line_that_does_not_fit_is_not_written_and_keeps_the_sequence() {
    LineWriter w;
    char small[8];
    TEST_ASSERT_EQUAL_size_t(0, w.ack(small, sizeof small, "SIM SUN 45"));
    TEST_ASSERT_EQUAL_UINT32(0, w.next_seq());
}

void test_longest_lines_fit_the_line_buffer() {
    LineWriter w;
    for (uint32_t i = 0; i < 3; ++i) w.ack(line, sizeof line, "PING");  // seq is not maximal,
    Telemetry t;                                                         // but its digits are
    t.t_ms = UINT32_MAX;
    t.mode = Mode::Tracking;
    t.l_mv = -2147483647;
    t.r_mv = -2147483647;
    t.err = -1.0f;
    t.angle_deg = -180.0f;
    t.fault = FaultCode::SelftestShort;
    t.has_sun_deg = true;
    t.sun_deg = -180.0f;
    TEST_ASSERT_GREATER_THAN_size_t(0, w.tm(line, sizeof line, t));

    const std::string echo(config::COMMAND_MAX_CHARS, 'X');
    TEST_ASSERT_GREATER_THAN_size_t(
        0, w.nack(line, sizeof line, echo.c_str(), NackReason::CalOutOfRange));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_mode_names);
    RUN_TEST(test_reason_names);
    RUN_TEST(test_fault_names);
    RUN_TEST(test_nack_names);
    RUN_TEST(test_parse_errors_map_to_nack_reasons);
    RUN_TEST(test_refusals_map_to_nack_reasons);
    RUN_TEST(test_tm_matches_the_design_example);
    RUN_TEST(test_tm_reports_the_fault_code);
    RUN_TEST(test_tm_reports_the_simulated_sun_angle);
    RUN_TEST(test_tm_rounds_error_to_three_and_angle_to_one_decimal);
    RUN_TEST(test_tm_never_prints_negative_zero);
    RUN_TEST(test_ev_mode_line);
    RUN_TEST(test_ev_mode_without_a_change_writes_nothing);
    RUN_TEST(test_ev_fault_line);
    RUN_TEST(test_ev_fault_without_a_fault_writes_nothing);
    RUN_TEST(test_ev_cal_line);
    RUN_TEST(test_ack_line);
    RUN_TEST(test_nack_line);
    RUN_TEST(test_every_line_type_takes_the_next_sequence_number);
    RUN_TEST(test_line_that_does_not_fit_is_not_written_and_keeps_the_sequence);
    RUN_TEST(test_longest_lines_fit_the_line_buffer);
    return UNITY_END();
}

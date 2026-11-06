// The core cycle against fake ports: which lines come out, in which order, and
// what the servo is told. The light model and full scenarios are in test_sil.
#include <unity.h>

#include <string>
#include <vector>

#include "core.h"

using namespace core;

void setUp() {}
void tearDown() {}

static const FaultParams fp;
static const SensingParams sp;
static const ModeParams mp;
static const int32_t FLOOR = config::ADC_FLOOR_MV;
static const int32_t BRIGHT = 2500;  // each channel; the sum is above the boot default
static const int32_t DIM = 1000;     // each channel; the sum is below the exit threshold

struct FakeSensors : SensorPort {
    SensorReading lit{DIM, DIM};
    SensorReading off{FLOOR, FLOOR};
    int reads_off = 0;
    SensorReading read() override { return lit; }
    SensorReading read_supply_off() override {
        ++reads_off;
        return off;
    }
};

struct FakeServo : ServoPort {
    std::vector<float> angles;
    void command(float angle_deg) override { angles.push_back(angle_deg); }
};

struct Lines : LineSink {
    std::vector<std::string> all;
    void write_line(const char* line) override { all.emplace_back(line); }
};

struct FakeSim : SimControl {
    float deg = 0.0f;
    void set_sun_deg(float sun_deg) override { deg = sun_deg; }
    float sun_deg() const override { return deg; }
};

// Drops seq and t_ms, which these tests do not care about:
// "EV,7,350,MODE,A,B,r" -> "EV,MODE,A,B,r"; "ACK,7,PING" -> "ACK,PING".
static std::string body(const std::string& line) {
    std::vector<std::string> f;
    size_t start = 0;
    while (true) {
        const size_t comma = line.find(',', start);
        f.push_back(line.substr(start, comma - start));
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    const size_t drop = (f[0] == "ACK" || f[0] == "NACK") ? 1 : 2;
    std::string out = f[0];
    for (size_t i = 1 + drop; i < f.size(); ++i) out += "," + f[i];
    return out;
}

struct Rig {
    FakeSensors sensors;
    FakeServo servo;
    Lines lines;
    FakeSim sim;
    Core core;
    uint32_t t_ms = 0;
    size_t seen = 0;

    explicit Rig(bool with_sim = false, const CoreParams& params = {})
        : core(sensors, servo, lines, with_sim ? &sim : nullptr, params) {}

    void boot() { core.boot(t_ms); }
    void step(uint32_t n = 1) {
        for (uint32_t i = 0; i < n; ++i) {
            t_ms += config::CYCLE_MS;
            core.step(t_ms);
        }
    }
    void send(const char* line) { core.handle_line(t_ms, line); }

    // The lines written since the last call, without TM, as bodies.
    std::vector<std::string> news() {
        std::vector<std::string> out;
        for (; seen < lines.all.size(); ++seen) {
            if (lines.all[seen].rfind("TM,", 0) != 0) out.push_back(body(lines.all[seen]));
        }
        return out;
    }
    std::string last_tm() const {
        for (auto it = lines.all.rbegin(); it != lines.all.rend(); ++it) {
            if (it->rfind("TM,", 0) == 0) return body(*it);
        }
        return "";
    }

    void to_tracking() {
        boot();
        sensors.lit = {BRIGHT, BRIGHT};
        step(mp.sun_found_cycles);
        news();
    }
    void to_safe() {
        boot();
        send("SAFE");
        news();
    }
    void to_injected_fault() {
        to_safe();
        sensors.lit = {BRIGHT, BRIGHT};
        send("INJECT L");
        step(fp.open_fault_cycles);
        news();
    }
};

static void assert_lines(const std::vector<std::string>& expected,
                         const std::vector<std::string>& actual) {
    TEST_ASSERT_EQUAL_size_t_MESSAGE(expected.size(), actual.size(), "number of lines");
    for (size_t i = 0; i < expected.size(); ++i) {
        TEST_ASSERT_EQUAL_STRING(expected[i].c_str(), actual[i].c_str());
    }
}

// --- boot ---

void test_boot_parks_the_servo_and_goes_to_search() {
    Rig rig;
    rig.boot();
    TEST_ASSERT_EQUAL_FLOAT(config::ANGLE_PARK_DEG, rig.servo.angles.front());
    assert_lines({"EV,MODE,BOOT,SEARCH,boot"}, rig.news());
}

void test_boot_with_a_short_reports_the_fault_then_the_mode() {
    Rig rig;
    rig.sensors.off = {fp.selftest_short_mv + 1, FLOOR};
    rig.boot();
    assert_lines({"EV,FAULT,SELFTEST_SHORT", "EV,MODE,BOOT,FAULT,fault"}, rig.news());
}

void test_nothing_happens_before_boot() {
    Rig rig;
    rig.step(10);
    TEST_ASSERT_TRUE(rig.lines.all.empty());
    TEST_ASSERT_TRUE(rig.servo.angles.empty());
}

void test_first_line_has_sequence_number_zero() {
    Rig rig;
    rig.boot();
    TEST_ASSERT_EQUAL_STRING("EV,0,0,MODE,BOOT,SEARCH,boot", rig.lines.all.front().c_str());
}

// --- telemetry ---

void test_telemetry_is_sent_every_second_cycle() {
    Rig rig;
    rig.boot();
    rig.step(4);
    TEST_ASSERT_EQUAL_size_t(1 + 2, rig.lines.all.size());
}

void test_telemetry_reports_the_sweep_in_search() {
    Rig rig;
    rig.boot();
    rig.step(2);
    TEST_ASSERT_EQUAL_STRING("TM,1,100,SEARCH,1000,1000,0.000,94.5,-,-",
                             rig.lines.all.back().c_str());
}

void test_telemetry_applies_r_gain() {
    CoreParams p;
    p.sensing.r_gain = 1.1f;
    Rig rig(false, p);
    rig.boot();
    rig.step(2);
    TEST_ASSERT_EQUAL_STRING("TM,SEARCH,1000,1100,-0.048,94.5,-,-", rig.last_tm().c_str());
}

// --- SEARCH and TRACKING ---

void test_sun_present_for_enough_cycles_starts_tracking() {
    Rig rig;
    rig.boot();
    rig.news();
    rig.sensors.lit = {BRIGHT, BRIGHT};
    rig.step(mp.sun_found_cycles);
    assert_lines({"EV,MODE,SEARCH,TRACKING,sun_found"}, rig.news());
}

void test_tracking_turns_towards_the_brighter_side() {
    Rig rig;
    rig.to_tracking();
    const float before = rig.core.angle_deg();
    rig.sensors.lit = {3000, 2000};  // left brighter: e = 0.2
    rig.step();
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, before + config::KP * 0.2f, rig.core.angle_deg());
}

void test_tracking_holds_inside_the_deadband() {
    Rig rig;
    rig.to_tracking();
    const float before = rig.core.angle_deg();
    rig.step(5);
    TEST_ASSERT_EQUAL_FLOAT(before, rig.core.angle_deg());
}

void test_sun_absent_for_enough_cycles_returns_to_search() {
    Rig rig;
    rig.to_tracking();
    rig.sensors.lit = {DIM, DIM};
    rig.step(mp.sun_lost_cycles);
    assert_lines({"EV,MODE,TRACKING,SEARCH,sun_lost"}, rig.news());
}

void test_three_sweeps_without_sun_go_to_safe() {
    Rig rig;
    rig.boot();
    rig.news();
    for (int i = 0; i < 2000 && rig.core.mode() == Mode::Search; ++i) rig.step();
    assert_lines({"EV,MODE,SEARCH,SAFE,no_sun"}, rig.news());
}

// --- SAFE ---

void test_safe_command_acks_then_reports_the_mode_change() {
    Rig rig;
    rig.to_tracking();
    rig.send("SAFE");
    assert_lines({"ACK,SAFE", "EV,MODE,TRACKING,SAFE,cmd_safe"}, rig.news());
}

void test_safe_parks_the_servo() {
    Rig rig;
    rig.boot();
    rig.step(10);
    rig.send("SAFE");
    rig.step();
    TEST_ASSERT_EQUAL_FLOAT(config::ANGLE_PARK_DEG, rig.servo.angles.back());
}

void test_safe_command_in_safe_is_only_acked() {
    Rig rig;
    rig.to_safe();
    rig.send("SAFE");
    assert_lines({"ACK,SAFE"}, rig.news());
}

void test_search_command_from_safe_starts_search() {
    Rig rig;
    rig.to_safe();
    rig.send("SEARCH");
    assert_lines({"ACK,SEARCH", "EV,MODE,SAFE,SEARCH,cmd_search"}, rig.news());
}

// --- faults ---

void test_open_sensor_reports_the_fault_then_the_mode() {
    Rig rig;
    rig.to_safe();
    rig.sensors.lit = {FLOOR, BRIGHT};
    rig.step(fp.open_fault_cycles);
    assert_lines({"EV,FAULT,SENSOR_OPEN_L", "EV,MODE,SAFE,FAULT,fault"}, rig.news());
}

void test_fault_holds_the_last_angle() {
    Rig rig;
    rig.boot();
    rig.step(10);
    rig.sensors.lit = {BRIGHT, FLOOR};
    for (int i = 0; i < 100 && rig.core.mode() != Mode::Fault; ++i) rig.step();
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Mode::Fault), static_cast<uint8_t>(rig.core.mode()));
    const float held = rig.core.angle_deg();
    rig.sensors.lit = {3000, 1000};
    rig.step(50);
    TEST_ASSERT_EQUAL_FLOAT(held, rig.servo.angles.back());
}

void test_telemetry_reports_the_active_fault() {
    Rig rig;
    rig.to_injected_fault();
    rig.step(2);
    TEST_ASSERT_EQUAL_STRING("TM,FAULT,142,2500,-0.893,90.0,INJECTED_L,-", rig.last_tm().c_str());
}

// --- INJECT ---

void test_inject_acks_and_faults_after_the_debounce() {
    Rig rig;
    rig.to_safe();
    rig.sensors.lit = {BRIGHT, BRIGHT};
    rig.send("INJECT L");
    assert_lines({"ACK,INJECT L"}, rig.news());
    rig.step(fp.open_fault_cycles - 1);
    assert_lines({}, rig.news());
    rig.step();
    assert_lines({"EV,FAULT,INJECTED_L", "EV,MODE,SAFE,FAULT,fault"}, rig.news());
}

void test_injected_channel_reads_the_floor_in_telemetry() {
    Rig rig;
    rig.to_safe();
    rig.sensors.lit = {BRIGHT, BRIGHT};
    rig.send("INJECT R");
    rig.step(2);
    TEST_ASSERT_EQUAL_STRING("TM,SAFE,2500,142,0.893,90.0,-,-", rig.last_tm().c_str());
}

void test_second_inject_replaces_the_first() {
    Rig rig;
    rig.to_safe();
    rig.send("INJECT L");
    rig.send("INJECT R");
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Injection::R),
                            static_cast<uint8_t>(rig.core.injection()));
}

void test_inject_in_fault_is_refused() {
    Rig rig;
    rig.to_injected_fault();
    rig.send("INJECT R");
    assert_lines({"NACK,INJECT R,fault_latched"}, rig.news());
}

// --- CLEAR ---

void test_clear_outside_fault_is_refused_without_running_checks() {
    Rig rig;
    rig.to_safe();
    const int reads_before = rig.sensors.reads_off;
    rig.send("CLEAR");
    assert_lines({"NACK,CLEAR,not_in_fault"}, rig.news());
    TEST_ASSERT_EQUAL_INT(reads_before, rig.sensors.reads_off);
}

void test_clear_with_passing_checks_returns_to_search() {
    Rig rig;
    rig.to_injected_fault();
    rig.send("CLEAR");
    assert_lines({"ACK,CLEAR", "EV,MODE,FAULT,SEARCH,cmd_clear"}, rig.news());
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(FaultCode::None),
                            static_cast<uint8_t>(rig.core.fault()));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Injection::None),
                            static_cast<uint8_t>(rig.core.injection()));
}

void test_clear_with_a_failing_selftest_stays_in_fault_and_reports_the_new_code() {
    Rig rig;
    rig.to_injected_fault();
    rig.sensors.off = {FLOOR, fp.selftest_short_mv + 1};
    rig.send("CLEAR");
    assert_lines({"NACK,CLEAR,selftest_failed", "EV,FAULT,SELFTEST_SHORT"}, rig.news());
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Mode::Fault),
                            static_cast<uint8_t>(rig.core.mode()));
}

void test_clear_with_the_sensor_still_open_reports_no_new_event() {
    Rig rig;
    rig.to_safe();
    rig.sensors.lit = {FLOOR, BRIGHT};
    rig.step(fp.open_fault_cycles);
    rig.news();
    rig.send("CLEAR");
    assert_lines({"NACK,CLEAR,selftest_failed"}, rig.news());
}

void test_clear_removes_the_injection_even_if_the_checks_fail() {
    Rig rig;
    rig.to_injected_fault();
    rig.sensors.off = {FLOOR, fp.selftest_short_mv + 1};
    rig.send("CLEAR");
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Injection::None),
                            static_cast<uint8_t>(rig.core.injection()));
}

void test_after_clear_a_new_fault_needs_the_full_debounce() {
    Rig rig;
    rig.to_injected_fault();
    rig.send("CLEAR");
    rig.news();
    rig.sensors.lit = {FLOOR, BRIGHT};
    rig.step(fp.open_fault_cycles - 1);
    assert_lines({}, rig.news());
}

// --- CAL ---

void test_cal_in_safe_sets_thresholds_from_the_room_light() {
    Rig rig;
    rig.to_safe();
    rig.sensors.lit = {283, 290};
    rig.send("CAL");
    const int32_t room = 283 + 290;
    assert_lines({"ACK,CAL", "EV,CAL," + std::to_string(room) + "," +
                                 std::to_string(room + sp.sun_enter_margin_mv) + "," +
                                 std::to_string(room + sp.sun_exit_margin_mv)},
                 rig.news());
    TEST_ASSERT_EQUAL_INT32(room + sp.sun_enter_margin_mv, rig.core.sun().enter_mv());
}

void test_after_cal_a_dim_torch_is_found() {
    Rig rig;
    rig.to_safe();
    rig.sensors.lit = {283, 290};
    rig.send("CAL");
    rig.send("SEARCH");
    rig.news();
    rig.sensors.lit = {DIM, DIM};  // far below the boot default, well above room + margin
    rig.step(mp.sun_found_cycles);
    assert_lines({"EV,MODE,SEARCH,TRACKING,sun_found"}, rig.news());
}

void test_cal_in_a_too_bright_room_is_refused_and_keeps_the_thresholds() {
    Rig rig;
    rig.to_safe();
    const int32_t each = (sp.sun_enter_max_mv - sp.sun_enter_margin_mv) / 2 + 1;
    rig.sensors.lit = {each, each};
    rig.send("CAL");
    assert_lines({"NACK,CAL,cal_out_of_range"}, rig.news());
    TEST_ASSERT_EQUAL_INT32(sp.sun_enter_mv, rig.core.sun().enter_mv());
}

void test_cal_outside_safe_is_refused() {
    Rig rig;
    rig.boot();
    rig.news();
    rig.send("CAL");
    assert_lines({"NACK,CAL,not_in_safe"}, rig.news());
}

// --- other commands ---

void test_ping_is_acked() {
    Rig rig;
    rig.boot();
    rig.news();
    rig.send("PING\r\n");
    assert_lines({"ACK,PING"}, rig.news());
}

void test_unknown_line_is_refused_with_a_safe_echo() {
    Rig rig;
    rig.boot();
    rig.news();
    rig.send("A,B");
    assert_lines({"NACK,A_B,unknown"}, rig.news());
}

void test_sim_sun_without_a_simulation_is_refused() {
    Rig rig;
    rig.boot();
    rig.news();
    rig.send("SIM SUN 45");
    assert_lines({"NACK,SIM SUN 45,not_sim"}, rig.news());
}

void test_sim_sun_moves_the_light_and_shows_in_telemetry() {
    Rig rig(true);
    rig.boot();
    rig.news();
    rig.send("SIM SUN -90");
    assert_lines({"ACK,SIM SUN -90"}, rig.news());
    rig.step(2);
    TEST_ASSERT_EQUAL_STRING("TM,SEARCH,1000,1000,0.000,94.5,-,-90.0", rig.last_tm().c_str());
}

void test_sim_sun_is_accepted_in_fault() {
    Rig rig(true);
    rig.to_injected_fault();
    rig.send("SIM SUN 10");
    assert_lines({"ACK,SIM SUN 10"}, rig.news());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_boot_parks_the_servo_and_goes_to_search);
    RUN_TEST(test_boot_with_a_short_reports_the_fault_then_the_mode);
    RUN_TEST(test_nothing_happens_before_boot);
    RUN_TEST(test_first_line_has_sequence_number_zero);
    RUN_TEST(test_telemetry_is_sent_every_second_cycle);
    RUN_TEST(test_telemetry_reports_the_sweep_in_search);
    RUN_TEST(test_telemetry_applies_r_gain);
    RUN_TEST(test_sun_present_for_enough_cycles_starts_tracking);
    RUN_TEST(test_tracking_turns_towards_the_brighter_side);
    RUN_TEST(test_tracking_holds_inside_the_deadband);
    RUN_TEST(test_sun_absent_for_enough_cycles_returns_to_search);
    RUN_TEST(test_three_sweeps_without_sun_go_to_safe);
    RUN_TEST(test_safe_command_acks_then_reports_the_mode_change);
    RUN_TEST(test_safe_parks_the_servo);
    RUN_TEST(test_safe_command_in_safe_is_only_acked);
    RUN_TEST(test_search_command_from_safe_starts_search);
    RUN_TEST(test_open_sensor_reports_the_fault_then_the_mode);
    RUN_TEST(test_fault_holds_the_last_angle);
    RUN_TEST(test_telemetry_reports_the_active_fault);
    RUN_TEST(test_inject_acks_and_faults_after_the_debounce);
    RUN_TEST(test_injected_channel_reads_the_floor_in_telemetry);
    RUN_TEST(test_second_inject_replaces_the_first);
    RUN_TEST(test_inject_in_fault_is_refused);
    RUN_TEST(test_clear_outside_fault_is_refused_without_running_checks);
    RUN_TEST(test_clear_with_passing_checks_returns_to_search);
    RUN_TEST(test_clear_with_a_failing_selftest_stays_in_fault_and_reports_the_new_code);
    RUN_TEST(test_clear_with_the_sensor_still_open_reports_no_new_event);
    RUN_TEST(test_clear_removes_the_injection_even_if_the_checks_fail);
    RUN_TEST(test_after_clear_a_new_fault_needs_the_full_debounce);
    RUN_TEST(test_cal_in_safe_sets_thresholds_from_the_room_light);
    RUN_TEST(test_after_cal_a_dim_torch_is_found);
    RUN_TEST(test_cal_in_a_too_bright_room_is_refused_and_keeps_the_thresholds);
    RUN_TEST(test_cal_outside_safe_is_refused);
    RUN_TEST(test_ping_is_acked);
    RUN_TEST(test_unknown_line_is_refused_with_a_safe_echo);
    RUN_TEST(test_sim_sun_without_a_simulation_is_refused);
    RUN_TEST(test_sim_sun_moves_the_light_and_shows_in_telemetry);
    RUN_TEST(test_sim_sun_is_accepted_in_fault);
    return UNITY_END();
}

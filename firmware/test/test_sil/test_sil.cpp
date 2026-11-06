// SIL scenarios: the real core, in a loop against the light model, in the
// measured dark and bright rooms. Every scenario also checks that the commanded
// angle never left the end stops.
#include <unity.h>

#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "core.h"
#include "light_model.h"

using namespace core;

void setUp() {}
void tearDown() {}

static constexpr uint32_t CYCLES_PER_S = 1000 / config::CYCLE_MS;

struct Lines : LineSink {
    std::vector<std::string> all;
    void write_line(const char* line) override { all.emplace_back(line); }
};

struct Sil {
    SimPlant plant;
    Lines lines;
    Core core;
    uint32_t t_ms = 0;

    explicit Sil(const LightParams& light, uint32_t seed = 1)
        : plant(light, seed), core(plant, plant, lines, &plant) {
        core.boot(t_ms);
    }

    void step(uint32_t n = 1) {
        for (uint32_t i = 0; i < n; ++i) {
            t_ms += config::CYCLE_MS;
            core.step(t_ms);
        }
    }
    void send(const char* line) { core.handle_line(t_ms, line); }

    // Steps until done() or the limit; returns the cycles taken, or -1 at the limit.
    int run_until(const std::function<bool()>& done, uint32_t max_cycles) {
        for (uint32_t i = 1; i <= max_cycles; ++i) {
            step();
            if (done()) return static_cast<int>(i);
        }
        return -1;
    }
    int run_until_mode(Mode mode, uint32_t max_cycles) {
        return run_until([&] { return core.mode() == mode; }, max_cycles);
    }

    // The demo procedure: SAFE, CAL with the torch off, then SEARCH.
    void calibrate() {
        send("SAFE");
        plant.torch_on = false;
        step();
        send("CAL");
        plant.torch_on = true;
        send("SEARCH");
    }

    bool has_line_ending(const std::string& tail) const {
        for (const std::string& line : lines.all) {
            if (line.size() >= tail.size() &&
                line.compare(line.size() - tail.size(), tail.size(), tail) == 0) {
                return true;
            }
        }
        return false;
    }

    float pointing_error_deg() const { return plant.angle_deg() - plant.sun_deg(); }

    void assert_end_stops_kept() const {
        TEST_ASSERT_TRUE_MESSAGE(plant.min_angle_deg() >= config::ANGLE_MIN_DEG, "below end stop");
        TEST_ASSERT_TRUE_MESSAGE(plant.max_angle_deg() <= config::ANGLE_MAX_DEG, "above end stop");
    }
};

static void assert_mode(Mode expected, Mode actual) {
    TEST_ASSERT_EQUAL_STRING(mode_name(expected), mode_name(actual));
}

static void report(const char* what, int cycles) {
    char msg[96];
    std::snprintf(msg, sizeof msg, "%s: %.2f s", what,
                  static_cast<double>(cycles) / CYCLES_PER_S);
    TEST_MESSAGE(msg);
}

static void report_deg(const char* what, float deg) {
    char msg[96];
    std::snprintf(msg, sizeof msg, "%s: %.2f deg", what, static_cast<double>(deg));
    TEST_MESSAGE(msg);
}

// How far from the light the controller may stop: the dead band in e, converted to
// degrees with the slope of e at the target, plus a little for noise. Room light
// flattens that slope, so the platform settles less precisely in a bright room.
static float settle_bound_deg(const LightParams& room) {
    const float sum_mv = room.ambient_l_mv + room.ambient_r_mv + room.intensity_mv;
    const float e_per_deg = room.intensity_mv / sum_mv * 2.0f / room.shadow_width_deg;
    return config::DEADBAND / e_per_deg + 0.5f;
}

// Acquires the light at sun_deg after CAL and lets the controller settle.
static void acquire(Sil& sil, float sun_deg) {
    sil.plant.set_sun_deg(sun_deg);
    sil.calibrate();
    TEST_ASSERT_GREATER_THAN_INT(0, sil.run_until_mode(Mode::Tracking, 20 * CYCLES_PER_S));
    sil.step(2 * CYCLES_PER_S);
}

// --- acquisition ---

void test_acquires_a_light_near_the_bottom_end_stop() {
    Sil sil(dark_room());
    acquire(sil, 15.0f);
    assert_mode(Mode::Tracking, sil.core.mode());
    TEST_ASSERT_FLOAT_WITHIN(settle_bound_deg(dark_room()), 0.0f, sil.pointing_error_deg());
    sil.assert_end_stops_kept();
}

void test_acquires_a_light_near_the_top_end_stop() {
    Sil sil(dark_room());
    acquire(sil, 165.0f);
    assert_mode(Mode::Tracking, sil.core.mode());
    TEST_ASSERT_FLOAT_WITHIN(settle_bound_deg(dark_room()), 0.0f, sil.pointing_error_deg());
    sil.assert_end_stops_kept();
}

void test_acquires_in_the_bright_room_after_cal() {
    Sil sil(bright_room());
    acquire(sil, 60.0f);
    assert_mode(Mode::Tracking, sil.core.mode());
    report_deg("settle bound, bright room", settle_bound_deg(bright_room()));
    TEST_ASSERT_FLOAT_WITHIN(settle_bound_deg(bright_room()), 0.0f, sil.pointing_error_deg());
    sil.assert_end_stops_kept();
}

void test_light_beyond_an_end_stop_waits_there_in_tracking() {
    Sil sil(dark_room());
    acquire(sil, 165.0f);
    sil.plant.set_sun_deg(178.0f);
    sil.step(5 * CYCLES_PER_S);
    assert_mode(Mode::Tracking, sil.core.mode());
    TEST_ASSERT_EQUAL_FLOAT(config::ANGLE_MAX_DEG, sil.plant.angle_deg());
    sil.assert_end_stops_kept();
}

// --- tracking ---

void test_tracks_a_moving_light() {
    Sil sil(dark_room());
    acquire(sil, 40.0f);
    const float rate_deg_per_cycle = 10.0f / CYCLES_PER_S;  // 10 deg/s
    float worst_deg = 0.0f;
    for (float sun = 40.0f; sun <= 140.0f; sun += rate_deg_per_cycle) {
        sil.plant.set_sun_deg(sun);
        sil.step();
        assert_mode(Mode::Tracking, sil.core.mode());
        worst_deg = std::fmax(worst_deg, std::fabs(sil.pointing_error_deg()));
    }
    char msg[64];
    std::snprintf(msg, sizeof msg, "worst pointing error at 10 deg/s: %.2f deg",
                  static_cast<double>(worst_deg));
    TEST_MESSAGE(msg);
    TEST_ASSERT_TRUE(worst_deg < 3.0f);
    sil.assert_end_stops_kept();
}

void test_light_lost_returns_to_search_and_is_found_again() {
    Sil sil(dark_room());
    acquire(sil, 90.0f);
    sil.plant.torch_on = false;
    const int lost = sil.run_until_mode(Mode::Search, 5 * CYCLES_PER_S);
    TEST_ASSERT_EQUAL_INT(config::SUN_LOST_CYCLES, lost);
    TEST_ASSERT_TRUE(sil.has_line_ending("MODE,TRACKING,SEARCH,sun_lost"));

    sil.plant.torch_on = true;
    TEST_ASSERT_GREATER_THAN_INT(0, sil.run_until_mode(Mode::Tracking, 10 * CYCLES_PER_S));
    sil.step(2 * CYCLES_PER_S);
    TEST_ASSERT_FLOAT_WITHIN(settle_bound_deg(dark_room()), 0.0f, sil.pointing_error_deg());
    sil.assert_end_stops_kept();
}

// --- no light ---

void test_three_sweeps_without_light_end_in_safe() {
    Sil sil(dark_room());
    sil.plant.torch_on = false;
    const int cycles = sil.run_until_mode(Mode::Safe, 30 * CYCLES_PER_S);
    report("SEARCH to SAFE without light", cycles);
    // From 90 deg: 80 deg up to the stop (not counted), then 3 full sweeps of 160 deg.
    // The last step of each run is cut short at the end stop, so each run takes a whole
    // number of cycles; the mode changes in the cycle after the third sweep ends.
    const auto cycles_for = [](float deg) {
        return static_cast<int>(std::ceil(deg / config::SEARCH_STEP_DEG));
    };
    const int expected = cycles_for(80.0f) + 3 * cycles_for(160.0f) + 1;
    TEST_ASSERT_EQUAL_INT(expected, cycles);
    TEST_ASSERT_TRUE(sil.has_line_ending("MODE,SEARCH,SAFE,no_sun"));
    sil.assert_end_stops_kept();
}

void test_dark_room_without_cal_fails_safe_and_cal_recovers() {
    Sil sil(dark_room());  // the torch is on, but it cannot reach the boot default
    bool tracked = false;
    const int cycles = sil.run_until(
        [&] {
            tracked = tracked || sil.core.mode() == Mode::Tracking;
            return sil.core.mode() == Mode::Safe;
        },
        30 * CYCLES_PER_S);
    TEST_ASSERT_GREATER_THAN_INT(0, cycles);
    TEST_ASSERT_FALSE_MESSAGE(tracked, "tracked before CAL");

    sil.calibrate();
    TEST_ASSERT_GREATER_THAN_INT(0, sil.run_until_mode(Mode::Tracking, 20 * CYCLES_PER_S));
    sil.assert_end_stops_kept();
}

// --- faults ---

// Opens the left sensor while tracking. The fault must latch after exactly the
// debounce, with the platform still pointed where it was: TRACKING holds while a
// sensor looks open. (Without the hold, the dark room never latched the fault.)
static void open_sensor_while_tracking(const LightParams& room, const char* what) {
    Sil sil(room);
    acquire(sil, 90.0f);
    const float pointed = sil.plant.angle_deg();
    sil.plant.open_l = true;
    const int cycles = sil.run_until_mode(Mode::Fault, 30 * CYCLES_PER_S);
    report(what, cycles);
    TEST_ASSERT_EQUAL_INT(config::OPEN_FAULT_CYCLES, cycles);
    TEST_ASSERT_TRUE(sil.has_line_ending("FAULT,SENSOR_OPEN_L"));
    TEST_ASSERT_EQUAL_FLOAT(pointed, sil.plant.angle_deg());
    sil.assert_end_stops_kept();
}

void test_open_sensor_in_the_dark_room_faults_after_the_debounce() {
    open_sensor_while_tracking(dark_room(), "open sensor to FAULT, dark room");
}

void test_open_sensor_in_the_bright_room_faults_after_the_debounce() {
    open_sensor_while_tracking(bright_room(), "open sensor to FAULT, bright room");
}

void test_injected_fault_and_clear_return_to_tracking() {
    Sil sil(dark_room());
    acquire(sil, 90.0f);
    sil.send("INJECT R");
    const int cycles = sil.run_until_mode(Mode::Fault, 30 * CYCLES_PER_S);
    report("INJECT R to FAULT, dark room", cycles);
    TEST_ASSERT_EQUAL_INT(config::OPEN_FAULT_CYCLES, cycles);
    TEST_ASSERT_TRUE(sil.has_line_ending("FAULT,INJECTED_R"));

    const float held = sil.plant.angle_deg();
    sil.step(CYCLES_PER_S);
    TEST_ASSERT_EQUAL_FLOAT(held, sil.plant.angle_deg());

    sil.send("CLEAR");
    assert_mode(Mode::Search, sil.core.mode());
    TEST_ASSERT_GREATER_THAN_INT(0, sil.run_until_mode(Mode::Tracking, 20 * CYCLES_PER_S));
    sil.step(2 * CYCLES_PER_S);
    TEST_ASSERT_FLOAT_WITHIN(settle_bound_deg(dark_room()), 0.0f, sil.pointing_error_deg());
    sil.assert_end_stops_kept();
}

void test_short_fails_the_boot_selftest_and_blocks_clear_until_removed() {
    // Not a Sil rig: the short has to be in place before boot().
    SimPlant plant(dark_room());
    plant.short_l = true;
    Lines lines;
    Core core(plant, plant, lines, &plant);
    core.boot(0);
    assert_mode(Mode::Fault, core.mode());
    TEST_ASSERT_EQUAL_STRING("EV,0,0,FAULT,SELFTEST_SHORT", lines.all.front().c_str());

    core.handle_line(0, "CLEAR");
    assert_mode(Mode::Fault, core.mode());
    plant.short_l = false;
    core.handle_line(0, "CLEAR");
    assert_mode(Mode::Search, core.mode());
}

// --- repeatability ---

static std::vector<std::string> scripted_run(uint32_t seed) {
    Sil sil(dark_room(), seed);
    acquire(sil, 70.0f);
    sil.plant.set_sun_deg(110.0f);
    sil.step(5 * CYCLES_PER_S);
    return sil.lines.all;
}

void test_same_seed_gives_exactly_the_same_lines() {
    const std::vector<std::string> a = scripted_run(123);
    const std::vector<std::string> b = scripted_run(123);
    TEST_ASSERT_EQUAL_size_t(a.size(), b.size());
    for (size_t i = 0; i < a.size(); ++i) TEST_ASSERT_EQUAL_STRING(a[i].c_str(), b[i].c_str());
}

void test_different_seed_gives_different_noise() {
    TEST_ASSERT_FALSE(scripted_run(123) == scripted_run(456));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_acquires_a_light_near_the_bottom_end_stop);
    RUN_TEST(test_acquires_a_light_near_the_top_end_stop);
    RUN_TEST(test_acquires_in_the_bright_room_after_cal);
    RUN_TEST(test_light_beyond_an_end_stop_waits_there_in_tracking);
    RUN_TEST(test_tracks_a_moving_light);
    RUN_TEST(test_light_lost_returns_to_search_and_is_found_again);
    RUN_TEST(test_three_sweeps_without_light_end_in_safe);
    RUN_TEST(test_dark_room_without_cal_fails_safe_and_cal_recovers);
    RUN_TEST(test_open_sensor_in_the_dark_room_faults_after_the_debounce);
    RUN_TEST(test_open_sensor_in_the_bright_room_faults_after_the_debounce);
    RUN_TEST(test_injected_fault_and_clear_return_to_tracking);
    RUN_TEST(test_short_fails_the_boot_selftest_and_blocks_clear_until_removed);
    RUN_TEST(test_same_seed_gives_exactly_the_same_lines);
    RUN_TEST(test_different_seed_gives_different_noise);
    return UNITY_END();
}

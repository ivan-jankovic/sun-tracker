#include <unity.h>

#include "faults.h"

using core::apply_injection;
using core::check_selftest;
using core::FaultCode;
using core::FaultDetector;
using core::FaultParams;
using core::Injection;
using core::open_sensor;

static const FaultParams fp;
static const int32_t OPEN = fp.open_mv - 1;           // reads as possibly open
static const int32_t LIT = fp.open_other_min_mv + 1;  // bright enough to compare against
static const int32_t SHADED = fp.open_mv;              // dark, but not open

void setUp() {}
void tearDown() {}

static void assert_code(FaultCode expected, FaultCode actual) {
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(expected), static_cast<uint8_t>(actual));
}

// Feeds the same reading for n cycles; returns the last result.
static FaultCode feed(FaultDetector& det, uint32_t n, int32_t l_mv, int32_t r_mv,
                      Injection injection = Injection::None) {
    FaultCode code = FaultCode::None;
    for (uint32_t i = 0; i < n; ++i) {
        code = det.update(l_mv, r_mv, injection);
    }
    return code;
}

// --- open_sensor: the condition for one cycle ---

void test_left_low_and_right_lit_is_open_left() {
    assert_code(FaultCode::SensorOpenL, open_sensor(OPEN, LIT));
}

void test_right_low_and_left_lit_is_open_right() {
    assert_code(FaultCode::SensorOpenR, open_sensor(LIT, OPEN));
}

void test_both_channels_low_is_not_open() {
    assert_code(FaultCode::None, open_sensor(OPEN, OPEN));
}

void test_low_channel_with_dim_other_is_not_open() {
    assert_code(FaultCode::None, open_sensor(OPEN, fp.open_other_min_mv));
}

void test_channel_at_open_threshold_is_not_open() {
    assert_code(FaultCode::None, open_sensor(SHADED, LIT));
}

// --- FaultDetector: the debounce ---

void test_open_condition_shorter_than_debounce_is_no_fault() {
    FaultDetector det;
    assert_code(FaultCode::None, feed(det, fp.open_fault_cycles - 1, OPEN, LIT));
}

void test_open_condition_for_debounce_cycles_is_a_fault() {
    FaultDetector det;
    assert_code(FaultCode::SensorOpenL, feed(det, fp.open_fault_cycles, OPEN, LIT));
}

void test_open_right_for_debounce_cycles_is_a_fault() {
    FaultDetector det;
    assert_code(FaultCode::SensorOpenR, feed(det, fp.open_fault_cycles, LIT, OPEN));
}

void test_one_normal_cycle_restarts_the_debounce() {
    FaultDetector det;
    feed(det, fp.open_fault_cycles - 1, OPEN, LIT);
    det.update(LIT, LIT);
    assert_code(FaultCode::None, feed(det, fp.open_fault_cycles - 1, OPEN, LIT));
}

void test_switching_channel_restarts_the_debounce() {
    FaultDetector det;
    feed(det, fp.open_fault_cycles - 1, OPEN, LIT);
    assert_code(FaultCode::None, feed(det, fp.open_fault_cycles - 1, LIT, OPEN));
}

void test_reset_clears_the_debounce() {
    FaultDetector det;
    feed(det, fp.open_fault_cycles - 1, OPEN, LIT);
    det.reset();
    assert_code(FaultCode::None, det.update(OPEN, LIT));
}

void test_fault_keeps_being_reported_while_condition_holds() {
    FaultDetector det;
    feed(det, fp.open_fault_cycles, OPEN, LIT);
    assert_code(FaultCode::SensorOpenL, det.update(OPEN, LIT));
}

// --- injection ---

void test_injection_makes_the_channel_read_like_an_open_one() {
    int32_t l = 2000, r = 2000;
    apply_injection(l, r, Injection::L);
    TEST_ASSERT_EQUAL_INT32(fp.injected_mv, l);
    TEST_ASSERT_EQUAL_INT32(2000, r);
}

void test_no_injection_keeps_the_readings() {
    int32_t l = 1500, r = 2500;
    apply_injection(l, r, Injection::None);
    TEST_ASSERT_EQUAL_INT32(1500, l);
    TEST_ASSERT_EQUAL_INT32(2500, r);
}

void test_injected_value_meets_the_open_condition() {
    int32_t l = 2000, r = 2000;
    apply_injection(l, r, Injection::R);
    assert_code(FaultCode::SensorOpenR, open_sensor(l, r));
}

void test_fault_on_injected_channel_is_reported_as_injected() {
    FaultDetector det;
    assert_code(FaultCode::InjectedL,
                feed(det, fp.open_fault_cycles, fp.injected_mv, LIT, Injection::L));
}

void test_injected_right_is_reported_as_injected_right() {
    FaultDetector det;
    assert_code(FaultCode::InjectedR,
                feed(det, fp.open_fault_cycles, LIT, fp.injected_mv, Injection::R));
}

void test_injection_is_debounced_like_a_real_open_sensor() {
    FaultDetector det;
    assert_code(FaultCode::None,
                feed(det, fp.open_fault_cycles - 1, fp.injected_mv, LIT, Injection::L));
}

void test_open_fault_on_the_other_channel_stays_a_real_fault() {
    FaultDetector det;
    assert_code(FaultCode::SensorOpenR, feed(det, fp.open_fault_cycles, LIT, OPEN, Injection::L));
}

// --- self-test ---

void test_selftest_passes_when_both_read_the_floor() {
    assert_code(FaultCode::None, check_selftest(config::ADC_FLOOR_MV, config::ADC_FLOOR_MV));
}

void test_selftest_passes_at_the_short_threshold() {
    assert_code(FaultCode::None, check_selftest(fp.selftest_short_mv, fp.selftest_short_mv));
}

void test_selftest_fails_when_left_is_above_threshold() {
    assert_code(FaultCode::SelftestShort,
                check_selftest(fp.selftest_short_mv + 1, config::ADC_FLOOR_MV));
}

void test_selftest_fails_when_right_is_above_threshold() {
    assert_code(FaultCode::SelftestShort,
                check_selftest(config::ADC_FLOOR_MV, fp.selftest_short_mv + 1));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_left_low_and_right_lit_is_open_left);
    RUN_TEST(test_right_low_and_left_lit_is_open_right);
    RUN_TEST(test_both_channels_low_is_not_open);
    RUN_TEST(test_low_channel_with_dim_other_is_not_open);
    RUN_TEST(test_channel_at_open_threshold_is_not_open);
    RUN_TEST(test_open_condition_shorter_than_debounce_is_no_fault);
    RUN_TEST(test_open_condition_for_debounce_cycles_is_a_fault);
    RUN_TEST(test_open_right_for_debounce_cycles_is_a_fault);
    RUN_TEST(test_one_normal_cycle_restarts_the_debounce);
    RUN_TEST(test_switching_channel_restarts_the_debounce);
    RUN_TEST(test_reset_clears_the_debounce);
    RUN_TEST(test_fault_keeps_being_reported_while_condition_holds);
    RUN_TEST(test_injection_makes_the_channel_read_like_an_open_one);
    RUN_TEST(test_no_injection_keeps_the_readings);
    RUN_TEST(test_injected_value_meets_the_open_condition);
    RUN_TEST(test_fault_on_injected_channel_is_reported_as_injected);
    RUN_TEST(test_injected_right_is_reported_as_injected_right);
    RUN_TEST(test_injection_is_debounced_like_a_real_open_sensor);
    RUN_TEST(test_open_fault_on_the_other_channel_stays_a_real_fault);
    RUN_TEST(test_selftest_passes_when_both_read_the_floor);
    RUN_TEST(test_selftest_passes_at_the_short_threshold);
    RUN_TEST(test_selftest_fails_when_left_is_above_threshold);
    RUN_TEST(test_selftest_fails_when_right_is_above_threshold);
    return UNITY_END();
}

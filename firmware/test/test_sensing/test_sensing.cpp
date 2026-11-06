#include <unity.h>

#include "sensing.h"

using core::apply_r_gain;
using core::pointing_error;
using core::SensingParams;
using core::SunPresence;

static const SensingParams params;

void setUp() {}
void tearDown() {}

void test_equal_light_gives_zero_error() {
    TEST_ASSERT_EQUAL_FLOAT(0.0f, pointing_error(1000, 1000));
}

void test_left_brighter_gives_positive_error() {
    TEST_ASSERT_EQUAL_FLOAT(0.5f, pointing_error(1500, 500));
}

void test_right_brighter_gives_negative_error() {
    TEST_ASSERT_EQUAL_FLOAT(-0.5f, pointing_error(500, 1500));
}

void test_only_one_channel_lit_gives_full_scale_error() {
    TEST_ASSERT_EQUAL_FLOAT(1.0f, pointing_error(2000, 0));
    TEST_ASSERT_EQUAL_FLOAT(-1.0f, pointing_error(0, 2000));
}

void test_sum_below_no_light_level_gives_zero_error() {
    TEST_ASSERT_EQUAL_FLOAT(0.0f, pointing_error(params.no_light_sum_mv - 1, 0));
}

void test_sum_at_no_light_level_gives_an_error() {
    TEST_ASSERT_EQUAL_FLOAT(1.0f, pointing_error(params.no_light_sum_mv, 0));
}

void test_both_channels_zero_does_not_divide_by_zero() {
    SensingParams no_floor;
    no_floor.no_light_sum_mv = 0;
    TEST_ASSERT_EQUAL_FLOAT(0.0f, pointing_error(0, 0, no_floor));
}

void test_r_gain_of_one_keeps_the_reading() {
    SensingParams unity_gain;
    unity_gain.r_gain = 1.0f;
    TEST_ASSERT_EQUAL_INT32(1234, apply_r_gain(1234, unity_gain));
}

void test_r_gain_scales_and_rounds_the_reading() {
    SensingParams gain;
    gain.r_gain = 1.1f;
    TEST_ASSERT_EQUAL_INT32(1100, apply_r_gain(1000, gain));
    TEST_ASSERT_EQUAL_INT32(11, apply_r_gain(10, gain));
    gain.r_gain = 0.5f;
    TEST_ASSERT_EQUAL_INT32(4, apply_r_gain(7, gain));  // 3.5 rounds away from zero
}

void test_sun_starts_absent() {
    TEST_ASSERT_FALSE(SunPresence().present());
}

void test_sun_stays_absent_below_enter_threshold() {
    SunPresence sun;
    TEST_ASSERT_FALSE(sun.update(params.sun_enter_mv - 1));
}

void test_sun_becomes_present_at_enter_threshold() {
    SunPresence sun;
    TEST_ASSERT_TRUE(sun.update(params.sun_enter_mv));
}

void test_sun_stays_present_between_thresholds() {
    SunPresence sun;
    sun.update(params.sun_enter_mv);
    TEST_ASSERT_TRUE(sun.update(params.sun_enter_mv - 1));
    TEST_ASSERT_TRUE(sun.update(params.sun_exit_mv));
}

void test_sun_leaves_below_exit_threshold() {
    SunPresence sun;
    sun.update(params.sun_enter_mv);
    TEST_ASSERT_FALSE(sun.update(params.sun_exit_mv - 1));
}

void test_sun_stays_absent_between_thresholds_after_leaving() {
    SunPresence sun;
    sun.update(params.sun_enter_mv);
    sun.update(params.sun_exit_mv - 1);
    TEST_ASSERT_FALSE(sun.update(params.sun_exit_mv));
    TEST_ASSERT_FALSE(sun.update(params.sun_enter_mv - 1));
}

void test_new_thresholds_apply_to_the_next_update() {
    SunPresence sun;
    sun.set_thresholds(1000, 800);
    TEST_ASSERT_TRUE(sun.update(1000));
    TEST_ASSERT_TRUE(sun.update(800));
    TEST_ASSERT_FALSE(sun.update(799));
}

void test_calibration_adds_the_margins_to_the_room_light() {
    const core::SunCalibration cal = core::calibrate_sun(573);
    TEST_ASSERT_TRUE(cal.ok);
    TEST_ASSERT_EQUAL_INT32(573 + params.sun_enter_margin_mv, cal.enter_mv);
    TEST_ASSERT_EQUAL_INT32(573 + params.sun_exit_margin_mv, cal.exit_mv);
}

void test_calibration_accepts_a_room_up_to_the_limit() {
    const int32_t room = params.sun_enter_max_mv - params.sun_enter_margin_mv;
    TEST_ASSERT_TRUE(core::calibrate_sun(room).ok);
}

void test_calibration_refuses_a_room_above_the_limit() {
    const int32_t room = params.sun_enter_max_mv - params.sun_enter_margin_mv + 1;
    TEST_ASSERT_FALSE(core::calibrate_sun(room).ok);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_equal_light_gives_zero_error);
    RUN_TEST(test_left_brighter_gives_positive_error);
    RUN_TEST(test_right_brighter_gives_negative_error);
    RUN_TEST(test_only_one_channel_lit_gives_full_scale_error);
    RUN_TEST(test_sum_below_no_light_level_gives_zero_error);
    RUN_TEST(test_sum_at_no_light_level_gives_an_error);
    RUN_TEST(test_both_channels_zero_does_not_divide_by_zero);
    RUN_TEST(test_r_gain_of_one_keeps_the_reading);
    RUN_TEST(test_r_gain_scales_and_rounds_the_reading);
    RUN_TEST(test_sun_starts_absent);
    RUN_TEST(test_sun_stays_absent_below_enter_threshold);
    RUN_TEST(test_sun_becomes_present_at_enter_threshold);
    RUN_TEST(test_sun_stays_present_between_thresholds);
    RUN_TEST(test_sun_leaves_below_exit_threshold);
    RUN_TEST(test_sun_stays_absent_between_thresholds_after_leaving);
    RUN_TEST(test_new_thresholds_apply_to_the_next_update);
    RUN_TEST(test_calibration_adds_the_margins_to_the_room_light);
    RUN_TEST(test_calibration_accepts_a_room_up_to_the_limit);
    RUN_TEST(test_calibration_refuses_a_room_above_the_limit);
    return UNITY_END();
}

#include <unity.h>

#include "controller.h"

using core::ControllerParams;
using core::SearchSweep;
using core::SweepParams;
using core::tracking_step;

static const ControllerParams cp;
static const SweepParams sp;
static constexpr float CENTRE_DEG = 90.0f;

void setUp() {}
void tearDown() {}

// Steps until the sweep reaches an end stop; returns the number of steps.
static int run_to_end_stop(SearchSweep& sweep) {
    for (int steps = 1; steps < 1000; ++steps) {
        const float angle = sweep.step();
        if (angle <= sp.angle_min_deg || angle >= sp.angle_max_deg) {
            return steps;
        }
    }
    TEST_FAIL_MESSAGE("sweep never reached an end stop");
    return -1;
}

// --- tracking_step ---

void test_error_inside_deadband_does_not_move() {
    const float e = cp.deadband * 0.99f;
    TEST_ASSERT_EQUAL_FLOAT(CENTRE_DEG, tracking_step(CENTRE_DEG, e));
    TEST_ASSERT_EQUAL_FLOAT(CENTRE_DEG, tracking_step(CENTRE_DEG, -e));
}

void test_error_at_deadband_moves() {
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, CENTRE_DEG + cp.kp * cp.deadband,
                             tracking_step(CENTRE_DEG, cp.deadband));
}

void test_positive_error_increases_angle_by_kp_times_e() {
    const float e = 0.1f;
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, CENTRE_DEG + cp.kp * e, tracking_step(CENTRE_DEG, e));
}

void test_negative_error_decreases_angle() {
    const float e = -0.1f;
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, CENTRE_DEG + cp.kp * e, tracking_step(CENTRE_DEG, e));
}

void test_direction_minus_one_flips_the_step() {
    ControllerParams flipped;
    flipped.direction = -1;
    const float e = 0.1f;
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, CENTRE_DEG - flipped.kp * e,
                             tracking_step(CENTRE_DEG, e, flipped));
}

void test_step_is_clamped_to_max_rate() {
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, CENTRE_DEG + cp.max_step_deg, tracking_step(CENTRE_DEG, 1.0f));
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, CENTRE_DEG - cp.max_step_deg, tracking_step(CENTRE_DEG, -1.0f));
}

void test_angle_is_clamped_to_top_end_stop() {
    TEST_ASSERT_EQUAL_FLOAT(cp.angle_max_deg, tracking_step(cp.angle_max_deg - 1.0f, 1.0f));
}

void test_angle_is_clamped_to_bottom_end_stop() {
    TEST_ASSERT_EQUAL_FLOAT(cp.angle_min_deg, tracking_step(cp.angle_min_deg + 1.0f, -1.0f));
}

void test_waits_at_end_stop_while_light_is_further_out() {
    float angle = cp.angle_max_deg;
    for (int i = 0; i < 10; ++i) {
        angle = tracking_step(angle, 1.0f);
    }
    TEST_ASSERT_EQUAL_FLOAT(cp.angle_max_deg, angle);
}

void test_out_of_range_input_is_brought_inside_end_stops() {
    TEST_ASSERT_EQUAL_FLOAT(cp.angle_max_deg, tracking_step(cp.angle_max_deg + 20.0f, 0.0f));
    TEST_ASSERT_EQUAL_FLOAT(cp.angle_min_deg, tracking_step(cp.angle_min_deg - 20.0f, 0.0f));
}

// --- SearchSweep ---

void test_sweep_from_middle_starts_upwards() {
    SearchSweep sweep;
    sweep.start(CENTRE_DEG);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, CENTRE_DEG + sp.step_deg, sweep.step());
}

void test_sweep_from_top_end_stop_starts_downwards() {
    SearchSweep sweep;
    sweep.start(sp.angle_max_deg);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, sp.angle_max_deg - sp.step_deg, sweep.step());
}

void test_sweep_reverses_at_top_end_stop() {
    SearchSweep sweep;
    sweep.start(CENTRE_DEG);
    run_to_end_stop(sweep);
    TEST_ASSERT_EQUAL_FLOAT(sp.angle_max_deg, sweep.angle_deg());
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, sp.angle_max_deg - sp.step_deg, sweep.step());
}

void test_sweep_reverses_at_bottom_end_stop() {
    SearchSweep sweep;
    sweep.start(sp.angle_max_deg);
    run_to_end_stop(sweep);
    TEST_ASSERT_EQUAL_FLOAT(sp.angle_min_deg, sweep.angle_deg());
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, sp.angle_min_deg + sp.step_deg, sweep.step());
}

void test_partial_first_run_is_not_a_full_sweep() {
    SearchSweep sweep;
    sweep.start(CENTRE_DEG);
    run_to_end_stop(sweep);
    TEST_ASSERT_EQUAL_UINT32(0, sweep.full_sweeps());
}

void test_each_end_to_end_run_counts_as_a_full_sweep() {
    SearchSweep sweep;
    sweep.start(CENTRE_DEG);
    run_to_end_stop(sweep);  // partial
    run_to_end_stop(sweep);
    TEST_ASSERT_EQUAL_UINT32(1, sweep.full_sweeps());
    run_to_end_stop(sweep);
    TEST_ASSERT_EQUAL_UINT32(2, sweep.full_sweeps());
}

void test_sweep_from_an_end_stop_counts_its_first_run() {
    SearchSweep sweep;
    sweep.start(sp.angle_min_deg);
    run_to_end_stop(sweep);
    TEST_ASSERT_EQUAL_UINT32(1, sweep.full_sweeps());
}

void test_start_resets_the_sweep_count() {
    SearchSweep sweep;
    sweep.start(sp.angle_min_deg);
    run_to_end_stop(sweep);
    sweep.start(CENTRE_DEG);
    TEST_ASSERT_EQUAL_UINT32(0, sweep.full_sweeps());
}

void test_sweep_never_leaves_the_end_stops() {
    SearchSweep sweep;
    sweep.start(sp.angle_max_deg + 30.0f);  // out of range input is clamped too
    for (int i = 0; i < 1000; ++i) {
        const float angle = sweep.step();
        TEST_ASSERT_TRUE(angle >= sp.angle_min_deg && angle <= sp.angle_max_deg);
    }
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_error_inside_deadband_does_not_move);
    RUN_TEST(test_error_at_deadband_moves);
    RUN_TEST(test_positive_error_increases_angle_by_kp_times_e);
    RUN_TEST(test_negative_error_decreases_angle);
    RUN_TEST(test_direction_minus_one_flips_the_step);
    RUN_TEST(test_step_is_clamped_to_max_rate);
    RUN_TEST(test_angle_is_clamped_to_top_end_stop);
    RUN_TEST(test_angle_is_clamped_to_bottom_end_stop);
    RUN_TEST(test_waits_at_end_stop_while_light_is_further_out);
    RUN_TEST(test_out_of_range_input_is_brought_inside_end_stops);
    RUN_TEST(test_sweep_from_middle_starts_upwards);
    RUN_TEST(test_sweep_from_top_end_stop_starts_downwards);
    RUN_TEST(test_sweep_reverses_at_top_end_stop);
    RUN_TEST(test_sweep_reverses_at_bottom_end_stop);
    RUN_TEST(test_partial_first_run_is_not_a_full_sweep);
    RUN_TEST(test_each_end_to_end_run_counts_as_a_full_sweep);
    RUN_TEST(test_sweep_from_an_end_stop_counts_its_first_run);
    RUN_TEST(test_start_resets_the_sweep_count);
    RUN_TEST(test_sweep_never_leaves_the_end_stops);
    return UNITY_END();
}

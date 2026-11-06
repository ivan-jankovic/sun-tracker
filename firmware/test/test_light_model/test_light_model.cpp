#include <unity.h>

#include "light_model.h"

using namespace core;

static const LightParams lp = dark_room();

void setUp() {}
void tearDown() {}

void test_centred_light_lights_both_channels_equally() {
    const SensorReading r = light_reading(90.0f, 90.0f, true, lp);
    TEST_ASSERT_INT32_WITHIN(1, lround(lp.ambient_l_mv + lp.intensity_mv / 2), r.l_mv);
    TEST_ASSERT_INT32_WITHIN(1, lround(lp.ambient_r_mv + lp.intensity_mv / 2), r.r_mv);
}

void test_light_at_a_higher_angle_lights_the_left_channel_more() {
    const SensorReading r = light_reading(100.0f, 90.0f, true, lp);
    TEST_ASSERT_GREATER_THAN_INT32(r.r_mv, r.l_mv);
}

void test_beyond_half_the_shadow_width_the_far_channel_sees_only_room_light() {
    const SensorReading r = light_reading(90.0f + lp.shadow_width_deg / 2 + 1, 90.0f, true, lp);
    TEST_ASSERT_INT32_WITHIN(1, lround(lp.ambient_r_mv), r.r_mv);
}

void test_light_behind_the_platform_adds_nothing() {
    const SensorReading r = light_reading(-90.0f, 90.0f, true, lp);
    TEST_ASSERT_INT32_WITHIN(1, lround(lp.ambient_l_mv), r.l_mv);
    TEST_ASSERT_INT32_WITHIN(1, lround(lp.ambient_r_mv), r.r_mv);
}

void test_torch_off_leaves_only_room_light() {
    const SensorReading r = light_reading(90.0f, 90.0f, false, lp);
    TEST_ASSERT_INT32_WITHIN(1, lround(lp.ambient_l_mv), r.l_mv);
}

void test_sum_falls_with_the_cosine_of_the_angle() {
    const SensorReading r = light_reading(150.0f, 90.0f, true, lp);  // 60 deg off: cos = 0.5
    TEST_ASSERT_INT32_WITHIN(2, lround(lp.ambient_l_mv + lp.ambient_r_mv + lp.intensity_mv / 2),
                             r.l_mv + r.r_mv);
}

void test_readings_are_clamped_to_the_adc_range() {
    LightParams p = lp;
    p.ambient_l_mv = 0.0f;
    p.ambient_r_mv = 0.0f;
    p.intensity_mv = 100000.0f;
    const SensorReading behind = light_reading(-90.0f, 90.0f, true, p);
    TEST_ASSERT_EQUAL_INT32(p.floor_mv, behind.l_mv);
    const SensorReading blinding = light_reading(90.0f, 90.0f, true, p);
    TEST_ASSERT_EQUAL_INT32(p.max_mv, blinding.l_mv);
}

void test_angle_difference_wraps_around() {
    const SensorReading a = light_reading(-170.0f, 170.0f, true, lp);  // 20 deg apart
    const SensorReading b = light_reading(70.0f, 50.0f, true, lp);     // the same 20 deg
    TEST_ASSERT_EQUAL_INT32(b.l_mv, a.l_mv);
    TEST_ASSERT_EQUAL_INT32(b.r_mv, a.r_mv);
}

void test_rng_repeats_for_the_same_seed() {
    Rng a(42), b(42);
    for (int i = 0; i < 100; ++i) TEST_ASSERT_EQUAL_UINT32(a.next(), b.next());
}

void test_rng_noise_stays_within_its_width() {
    Rng rng(7);
    for (int i = 0; i < 10000; ++i) {
        const float n = rng.uniform(5.0f);
        TEST_ASSERT_TRUE(n >= -5.0f && n <= 5.0f);
    }
}

void test_plant_reads_the_model_at_the_commanded_angle() {
    SimPlant plant(lp);
    plant.params.noise_mv = 0.0f;
    plant.set_sun_deg(120.0f);
    plant.command(110.0f);
    const SensorReading expected = light_reading(120.0f, 110.0f, true, lp);
    const SensorReading r = plant.read();
    TEST_ASSERT_EQUAL_INT32(expected.l_mv, r.l_mv);
    TEST_ASSERT_EQUAL_INT32(expected.r_mv, r.r_mv);
}

void test_plant_open_channel_reads_exactly_the_floor() {
    SimPlant plant(lp);
    plant.open_r = true;
    TEST_ASSERT_EQUAL_INT32(lp.floor_mv, plant.read().r_mv);
}

void test_plant_supply_off_reads_the_floor_unless_shorted() {
    SimPlant plant(lp);
    TEST_ASSERT_EQUAL_INT32(lp.floor_mv, plant.read_supply_off().l_mv);
    plant.short_l = true;
    TEST_ASSERT_EQUAL_INT32(lp.max_mv, plant.read_supply_off().l_mv);
}

void test_plant_records_the_range_of_commanded_angles() {
    SimPlant plant(lp);
    plant.command(20.0f);
    plant.command(160.0f);
    TEST_ASSERT_EQUAL_FLOAT(20.0f, plant.min_angle_deg());
    TEST_ASSERT_EQUAL_FLOAT(160.0f, plant.max_angle_deg());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_centred_light_lights_both_channels_equally);
    RUN_TEST(test_light_at_a_higher_angle_lights_the_left_channel_more);
    RUN_TEST(test_beyond_half_the_shadow_width_the_far_channel_sees_only_room_light);
    RUN_TEST(test_light_behind_the_platform_adds_nothing);
    RUN_TEST(test_torch_off_leaves_only_room_light);
    RUN_TEST(test_sum_falls_with_the_cosine_of_the_angle);
    RUN_TEST(test_readings_are_clamped_to_the_adc_range);
    RUN_TEST(test_angle_difference_wraps_around);
    RUN_TEST(test_rng_repeats_for_the_same_seed);
    RUN_TEST(test_rng_noise_stays_within_its_width);
    RUN_TEST(test_plant_reads_the_model_at_the_commanded_angle);
    RUN_TEST(test_plant_open_channel_reads_exactly_the_floor);
    RUN_TEST(test_plant_supply_off_reads_the_floor_unless_shorted);
    RUN_TEST(test_plant_records_the_range_of_commanded_angles);
    return UNITY_END();
}

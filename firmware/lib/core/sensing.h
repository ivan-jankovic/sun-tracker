// Turns two channel readings into a pointing error and a sun-present flag.
#pragma once

#include <cstdint>

#include "config.h"

namespace core {

struct SensingParams {
    float r_gain = config::R_GAIN;
    int32_t no_light_sum_mv = config::NO_LIGHT_SUM_MV;
    int32_t sun_enter_mv = config::SUN_ENTER_MV;
    int32_t sun_exit_mv = config::SUN_EXIT_MV;
    int32_t sun_enter_margin_mv = config::SUN_ENTER_MARGIN_MV;
    int32_t sun_exit_margin_mv = config::SUN_EXIT_MARGIN_MV;
    int32_t sun_enter_max_mv = config::SUN_ENTER_MAX_MV;
};

// Cancels the mismatch between the photoresistors: with both equally lit, L = R.
int32_t apply_r_gain(int32_t r_raw_mv, const SensingParams& params = {});

// e = (L - R) / (L + R), in -1..+1. Positive: left is brighter, light is to the left.
// 0 when the sum is below the no-light level, because there is no direction to follow.
float pointing_error(int32_t l_mv, int32_t r_mv, const SensingParams& params = {});

// Sun present from L + R, with hysteresis so the mode does not flicker at the threshold.
class SunPresence {
public:
    explicit SunPresence(const SensingParams& params = {})
        : enter_mv_(params.sun_enter_mv), exit_mv_(params.sun_exit_mv) {}

    bool update(int32_t sum_mv);
    bool present() const { return present_; }

    void set_thresholds(int32_t enter_mv, int32_t exit_mv);
    int32_t enter_mv() const { return enter_mv_; }
    int32_t exit_mv() const { return exit_mv_; }

private:
    int32_t enter_mv_;
    int32_t exit_mv_;
    bool present_ = false;
};

struct SunCalibration {
    bool ok = false;  // false: the room is too bright, keep the old thresholds
    int32_t enter_mv = 0;
    int32_t exit_mv = 0;
};

// CAL: the thresholds sit a fixed margin above the room light, measured with the torch off.
SunCalibration calibrate_sun(int32_t room_sum_mv, const SensingParams& params = {});

}  // namespace core

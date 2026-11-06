#include "sensing.h"

#include <cmath>

namespace core {

int32_t apply_r_gain(int32_t r_raw_mv, const SensingParams& params) {
    return static_cast<int32_t>(std::lround(static_cast<float>(r_raw_mv) * params.r_gain));
}

float pointing_error(int32_t l_mv, int32_t r_mv, const SensingParams& params) {
    const int32_t sum_mv = l_mv + r_mv;
    if (sum_mv < params.no_light_sum_mv || sum_mv <= 0) {
        return 0.0f;
    }
    return static_cast<float>(l_mv - r_mv) / static_cast<float>(sum_mv);
}

bool SunPresence::update(int32_t sum_mv) {
    if (present_) {
        present_ = sum_mv >= exit_mv_;
    } else {
        present_ = sum_mv >= enter_mv_;
    }
    return present_;
}

void SunPresence::set_thresholds(int32_t enter_mv, int32_t exit_mv) {
    enter_mv_ = enter_mv;
    exit_mv_ = exit_mv;
}

SunCalibration calibrate_sun(int32_t room_sum_mv, const SensingParams& params) {
    SunCalibration cal;
    cal.enter_mv = room_sum_mv + params.sun_enter_margin_mv;
    cal.exit_mv = room_sum_mv + params.sun_exit_margin_mv;
    cal.ok = cal.enter_mv <= params.sun_enter_max_mv;
    return cal;
}

}  // namespace core

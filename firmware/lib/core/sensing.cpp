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
        present_ = sum_mv >= params_.sun_exit_mv;
    } else {
        present_ = sum_mv >= params_.sun_enter_mv;
    }
    return present_;
}

}  // namespace core

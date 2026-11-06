#include "controller.h"

#include <algorithm>
#include <cmath>

namespace core {

float tracking_step(float angle_deg, float e, const ControllerParams& params) {
    float target_deg = angle_deg;
    if (std::fabs(e) >= params.deadband) {
        const float step_deg = std::clamp(params.direction * params.kp * e,
                                          -params.max_step_deg, params.max_step_deg);
        target_deg += step_deg;
    }
    return std::clamp(target_deg, params.angle_min_deg, params.angle_max_deg);
}

void SearchSweep::start(float angle_deg) {
    angle_deg_ = std::clamp(angle_deg, params_.angle_min_deg, params_.angle_max_deg);
    direction_ = angle_deg_ >= params_.angle_max_deg ? -1 : 1;
    left_from_end_stop_ =
        angle_deg_ <= params_.angle_min_deg || angle_deg_ >= params_.angle_max_deg;
    full_sweeps_ = 0;
}

float SearchSweep::step() {
    angle_deg_ = std::clamp(angle_deg_ + direction_ * params_.step_deg,
                            params_.angle_min_deg, params_.angle_max_deg);

    const bool at_end_stop = direction_ > 0 ? angle_deg_ >= params_.angle_max_deg
                                            : angle_deg_ <= params_.angle_min_deg;
    if (at_end_stop) {
        if (left_from_end_stop_) {
            ++full_sweeps_;
        }
        left_from_end_stop_ = true;
        direction_ = -direction_;
    }
    return angle_deg_;
}

}  // namespace core

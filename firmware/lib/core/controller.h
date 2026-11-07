// Where to point next: the TRACKING controller and the SEARCH sweep.
#pragma once

#include <cstdint>

#include "config.h"

namespace core {

struct ControllerParams {
    float deadband = config::DEADBAND;
    float kp = config::KP;
    float max_step_deg = config::MAX_STEP_DEG;
    int direction = config::DIRECTION;
    float angle_min_deg = config::ANGLE_MIN_DEG;
    float angle_max_deg = config::ANGLE_MAX_DEG;
};

// One TRACKING cycle: dead band, P step, rate clamp, end stop clamp.
// The result never lies outside the end stops, whatever the input.
float tracking_step(float angle_deg, float e, const ControllerParams& params = {});

// One cycle towards target_deg, moving at most max_step_deg, without overshooting.
// SAFE parks with it, so the servo never sees a large jump: on the USB supply, the
// rail sags to about 4.1 V under load and an SG90 then crawls through big moves.
float slew_step(float angle_deg, float target_deg, float max_step_deg);

struct SweepParams {
    float step_deg = config::SEARCH_STEP_DEG;
    float angle_min_deg = config::ANGLE_MIN_DEG;
    float angle_max_deg = config::ANGLE_MAX_DEG;
};

// Sweeps between the end stops and counts full sweeps (end stop to end stop).
// The partial run from the start angle to the first end stop does not count.
class SearchSweep {
public:
    explicit SearchSweep(const SweepParams& params = {}) : params_(params) {}

    // Starts upwards, or downwards if already at the top end stop.
    void start(float angle_deg);
    float step();

    float angle_deg() const { return angle_deg_; }
    uint32_t full_sweeps() const { return full_sweeps_; }

private:
    SweepParams params_;
    float angle_deg_ = config::ANGLE_PARK_DEG;
    int direction_ = 1;
    bool left_from_end_stop_ = false;
    uint32_t full_sweeps_ = 0;
};

}  // namespace core

#include "faults.h"

namespace core {

void apply_injection(int32_t& l_mv, int32_t& r_mv, Injection injection,
                     const FaultParams& params) {
    if (injection == Injection::L) {
        l_mv = params.injected_mv;
    } else if (injection == Injection::R) {
        r_mv = params.injected_mv;
    }
}

FaultCode open_sensor(int32_t l_mv, int32_t r_mv, const FaultParams& params) {
    if (l_mv < params.open_mv && r_mv > params.open_other_min_mv) {
        return FaultCode::SensorOpenL;
    }
    if (r_mv < params.open_mv && l_mv > params.open_other_min_mv) {
        return FaultCode::SensorOpenR;
    }
    return FaultCode::None;
}

FaultCode check_selftest(int32_t l_off_mv, int32_t r_off_mv, const FaultParams& params) {
    if (l_off_mv > params.selftest_short_mv || r_off_mv > params.selftest_short_mv) {
        return FaultCode::SelftestShort;
    }
    return FaultCode::None;
}

FaultCode FaultDetector::update(int32_t l_mv, int32_t r_mv, Injection injection) {
    const FaultCode now = open_sensor(l_mv, r_mv, params_);
    open_l_cycles_ = now == FaultCode::SensorOpenL ? open_l_cycles_ + 1 : 0;
    open_r_cycles_ = now == FaultCode::SensorOpenR ? open_r_cycles_ + 1 : 0;

    if (open_l_cycles_ >= params_.open_fault_cycles) {
        return injection == Injection::L ? FaultCode::InjectedL : FaultCode::SensorOpenL;
    }
    if (open_r_cycles_ >= params_.open_fault_cycles) {
        return injection == Injection::R ? FaultCode::InjectedR : FaultCode::SensorOpenR;
    }
    return FaultCode::None;
}

void FaultDetector::reset() {
    open_l_cycles_ = 0;
    open_r_cycles_ = 0;
}

}  // namespace core

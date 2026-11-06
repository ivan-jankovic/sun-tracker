// Fault detection: the open-sensor rule with its debounce, the one-shot checks
// run at boot and on CLEAR, and fault injection.
#pragma once

#include <cstdint>

#include "config.h"

namespace core {

enum class FaultCode : uint8_t {
    None,
    SelftestShort,
    SensorOpenL,
    SensorOpenR,
    InjectedL,
    InjectedR,
};

enum class Injection : uint8_t { None, L, R };

struct FaultParams {
    int32_t open_mv = config::OPEN_MV;
    int32_t open_other_min_mv = config::OPEN_OTHER_MIN_MV;
    uint32_t open_fault_cycles = config::OPEN_FAULT_CYCLES;
    int32_t selftest_short_mv = config::SELFTEST_SHORT_MV;
    int32_t injected_mv = config::ADC_FLOOR_MV;  // what a really open channel reads
};

// An injected channel reads like a really open one, so it goes through the same detection.
void apply_injection(int32_t& l_mv, int32_t& r_mv, Injection injection,
                     const FaultParams& params = {});

// The open-sensor condition for one cycle, without debounce: one channel below
// OPEN_MV while the other is above OPEN_OTHER_MIN_MV. Uniform darkness cannot
// cause it, because both channels would be low.
FaultCode open_sensor(int32_t l_mv, int32_t r_mv, const FaultParams& params = {});

// Readings taken with the sensor supply off. A healthy channel reads the ADC floor.
FaultCode check_selftest(int32_t l_off_mv, int32_t r_off_mv, const FaultParams& params = {});

// The open-sensor condition, debounced: reports a fault only after it held for
// OPEN_FAULT_CYCLES cycles in a row on the same channel. A fault on the injected
// channel is reported as INJECTED_L or INJECTED_R.
class FaultDetector {
public:
    explicit FaultDetector(const FaultParams& params = {}) : params_(params) {}

    FaultCode update(int32_t l_mv, int32_t r_mv, Injection injection = Injection::None);
    void reset();

private:
    FaultParams params_;
    uint32_t open_l_cycles_ = 0;
    uint32_t open_r_cycles_ = 0;
};

}  // namespace core

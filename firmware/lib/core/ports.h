// The core's only contact with the outside world. The board, the simulation and
// the tests each implement these, so all three run the same core code.
#pragma once

#include <cstdint>

namespace core {

struct SensorReading {
    int32_t l_mv = 0;
    int32_t r_mv = 0;
};

class SensorPort {
public:
    // Both channels, averaged, with the sensor supply on.
    virtual SensorReading read() = 0;
    // Both channels with the supply briefly off, for the self-test. The supply is on
    // again when this returns.
    virtual SensorReading read_supply_off() = 0;

protected:
    ~SensorPort() = default;  // never deleted through the interface
};

class ServoPort {
public:
    virtual void command(float angle_deg) = 0;

protected:
    ~ServoPort() = default;
};

class LineSink {
public:
    virtual void write_line(const char* line) = 0;  // without a line end

protected:
    ~LineSink() = default;
};

// Only the esp32dev-sim build and the simulation have one.
class SimControl {
public:
    virtual void set_sun_deg(float sun_deg) = 0;
    virtual float sun_deg() const = 0;

protected:
    ~SimControl() = default;
};

}  // namespace core

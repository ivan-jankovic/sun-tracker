// A model of the light and the platform, for the SIL scenarios and the
// esp32dev-sim build. With d = sun_deg - angle_deg:
//   visible = max(0, cos d)
//   L = ambient_l + intensity * visible * clamp(0.5 + d / W, 0, 1) + noise
//   R = ambient_r + intensity * visible * clamp(0.5 - d / W, 0, 1) + noise
// clamped to the ADC range. The model adds light in millivolts, so it ignores the
// photoresistor's non-linearity: in bright rooms the real sensor responds less.
#pragma once

#include <cstdint>

#include "config.h"
#include "ports.h"

namespace core {

struct LightParams {
    float ambient_l_mv = 283.0f;   // room light per channel; measured in a dark room
    float ambient_r_mv = 290.0f;
    float intensity_mv = 3400.0f;  // what the torch adds to L + R, pointing straight at it
    float shadow_width_deg = 30.0f;  // W: the width of the wall's shadow
    float noise_mv = 5.0f;           // uniform, +-
    int32_t floor_mv = config::ADC_FLOOR_MV;
    int32_t max_mv = 3300;
};

// Measured room presets.
LightParams dark_room();    // room 573 mV, torch +3400 mV
LightParams bright_room();  // room 3454 mV, torch +1160 mV

// The model without noise, clamped to the ADC range.
SensorReading light_reading(float sun_deg, float angle_deg, bool torch_on,
                            const LightParams& params);

// xorshift32: small, and the same sequence on the host and the ESP32, so a
// failing scenario repeats exactly.
class Rng {
public:
    explicit Rng(uint32_t seed) : state_(seed != 0 ? seed : 1) {}
    uint32_t next();
    float uniform(float half_width);  // in -half_width..+half_width

private:
    uint32_t state_;
};

// The platform and its light as ports for the core: the servo command moves the
// platform (the commanded angle is taken as the actual one, as on the SG90), and
// the sensors read the model at that angle. Faults are switched on by the test.
class SimPlant : public SensorPort, public ServoPort, public SimControl {
public:
    explicit SimPlant(const LightParams& light = {}, uint32_t seed = 1)
        : params(light), rng_(seed) {}

    SensorReading read() override;
    SensorReading read_supply_off() override;
    void command(float angle_deg) override;
    void set_sun_deg(float sun_deg) override { sun_deg_ = sun_deg; }
    float sun_deg() const override { return sun_deg_; }

    float angle_deg() const { return angle_deg_; }
    float min_angle_deg() const { return min_angle_deg_; }
    float max_angle_deg() const { return max_angle_deg_; }

    bool torch_on = true;
    bool open_l = false;    // photoresistor removed: the channel reads the floor
    bool open_r = false;
    bool short_l = false;   // midpoint touching a 3.3 V supply other than D25
    LightParams params;     // may be changed while running, e.g. to dim the room

private:
    Rng rng_;
    float sun_deg_ = 90.0f;
    float angle_deg_ = config::ANGLE_PARK_DEG;
    float min_angle_deg_ = config::ANGLE_PARK_DEG;
    float max_angle_deg_ = config::ANGLE_PARK_DEG;
};

}  // namespace core

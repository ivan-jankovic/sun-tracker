#include "light_model.h"

#include <algorithm>
#include <cmath>

namespace core {

namespace {

constexpr float DEG_TO_RAD = 3.14159265f / 180.0f;

// Angle difference wrapped to -180..180, so a light behind the platform stays behind it.
float wrap_deg(float deg) {
    deg = std::fmod(deg + 180.0f, 360.0f);
    if (deg < 0.0f) deg += 360.0f;
    return deg - 180.0f;
}

int32_t to_adc(float mv, const LightParams& p) {
    const float clamped = std::clamp(mv, static_cast<float>(p.floor_mv), static_cast<float>(p.max_mv));
    return static_cast<int32_t>(std::lround(clamped));
}

struct Channels {
    float l_mv;
    float r_mv;
};

Channels model(float sun_deg, float angle_deg, bool torch_on, const LightParams& p) {
    const float d = wrap_deg(sun_deg - angle_deg);
    const float visible = torch_on ? std::max(0.0f, std::cos(d * DEG_TO_RAD)) : 0.0f;
    const float lit = p.intensity_mv * visible;
    return {p.ambient_l_mv + lit * std::clamp(0.5f + d / p.shadow_width_deg, 0.0f, 1.0f),
            p.ambient_r_mv + lit * std::clamp(0.5f - d / p.shadow_width_deg, 0.0f, 1.0f)};
}

}  // namespace

LightParams dark_room() { return LightParams{}; }

LightParams bright_room() {
    LightParams p;
    // The real room read 2166 / 1288 mV, with the light coming from the left. This additive
    // model would turn that into a much larger pointing bias than the real sensor showed,
    // because the real sensor responds less in bright light. So the 3454 mV are spread evenly.
    p.ambient_l_mv = 1727.0f;
    p.ambient_r_mv = 1727.0f;
    p.intensity_mv = 1160.0f;
    p.noise_mv = 15.0f;
    return p;
}

SensorReading light_reading(float sun_deg, float angle_deg, bool torch_on,
                            const LightParams& params) {
    const Channels c = model(sun_deg, angle_deg, torch_on, params);
    return {to_adc(c.l_mv, params), to_adc(c.r_mv, params)};
}

uint32_t Rng::next() {
    state_ ^= state_ << 13;
    state_ ^= state_ >> 17;
    state_ ^= state_ << 5;
    return state_;
}

float Rng::uniform(float half_width) {
    const float unit = static_cast<float>(next()) / 4294967296.0f;  // 0..1
    return (2.0f * unit - 1.0f) * half_width;
}

SensorReading SimPlant::read() {
    const Channels c = model(sun_deg_, angle_deg_, torch_on, params);
    SensorReading r{to_adc(c.l_mv + rng_.uniform(params.noise_mv), params),
                    to_adc(c.r_mv + rng_.uniform(params.noise_mv), params)};
    if (short_l) r.l_mv = params.max_mv;
    if (open_l) r.l_mv = params.floor_mv;
    if (open_r) r.r_mv = params.floor_mv;
    return r;
}

SensorReading SimPlant::read_supply_off() {
    // Healthy and open channels both read the floor with the supply off.
    return {short_l ? params.max_mv : params.floor_mv, params.floor_mv};
}

void SimPlant::command(float angle_deg) {
    angle_deg_ = angle_deg;
    min_angle_deg_ = std::min(min_angle_deg_, angle_deg);
    max_angle_deg_ = std::max(max_angle_deg_, angle_deg);
}

}  // namespace core

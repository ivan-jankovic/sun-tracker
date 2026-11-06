#include "board_servo.h"

#include <Arduino.h>

#include <algorithm>
#include <cmath>

#include "config.h"

namespace {

constexpr float PERIOD_US = 1e6f / config::SERVO_PWM_HZ;  // 20000 us at 50 Hz
constexpr float FULL_SCALE = static_cast<float>(1UL << config::SERVO_PWM_BITS);

}  // namespace

bool BoardServo::begin() {
    return ledcAttach(config::PIN_SERVO, config::SERVO_PWM_HZ, config::SERVO_PWM_BITS);
}

// Linear from SERVO_MIN_US at 0 deg to SERVO_MAX_US at 180 deg. The core already
// keeps the angle inside the soft end stops; the clamp here only protects the servo.
void BoardServo::command(float angle_deg) {
    const float angle = std::clamp(angle_deg, 0.0f, 180.0f);
    const float pulse_us = config::SERVO_MIN_US +
                           (config::SERVO_MAX_US - config::SERVO_MIN_US) * angle / 180.0f;
    ledcWrite(config::PIN_SERVO, static_cast<uint32_t>(std::lround(pulse_us / PERIOD_US * FULL_SCALE)));
}

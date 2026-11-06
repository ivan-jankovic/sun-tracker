// Pins, rates, thresholds and calibration. Values marked (cal) are starting
// points, to be calibrated on the hardware. See docs/design.md.
#pragma once

#include <cstdint>

namespace config {

// Pins (ESP32 DevKit V1)
inline constexpr uint8_t PIN_SENSOR_SUPPLY = 25;  // HIGH = 3.3 V to the photoresistors
inline constexpr uint8_t PIN_SENSOR_L = 34;       // ADC1, input-only
inline constexpr uint8_t PIN_SENSOR_R = 35;       // ADC1, input-only
inline constexpr uint8_t PIN_SERVO = 26;          // 50 Hz PWM, 3.3 V logic

// Timing
inline constexpr uint32_t CYCLE_MS = 50;              // control cycle, 20 Hz
inline constexpr uint32_t TELEMETRY_EVERY_CYCLES = 2;  // TM at 10 Hz
inline constexpr uint32_t SERIAL_BAUD = 115200;

// Sensing
inline constexpr uint32_t ADC_SAMPLES = 8;  // averaged per channel, per cycle
// Lowest reading analogReadMilliVolts() returns: 0 V reads as this (raw 0).
// Measured with D34 and D35 tied to GND, 12-bit, 11 dB. Re-measure for another board.
inline constexpr int32_t ADC_FLOOR_MV = 142;
inline constexpr float R_GAIN = 1.0f;  // (cal) right channel mismatch
inline constexpr int32_t NO_LIGHT_SUM_MV = 2 * ADC_FLOOR_MV + 100;  // below this L + R, e is 0
inline constexpr int32_t SUN_ENTER_MV = 3600;    // (cal) L + R to declare sun present
inline constexpr int32_t SUN_EXIT_MV = 3000;     // (cal) L + R below this: sun absent

// Actuator
inline constexpr uint32_t SERVO_PWM_HZ = 50;
inline constexpr uint8_t SERVO_PWM_BITS = 16;
inline constexpr uint32_t SERVO_MIN_US = 500;   // (cal) pulse width at 0 deg
inline constexpr uint32_t SERVO_MAX_US = 2400;  // (cal) pulse width at 180 deg
inline constexpr float ANGLE_MIN_DEG = 10.0f;   // (cal) soft end stop
inline constexpr float ANGLE_MAX_DEG = 170.0f;  // (cal) soft end stop
inline constexpr float ANGLE_PARK_DEG = 90.0f;  // boot and SAFE position

// Modes
inline constexpr float SEARCH_RATE_DEG_S = 45.0f;
inline constexpr uint32_t SEARCH_MAX_SWEEPS = 3;
inline constexpr uint32_t SUN_FOUND_CYCLES = 3;
inline constexpr uint32_t SUN_LOST_CYCLES = 10;

// Controller
inline constexpr float DEADBAND = 0.04f;  // (cal) |e| below this: no motion
inline constexpr float KP = 10.0f;        // (cal) degrees per unit of e, per cycle
inline constexpr float MAX_RATE_DEG_S = 90.0f;
inline constexpr int DIRECTION = 1;  // -1 if the servo is mounted the other way round

// Fault detection
// With the supply off, above this: a short. A healthy channel reads the floor.
inline constexpr int32_t SELFTEST_SHORT_MV = ADC_FLOOR_MV + 100;
// (cal) Below this, a channel may be open (an open one reads the floor). Must stay
// below the darkest reading of a shaded but connected sensor, torch from the side:
// 374 mV measured in a dark room, 1215 mV in bright room light.
inline constexpr int32_t OPEN_MV = ADC_FLOOR_MV + 60;
inline constexpr int32_t OPEN_OTHER_MIN_MV = 500;  // only if the other channel is above this
inline constexpr uint32_t OPEN_FAULT_CYCLES = 20;

// Telecommands
inline constexpr uint32_t COMMAND_MAX_CHARS = 32;  // longer lines are refused as unknown
inline constexpr float SIM_SUN_MIN_DEG = -180.0f;  // -90 puts the light behind the platform
inline constexpr float SIM_SUN_MAX_DEG = 180.0f;

// Derived per-cycle steps, so the rates above stay the only source of truth.
inline constexpr float MAX_STEP_DEG = MAX_RATE_DEG_S * CYCLE_MS / 1000.0f;
inline constexpr float SEARCH_STEP_DEG = SEARCH_RATE_DEG_S * CYCLE_MS / 1000.0f;

static_assert(SUN_EXIT_MV < SUN_ENTER_MV, "sun hysteresis needs exit below enter");
static_assert(ANGLE_MIN_DEG < ANGLE_PARK_DEG && ANGLE_PARK_DEG < ANGLE_MAX_DEG,
              "park position must lie between the end stops");
static_assert(DIRECTION == 1 || DIRECTION == -1, "DIRECTION is a sign");
static_assert(ADC_FLOOR_MV < OPEN_MV && OPEN_MV < OPEN_OTHER_MIN_MV,
              "an open channel (floor) must read below OPEN_MV, a lit one above");
static_assert(ADC_FLOOR_MV < SELFTEST_SHORT_MV, "a healthy self-test reads the floor");

}  // namespace config

#include "board_sensors.h"

#include <Arduino.h>

#include "config.h"

namespace {

int32_t average_mv(uint8_t pin) {
    uint32_t sum = 0;
    for (uint32_t i = 0; i < config::ADC_SAMPLES; ++i) sum += analogReadMilliVolts(pin);
    return static_cast<int32_t>((sum + config::ADC_SAMPLES / 2) / config::ADC_SAMPLES);
}

}  // namespace

void BoardSensors::begin() {
    analogReadResolution(12);
    analogSetPinAttenuation(config::PIN_SENSOR_L, ADC_11db);
    analogSetPinAttenuation(config::PIN_SENSOR_R, ADC_11db);
    pinMode(config::PIN_SENSOR_SUPPLY, OUTPUT);
    digitalWrite(config::PIN_SENSOR_SUPPLY, HIGH);
    delay(config::SUPPLY_SETTLE_MS);
}

core::SensorReading BoardSensors::read() {
    return {average_mv(config::PIN_SENSOR_L), average_mv(config::PIN_SENSOR_R)};
}

core::SensorReading BoardSensors::read_supply_off() {
    digitalWrite(config::PIN_SENSOR_SUPPLY, LOW);
    delay(config::SUPPLY_SETTLE_MS);
    const core::SensorReading r = read();
    digitalWrite(config::PIN_SENSOR_SUPPLY, HIGH);
    delay(config::SUPPLY_SETTLE_MS);  // CLEAR reads again straight after this
    return r;
}

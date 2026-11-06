// Arduino glue: builds the ports, hands them to the core, and runs the control
// cycle every CYCLE_MS. All decisions are in lib/core; this file only connects.
#include <Arduino.h>

#include "board_servo.h"
#include "config.h"
#include "core.h"
#include "serial_link.h"

#ifdef SIM_SUN
#include "light_model.h"
#else
#include "board_sensors.h"
#endif

namespace {

BoardServo servo;
SerialLink serial;

#ifdef SIM_SUN
// The simulated light reads the model at the commanded angle, and the real servo
// still moves, so the platform shows what the simulation does.
class BothServos : public core::ServoPort {
public:
    BothServos(core::ServoPort& a, core::ServoPort& b) : a_(a), b_(b) {}
    void command(float angle_deg) override {
        a_.command(angle_deg);
        b_.command(angle_deg);
    }

private:
    core::ServoPort& a_;
    core::ServoPort& b_;
};

core::SimPlant plant(core::dark_room());
BothServos servos(servo, plant);
core::Core flight(plant, servos, serial, &plant);
#else
BoardSensors sensors;
core::Core flight(sensors, servo, serial);
#endif

uint32_t next_cycle_ms = 0;

}  // namespace

void setup() {
    serial.begin();
#ifdef SIM_SUN
    serial.log("sun-tracker esp32dev-sim: simulated light, dark room");
#else
    sensors.begin();
    serial.log("sun-tracker esp32dev");
#endif
    if (!servo.begin()) serial.log("servo PWM setup failed");

    flight.boot(millis());
    next_cycle_ms = millis() + config::CYCLE_MS;
}

void loop() {
    std::string_view line;
    while (serial.read_line(line)) flight.handle_line(millis(), line);

    const uint32_t now = millis();
    // Signed difference, so the comparison still works when millis() wraps after 49 days.
    if (static_cast<int32_t>(now - next_cycle_ms) < 0) return;

    flight.step(now);
    next_cycle_ms += config::CYCLE_MS;
    // If a cycle overran by more than a whole cycle, skip ahead instead of running
    // the missed cycles in a burst.
    if (static_cast<int32_t>(now - next_cycle_ms) >= 0) next_cycle_ms = now + config::CYCLE_MS;
}

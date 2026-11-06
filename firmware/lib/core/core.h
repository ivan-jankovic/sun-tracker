// The core cycle: connects sensing, the controller, fault detection, the mode
// manager and the protocol. The Arduino glue calls boot() once, step() every
// control cycle and handle_line() for each received line; nothing else.
#pragma once

#include <cstdint>
#include <string_view>

#include "commands.h"
#include "config.h"
#include "controller.h"
#include "faults.h"
#include "modes.h"
#include "ports.h"
#include "protocol.h"
#include "sensing.h"

namespace core {

struct CoreParams {
    SensingParams sensing;
    ControllerParams controller;
    SweepParams sweep;
    FaultParams faults;
    ModeParams modes;
    CommandParams commands;
    float park_deg = config::ANGLE_PARK_DEG;
    uint32_t telemetry_every_cycles = config::TELEMETRY_EVERY_CYCLES;
};

class Core {
public:
    Core(SensorPort& sensors, ServoPort& servo, LineSink& sink, SimControl* sim = nullptr,
         const CoreParams& params = {});

    // Commands the park angle, runs the self-test and leaves BOOT.
    void boot(uint32_t t_ms);
    // One control cycle. Does nothing before boot().
    void step(uint32_t t_ms);
    // One received line. Sends exactly one ACK or NACK, then any events it caused.
    void handle_line(uint32_t t_ms, std::string_view line);

    Mode mode() const { return modes_.mode(); }
    float angle_deg() const { return angle_deg_; }
    FaultCode fault() const { return fault_; }
    Injection injection() const { return injection_; }
    const SunPresence& sun() const { return sun_; }

private:
    SensorReading measure();
    void act(float e);
    void enter(const Transition& t, uint32_t t_ms);
    void raise_fault(FaultCode code, uint32_t t_ms);
    FaultCode run_checks();

    void reply(const CommandOutcome& outcome, const char* echo, uint32_t t_ms);
    void on_clear(const char* echo, uint32_t t_ms);
    void on_cal(const char* echo, uint32_t t_ms);

    void send(size_t length);

    SensorPort& sensors_;
    ServoPort& servo_;
    LineSink& sink_;
    SimControl* sim_;
    CoreParams params_;

    SunPresence sun_;
    SearchSweep sweep_;
    FaultDetector detector_;
    ModeManager modes_;
    LineWriter writer_;

    float angle_deg_;
    FaultCode fault_ = FaultCode::None;
    Injection injection_ = Injection::None;
    uint32_t cycles_ = 0;
    char line_[LINE_MAX_CHARS] = {};
};

}  // namespace core

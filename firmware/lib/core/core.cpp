#include "core.h"

namespace core {

Core::Core(SensorPort& sensors, ServoPort& servo, LineSink& sink, SimControl* sim,
           const CoreParams& params)
    : sensors_(sensors),
      servo_(servo),
      sink_(sink),
      sim_(sim),
      params_(params),
      sun_(params.sensing),
      sweep_(params.sweep),
      detector_(params.faults),
      modes_(params.modes),
      angle_deg_(params.park_deg) {}

void Core::boot(uint32_t t_ms) {
    if (modes_.mode() != Mode::Boot) return;
    servo_.command(angle_deg_);

    const SensorReading off = sensors_.read_supply_off();
    const FaultCode selftest = check_selftest(off.l_mv, off.r_mv, params_.faults);
    if (selftest != FaultCode::None) raise_fault(selftest, t_ms);
    enter(modes_.boot(selftest == FaultCode::None), t_ms);
}

void Core::step(uint32_t t_ms) {
    if (modes_.mode() == Mode::Boot) return;

    const SensorReading r = measure();
    const float e = pointing_error(r.l_mv, r.r_mv, params_.sensing);

    CycleInputs in;
    in.sun_present = sun_.update(r.l_mv + r.r_mv);
    in.full_sweeps = sweep_.full_sweeps();
    if (modes_.mode() != Mode::Fault) in.fault = detector_.update(r.l_mv, r.r_mv, injection_);

    const Transition t = modes_.cycle(in);
    if (t.changed && t.to == Mode::Fault) raise_fault(in.fault, t_ms);
    enter(t, t_ms);

    // While a sensor looks open, its reading is not trusted, so the platform holds still
    // in TRACKING and SEARCH. Otherwise it would turn away from the light, the other
    // channel would fall into the wall's shadow and restart the debounce, and the fault
    // might never latch. Holding cannot get stuck: either the condition lasts for the
    // debounce (FAULT) or it ends and the motion continues.
    const bool suspect = open_sensor(r.l_mv, r.r_mv, params_.faults) != FaultCode::None;
    act(e, suspect);

    if (++cycles_ % params_.telemetry_every_cycles == 0) {
        Telemetry tm;
        tm.t_ms = t_ms;
        tm.mode = modes_.mode();
        tm.l_mv = r.l_mv;
        tm.r_mv = r.r_mv;
        tm.err = e;
        tm.angle_deg = angle_deg_;
        tm.fault = fault_;
        tm.has_sun_deg = sim_ != nullptr;
        tm.sun_deg = sim_ != nullptr ? sim_->sun_deg() : 0.0f;
        send(writer_.tm(line_, sizeof line_, tm));
    }
}

void Core::handle_line(uint32_t t_ms, std::string_view line) {
    char echo[config::COMMAND_MAX_CHARS + 1];
    sanitize_echo(line, echo, sizeof echo);

    const ParseResult parsed = parse_command(line, params_.commands);
    if (parsed.status != ParseStatus::Ok) {
        send(writer_.nack(line_, sizeof line_, echo, nack_reason(parsed.status)));
        return;
    }

    const Command& command = parsed.command;
    switch (command.type) {
        case CommandType::Ping:
            send(writer_.ack(line_, sizeof line_, echo));
            break;
        case CommandType::Safe:
            reply(modes_.on_safe(), echo, t_ms);
            break;
        case CommandType::Search:
            reply(modes_.on_search(), echo, t_ms);
            break;
        case CommandType::Clear:
            on_clear(echo, t_ms);
            break;
        case CommandType::Inject: {
            const CommandOutcome outcome = modes_.on_inject();
            // One injection at a time: with both channels at the floor, the open-sensor
            // rule could never fire, so a second INJECT replaces the first.
            if (outcome.refusal == Refusal::None) injection_ = command.channel;
            reply(outcome, echo, t_ms);
            break;
        }
        case CommandType::Cal:
            on_cal(echo, t_ms);
            break;
        case CommandType::SimSun:
            if (sim_ == nullptr) {
                send(writer_.nack(line_, sizeof line_, echo, NackReason::NotSim));
            } else {
                sim_->set_sun_deg(command.sun_deg);
                send(writer_.ack(line_, sizeof line_, echo));
            }
            break;
    }
}

// The firmware's view of the sensors: gain-corrected and with any injection applied.
// The cycle, the CLEAR check and CAL all use it, so they cannot see different values.
SensorReading Core::measure() {
    SensorReading r = sensors_.read();
    r.r_mv = apply_r_gain(r.r_mv, params_.sensing);
    apply_injection(r.l_mv, r.r_mv, injection_, params_.faults);
    return r;
}

void Core::act(float e, bool sensor_suspect) {
    switch (modes_.mode()) {
        case Mode::Tracking:
            if (!sensor_suspect) angle_deg_ = tracking_step(angle_deg_, e, params_.controller);
            break;
        case Mode::Search:
            if (!sensor_suspect) angle_deg_ = sweep_.step();
            break;
        case Mode::Safe:
            angle_deg_ = params_.park_deg;
            break;
        case Mode::Fault:  // holds the last angle
        case Mode::Boot:
            break;
    }
    servo_.command(angle_deg_);
}

void Core::enter(const Transition& t, uint32_t t_ms) {
    if (!t.changed) return;
    send(writer_.ev_mode(line_, sizeof line_, t_ms, t));
    if (t.to == Mode::Search) sweep_.start(angle_deg_);
}

void Core::raise_fault(FaultCode code, uint32_t t_ms) {
    fault_ = code;
    send(writer_.ev_fault(line_, sizeof line_, t_ms, code));
}

// The CLEAR checks: the self-test first, then one reading for an open sensor.
FaultCode Core::run_checks() {
    const SensorReading off = sensors_.read_supply_off();
    const FaultCode selftest = check_selftest(off.l_mv, off.r_mv, params_.faults);
    if (selftest != FaultCode::None) return selftest;
    const SensorReading r = measure();
    return open_sensor(r.l_mv, r.r_mv, params_.faults);
}

void Core::reply(const CommandOutcome& outcome, const char* echo, uint32_t t_ms) {
    if (outcome.refusal != Refusal::None) {
        send(writer_.nack(line_, sizeof line_, echo, nack_reason(outcome.refusal)));
        return;
    }
    send(writer_.ack(line_, sizeof line_, echo));
    enter(outcome.transition, t_ms);
}

void Core::on_clear(const char* echo, uint32_t t_ms) {
    const bool in_fault = modes_.mode() == Mode::Fault;
    FaultCode found = FaultCode::None;
    if (in_fault) {
        injection_ = Injection::None;
        found = run_checks();
    }

    const CommandOutcome outcome = modes_.on_clear(found == FaultCode::None);
    if (outcome.refusal == Refusal::None) {
        fault_ = FaultCode::None;
        detector_.reset();
    }
    reply(outcome, echo, t_ms);

    // Still in FAULT: report what the checks found, if it is not the fault already reported.
    if (in_fault && found != FaultCode::None && found != fault_) raise_fault(found, t_ms);
}

void Core::on_cal(const char* echo, uint32_t t_ms) {
    const CommandOutcome outcome = modes_.on_cal();
    if (outcome.refusal != Refusal::None) {
        reply(outcome, echo, t_ms);
        return;
    }

    const SensorReading r = measure();
    const int32_t room_mv = r.l_mv + r.r_mv;
    const SunCalibration cal = calibrate_sun(room_mv, params_.sensing);
    if (!cal.ok) {
        send(writer_.nack(line_, sizeof line_, echo, NackReason::CalOutOfRange));
        return;
    }
    sun_.set_thresholds(cal.enter_mv, cal.exit_mv);
    send(writer_.ack(line_, sizeof line_, echo));
    send(writer_.ev_cal(line_, sizeof line_, t_ms, room_mv, cal.enter_mv, cal.exit_mv));
}

void Core::send(size_t length) {
    if (length > 0) sink_.write_line(line_);
}

}  // namespace core

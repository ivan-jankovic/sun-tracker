#include "modes.h"

namespace core {

namespace {

CommandOutcome refuse(Refusal refusal) {
    CommandOutcome outcome;
    outcome.refusal = refusal;
    return outcome;
}

CommandOutcome accept(const Transition& transition = {}) {
    CommandOutcome outcome;
    outcome.transition = transition;
    return outcome;
}

}  // namespace

Transition ModeManager::go(Mode to, Reason reason) {
    Transition t;
    t.changed = true;
    t.from = mode_;
    t.to = to;
    t.reason = reason;
    mode_ = to;
    sun_present_cycles_ = 0;
    sun_absent_cycles_ = 0;
    return t;
}

Transition ModeManager::boot(bool selftest_ok) {
    if (mode_ != Mode::Boot) return {};
    return selftest_ok ? go(Mode::Search, Reason::Boot) : go(Mode::Fault, Reason::Fault);
}

Transition ModeManager::cycle(const CycleInputs& in) {
    if (mode_ == Mode::Boot || mode_ == Mode::Fault) return {};

    if (in.fault != FaultCode::None) return go(Mode::Fault, Reason::Fault);

    if (mode_ == Mode::Search) {
        sun_present_cycles_ = in.sun_present ? sun_present_cycles_ + 1 : 0;
        if (sun_present_cycles_ >= params_.sun_found_cycles) {
            return go(Mode::Tracking, Reason::SunFound);
        }
        if (in.full_sweeps >= params_.search_max_sweeps) return go(Mode::Safe, Reason::NoSun);
    } else if (mode_ == Mode::Tracking) {
        sun_absent_cycles_ = in.sun_present ? 0 : sun_absent_cycles_ + 1;
        if (sun_absent_cycles_ >= params_.sun_lost_cycles) {
            return go(Mode::Search, Reason::SunLost);
        }
    }
    return {};
}

CommandOutcome ModeManager::on_safe() {
    if (mode_ == Mode::Fault) return refuse(Refusal::FaultLatched);
    if (mode_ == Mode::Safe) return accept();
    return accept(go(Mode::Safe, Reason::CmdSafe));
}

CommandOutcome ModeManager::on_search() {
    if (mode_ == Mode::Fault) return refuse(Refusal::FaultLatched);
    if (mode_ == Mode::Search) return accept();
    return accept(go(Mode::Search, Reason::CmdSearch));
}

CommandOutcome ModeManager::on_clear(bool checks_ok) {
    if (mode_ != Mode::Fault) return refuse(Refusal::NotInFault);
    if (!checks_ok) return refuse(Refusal::SelftestFailed);
    return accept(go(Mode::Search, Reason::CmdClear));
}

CommandOutcome ModeManager::on_inject() const {
    return mode_ == Mode::Fault ? refuse(Refusal::FaultLatched) : accept();
}

CommandOutcome ModeManager::on_cal() const {
    return mode_ == Mode::Safe ? accept() : refuse(Refusal::NotInSafe);
}

}  // namespace core

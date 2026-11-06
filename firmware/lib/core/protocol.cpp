#include "protocol.h"

#include <cmath>
#include <cstdio>

namespace core {

namespace {

// Rounds to the printed precision first, so a tiny negative value prints as
// "0.000" and not "-0.000".
float printable(float value, float scale) {
    const float rounded = std::round(value * scale) / scale;
    return rounded == 0.0f ? 0.0f : rounded;
}

}  // namespace

const char* mode_name(Mode mode) {
    switch (mode) {
        case Mode::Boot: return "BOOT";
        case Mode::Safe: return "SAFE";
        case Mode::Search: return "SEARCH";
        case Mode::Tracking: return "TRACKING";
        case Mode::Fault: return "FAULT";
    }
    return "?";
}

const char* reason_name(Reason reason) {
    switch (reason) {
        case Reason::Boot: return "boot";
        case Reason::SunFound: return "sun_found";
        case Reason::SunLost: return "sun_lost";
        case Reason::NoSun: return "no_sun";
        case Reason::CmdSafe: return "cmd_safe";
        case Reason::CmdSearch: return "cmd_search";
        case Reason::CmdClear: return "cmd_clear";
        case Reason::Fault: return "fault";
    }
    return "?";
}

const char* fault_name(FaultCode code) {
    switch (code) {
        case FaultCode::None: return "-";
        case FaultCode::SelftestShort: return "SELFTEST_SHORT";
        case FaultCode::SensorOpenL: return "SENSOR_OPEN_L";
        case FaultCode::SensorOpenR: return "SENSOR_OPEN_R";
        case FaultCode::InjectedL: return "INJECTED_L";
        case FaultCode::InjectedR: return "INJECTED_R";
    }
    return "?";
}

const char* nack_name(NackReason reason) {
    switch (reason) {
        case NackReason::Unknown: return "unknown";
        case NackReason::BadArg: return "bad_arg";
        case NackReason::FaultLatched: return "fault_latched";
        case NackReason::NotInFault: return "not_in_fault";
        case NackReason::NotInSafe: return "not_in_safe";
        case NackReason::CalOutOfRange: return "cal_out_of_range";
        case NackReason::NotSim: return "not_sim";
        case NackReason::SelftestFailed: return "selftest_failed";
    }
    return "?";
}

NackReason nack_reason(ParseStatus status) {
    return status == ParseStatus::BadArg ? NackReason::BadArg : NackReason::Unknown;
}

NackReason nack_reason(Refusal refusal) {
    switch (refusal) {
        case Refusal::NotInFault: return NackReason::NotInFault;
        case Refusal::NotInSafe: return NackReason::NotInSafe;
        case Refusal::SelftestFailed: return NackReason::SelftestFailed;
        case Refusal::FaultLatched:
        case Refusal::None: break;
    }
    return NackReason::FaultLatched;
}

size_t LineWriter::finish(int written, size_t size) {
    if (written < 0 || static_cast<size_t>(written) >= size) return 0;
    ++seq_;
    return static_cast<size_t>(written);
}

size_t LineWriter::tm(char* out, size_t size, const Telemetry& t) {
    char sun[16] = "-";
    if (t.has_sun_deg) std::snprintf(sun, sizeof sun, "%.1f", printable(t.sun_deg, 10.0f));
    const int n = std::snprintf(out, size, "TM,%lu,%lu,%s,%ld,%ld,%.3f,%.1f,%s,%s",
                                static_cast<unsigned long>(seq_),
                                static_cast<unsigned long>(t.t_ms), mode_name(t.mode),
                                static_cast<long>(t.l_mv), static_cast<long>(t.r_mv),
                                printable(t.err, 1000.0f), printable(t.angle_deg, 10.0f),
                                fault_name(t.fault), sun);
    return finish(n, size);
}

size_t LineWriter::ev_mode(char* out, size_t size, uint32_t t_ms, const Transition& transition) {
    if (!transition.changed) return 0;
    const int n = std::snprintf(out, size, "EV,%lu,%lu,MODE,%s,%s,%s",
                                static_cast<unsigned long>(seq_),
                                static_cast<unsigned long>(t_ms), mode_name(transition.from),
                                mode_name(transition.to), reason_name(transition.reason));
    return finish(n, size);
}

size_t LineWriter::ev_fault(char* out, size_t size, uint32_t t_ms, FaultCode code) {
    if (code == FaultCode::None) return 0;
    const int n = std::snprintf(out, size, "EV,%lu,%lu,FAULT,%s",
                                static_cast<unsigned long>(seq_),
                                static_cast<unsigned long>(t_ms), fault_name(code));
    return finish(n, size);
}

size_t LineWriter::ev_cal(char* out, size_t size, uint32_t t_ms, int32_t room_mv,
                          int32_t enter_mv, int32_t exit_mv) {
    const int n = std::snprintf(out, size, "EV,%lu,%lu,CAL,%ld,%ld,%ld",
                                static_cast<unsigned long>(seq_),
                                static_cast<unsigned long>(t_ms), static_cast<long>(room_mv),
                                static_cast<long>(enter_mv), static_cast<long>(exit_mv));
    return finish(n, size);
}

size_t LineWriter::ack(char* out, size_t size, const char* echo) {
    const int n = std::snprintf(out, size, "ACK,%lu,%s", static_cast<unsigned long>(seq_), echo);
    return finish(n, size);
}

size_t LineWriter::nack(char* out, size_t size, const char* echo, NackReason reason) {
    const int n = std::snprintf(out, size, "NACK,%lu,%s,%s", static_cast<unsigned long>(seq_),
                                echo, nack_name(reason));
    return finish(n, size);
}

}  // namespace core

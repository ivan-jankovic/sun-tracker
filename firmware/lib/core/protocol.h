// The serial protocol of docs/design.md: names on the wire and the TM, EV, ACK
// and NACK lines. Every line written here takes the next number of one shared
// sequence counter, so the ground station can count lost lines.
#pragma once

#include <cstddef>
#include <cstdint>

#include "commands.h"
#include "faults.h"
#include "modes.h"

namespace core {

// Long enough for the longest line; a line that would not fit is not written.
inline constexpr size_t LINE_MAX_CHARS = 96;

enum class NackReason : uint8_t {
    Unknown,
    BadArg,
    FaultLatched,
    NotInFault,
    NotInSafe,
    CalOutOfRange,
    NotSim,
    SelftestFailed,
};

const char* mode_name(Mode mode);
const char* reason_name(Reason reason);
const char* fault_name(FaultCode code);  // "-" for no fault
const char* nack_name(NackReason reason);

// Only for a refused parse (Unknown or BadArg) or a refusal (not None).
NackReason nack_reason(ParseStatus status);
NackReason nack_reason(Refusal refusal);

struct Telemetry {
    uint32_t t_ms = 0;
    Mode mode = Mode::Boot;
    int32_t l_mv = 0;
    int32_t r_mv = 0;
    float err = 0.0f;
    float angle_deg = 0.0f;
    FaultCode fault = FaultCode::None;
    bool has_sun_deg = false;  // true in the esp32dev-sim build only
    float sun_deg = 0.0f;
};

// Each function writes one line without a line end into `out` and returns its
// length. It returns 0, writes nothing usable and keeps the sequence number if
// the line does not fit or there is nothing to report.
class LineWriter {
public:
    uint32_t next_seq() const { return seq_; }

    size_t tm(char* out, size_t size, const Telemetry& tm);
    size_t ev_mode(char* out, size_t size, uint32_t t_ms, const Transition& transition);
    size_t ev_fault(char* out, size_t size, uint32_t t_ms, FaultCode code);
    size_t ev_cal(char* out, size_t size, uint32_t t_ms, int32_t room_mv, int32_t enter_mv,
                  int32_t exit_mv);
    size_t ack(char* out, size_t size, const char* echo);
    size_t nack(char* out, size_t size, const char* echo, NackReason reason);

private:
    size_t finish(int written, size_t size);

    uint32_t seq_ = 0;
};

}  // namespace core

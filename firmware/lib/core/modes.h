// The mode manager: the transition table of docs/design.md, and nothing else.
// Every change of mode is returned with its reason, so the caller can report it.
#pragma once

#include <cstdint>

#include "config.h"
#include "faults.h"

namespace core {

// BOOT exists only until the boot self-test has run; it never appears in telemetry.
enum class Mode : uint8_t { Boot, Safe, Search, Tracking, Fault };

enum class Reason : uint8_t { Boot, SunFound, SunLost, NoSun, CmdSafe, CmdSearch, CmdClear, Fault };

struct Transition {
    bool changed = false;
    Mode from = Mode::Boot;
    Mode to = Mode::Boot;
    Reason reason = Reason::Boot;
};

// Why a command is refused in the current mode. Parse errors are reported elsewhere.
enum class Refusal : uint8_t { None, FaultLatched, NotInFault, NotInSafe, SelftestFailed };

struct CommandOutcome {
    Refusal refusal = Refusal::None;  // None: the command is accepted (ACK)
    Transition transition;            // set only if the mode changed
};

struct CycleInputs {
    bool sun_present = false;
    FaultCode fault = FaultCode::None;
    uint32_t full_sweeps = 0;  // from SearchSweep, counted since SEARCH was entered
};

struct ModeParams {
    uint32_t sun_found_cycles = config::SUN_FOUND_CYCLES;
    uint32_t sun_lost_cycles = config::SUN_LOST_CYCLES;
    uint32_t search_max_sweeps = config::SEARCH_MAX_SWEEPS;
};

class ModeManager {
public:
    explicit ModeManager(const ModeParams& params = {}) : params_(params) {}

    Mode mode() const { return mode_; }

    // Leaves BOOT: to SEARCH if the self-test passed, else to FAULT.
    Transition boot(bool selftest_ok);

    // Once per control cycle. A fault wins over every other rule.
    Transition cycle(const CycleInputs& in);

    CommandOutcome on_safe();
    CommandOutcome on_search();
    CommandOutcome on_clear(bool checks_ok);
    CommandOutcome on_inject() const;
    CommandOutcome on_cal() const;

private:
    Transition go(Mode to, Reason reason);

    ModeParams params_;
    Mode mode_ = Mode::Boot;
    uint32_t sun_present_cycles_ = 0;
    uint32_t sun_absent_cycles_ = 0;
};

}  // namespace core

#include <unity.h>

#include "modes.h"

using core::CommandOutcome;
using core::CycleInputs;
using core::FaultCode;
using core::Mode;
using core::ModeManager;
using core::ModeParams;
using core::Reason;
using core::Refusal;
using core::Transition;

static const ModeParams mp;

void setUp() {}
void tearDown() {}

static void assert_mode(Mode expected, Mode actual) {
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(expected), static_cast<uint8_t>(actual));
}

static void assert_transition(Mode from, Mode to, Reason reason, const Transition& t) {
    TEST_ASSERT_TRUE_MESSAGE(t.changed, "expected a transition");
    assert_mode(from, t.from);
    assert_mode(to, t.to);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(reason), static_cast<uint8_t>(t.reason));
}

static void assert_accepted_without_change(const CommandOutcome& o) {
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Refusal::None), static_cast<uint8_t>(o.refusal));
    TEST_ASSERT_FALSE(o.transition.changed);
}

static void assert_refused(Refusal expected, const CommandOutcome& o) {
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(expected), static_cast<uint8_t>(o.refusal));
    TEST_ASSERT_FALSE(o.transition.changed);
}

static CycleInputs sun(bool present) {
    CycleInputs in;
    in.sun_present = present;
    return in;
}

static CycleInputs fault(FaultCode code) {
    CycleInputs in;
    in.fault = code;
    return in;
}

static CycleInputs sweeps(uint32_t n) {
    CycleInputs in;
    in.full_sweeps = n;
    return in;
}

// Runs n cycles with the same inputs; returns the last transition.
static Transition run(ModeManager& m, uint32_t n, const CycleInputs& in) {
    Transition t;
    for (uint32_t i = 0; i < n; ++i) t = m.cycle(in);
    return t;
}

static ModeManager in_search() {
    ModeManager m;
    m.boot(true);
    return m;
}

static ModeManager in_tracking() {
    ModeManager m = in_search();
    run(m, mp.sun_found_cycles, sun(true));
    return m;
}

static ModeManager in_safe() {
    ModeManager m = in_search();
    m.on_safe();
    return m;
}

static ModeManager in_fault() {
    ModeManager m;
    m.boot(false);
    return m;
}

// --- boot ---

void test_starts_in_boot() {
    assert_mode(Mode::Boot, ModeManager().mode());
}

void test_boot_with_passing_selftest_goes_to_search() {
    ModeManager m;
    assert_transition(Mode::Boot, Mode::Search, Reason::Boot, m.boot(true));
}

void test_boot_with_failing_selftest_goes_to_fault() {
    ModeManager m;
    assert_transition(Mode::Boot, Mode::Fault, Reason::Fault, m.boot(false));
}

void test_boot_runs_only_once() {
    ModeManager m = in_search();
    TEST_ASSERT_FALSE(m.boot(false).changed);
    assert_mode(Mode::Search, m.mode());
}

void test_cycle_before_boot_does_nothing() {
    ModeManager m;
    TEST_ASSERT_FALSE(m.cycle(fault(FaultCode::SensorOpenL)).changed);
    assert_mode(Mode::Boot, m.mode());
}

// --- SEARCH ---

void test_search_stays_while_sun_is_present_for_too_few_cycles() {
    ModeManager m = in_search();
    TEST_ASSERT_FALSE(run(m, mp.sun_found_cycles - 1, sun(true)).changed);
    assert_mode(Mode::Search, m.mode());
}

void test_search_goes_to_tracking_when_sun_is_present_for_enough_cycles() {
    ModeManager m = in_search();
    assert_transition(Mode::Search, Mode::Tracking, Reason::SunFound,
                      run(m, mp.sun_found_cycles, sun(true)));
}

void test_one_cycle_without_sun_restarts_the_sun_found_count() {
    ModeManager m = in_search();
    run(m, mp.sun_found_cycles - 1, sun(true));
    m.cycle(sun(false));
    run(m, mp.sun_found_cycles - 1, sun(true));
    assert_mode(Mode::Search, m.mode());
}

void test_search_stays_below_the_sweep_limit() {
    ModeManager m = in_search();
    TEST_ASSERT_FALSE(m.cycle(sweeps(mp.search_max_sweeps - 1)).changed);
}

void test_search_goes_to_safe_after_the_sweep_limit() {
    ModeManager m = in_search();
    assert_transition(Mode::Search, Mode::Safe, Reason::NoSun,
                      m.cycle(sweeps(mp.search_max_sweeps)));
}

void test_sun_found_wins_over_no_sun_in_the_same_cycle() {
    ModeManager m = in_search();
    run(m, mp.sun_found_cycles - 1, sun(true));
    CycleInputs in = sun(true);
    in.full_sweeps = mp.search_max_sweeps;
    assert_transition(Mode::Search, Mode::Tracking, Reason::SunFound, m.cycle(in));
}

// --- TRACKING ---

void test_tracking_stays_while_sun_is_absent_for_too_few_cycles() {
    ModeManager m = in_tracking();
    TEST_ASSERT_FALSE(run(m, mp.sun_lost_cycles - 1, sun(false)).changed);
    assert_mode(Mode::Tracking, m.mode());
}

void test_tracking_goes_to_search_when_sun_is_absent_for_enough_cycles() {
    ModeManager m = in_tracking();
    assert_transition(Mode::Tracking, Mode::Search, Reason::SunLost,
                      run(m, mp.sun_lost_cycles, sun(false)));
}

void test_one_cycle_with_sun_restarts_the_sun_lost_count() {
    ModeManager m = in_tracking();
    run(m, mp.sun_lost_cycles - 1, sun(false));
    m.cycle(sun(true));
    run(m, mp.sun_lost_cycles - 1, sun(false));
    assert_mode(Mode::Tracking, m.mode());
}

void test_tracking_ignores_the_sweep_count() {
    ModeManager m = in_tracking();
    CycleInputs in = sun(true);
    in.full_sweeps = mp.search_max_sweeps;
    TEST_ASSERT_FALSE(m.cycle(in).changed);
}

void test_search_after_sun_lost_needs_a_fresh_sun_found_count() {
    ModeManager m = in_tracking();
    run(m, mp.sun_lost_cycles, sun(false));
    run(m, mp.sun_found_cycles - 1, sun(true));
    assert_mode(Mode::Search, m.mode());
}

// --- SAFE ---

void test_safe_stays_passive_when_sun_is_present() {
    ModeManager m = in_safe();
    TEST_ASSERT_FALSE(run(m, mp.sun_found_cycles * 10, sun(true)).changed);
    assert_mode(Mode::Safe, m.mode());
}

// --- FAULT ---

void test_fault_from_search_goes_to_fault() {
    ModeManager m = in_search();
    assert_transition(Mode::Search, Mode::Fault, Reason::Fault,
                      m.cycle(fault(FaultCode::SensorOpenL)));
}

void test_fault_from_tracking_goes_to_fault() {
    ModeManager m = in_tracking();
    assert_transition(Mode::Tracking, Mode::Fault, Reason::Fault,
                      m.cycle(fault(FaultCode::InjectedR)));
}

void test_fault_from_safe_goes_to_fault() {
    ModeManager m = in_safe();
    assert_transition(Mode::Safe, Mode::Fault, Reason::Fault,
                      m.cycle(fault(FaultCode::SensorOpenR)));
}

void test_fault_wins_over_sun_found_in_the_same_cycle() {
    ModeManager m = in_search();
    run(m, mp.sun_found_cycles - 1, sun(true));
    CycleInputs in = sun(true);
    in.fault = FaultCode::SensorOpenL;
    assert_transition(Mode::Search, Mode::Fault, Reason::Fault, m.cycle(in));
}

void test_fault_latches_when_the_condition_goes_away() {
    ModeManager m = in_fault();
    TEST_ASSERT_FALSE(run(m, 100, sun(true)).changed);
    assert_mode(Mode::Fault, m.mode());
}

// --- SAFE command ---

void test_safe_command_from_search_goes_to_safe() {
    ModeManager m = in_search();
    assert_transition(Mode::Search, Mode::Safe, Reason::CmdSafe, m.on_safe().transition);
}

void test_safe_command_from_tracking_goes_to_safe() {
    ModeManager m = in_tracking();
    assert_transition(Mode::Tracking, Mode::Safe, Reason::CmdSafe, m.on_safe().transition);
}

void test_safe_command_in_safe_is_accepted_without_a_transition() {
    ModeManager m = in_safe();
    assert_accepted_without_change(m.on_safe());
}

void test_safe_command_in_fault_is_refused() {
    ModeManager m = in_fault();
    assert_refused(Refusal::FaultLatched, m.on_safe());
    assert_mode(Mode::Fault, m.mode());
}

// --- SEARCH command ---

void test_search_command_from_safe_goes_to_search() {
    ModeManager m = in_safe();
    assert_transition(Mode::Safe, Mode::Search, Reason::CmdSearch, m.on_search().transition);
}

void test_search_command_from_tracking_goes_to_search() {
    ModeManager m = in_tracking();
    assert_transition(Mode::Tracking, Mode::Search, Reason::CmdSearch, m.on_search().transition);
}

void test_search_command_in_search_is_accepted_without_a_transition() {
    ModeManager m = in_search();
    assert_accepted_without_change(m.on_search());
}

void test_search_command_in_fault_is_refused() {
    ModeManager m = in_fault();
    assert_refused(Refusal::FaultLatched, m.on_search());
}

// --- CLEAR command ---

void test_clear_outside_fault_is_refused() {
    ModeManager m = in_search();
    assert_refused(Refusal::NotInFault, m.on_clear(true));
}

void test_clear_with_failing_checks_is_refused_and_stays_in_fault() {
    ModeManager m = in_fault();
    assert_refused(Refusal::SelftestFailed, m.on_clear(false));
    assert_mode(Mode::Fault, m.mode());
}

void test_clear_with_passing_checks_goes_to_search() {
    ModeManager m = in_fault();
    assert_transition(Mode::Fault, Mode::Search, Reason::CmdClear, m.on_clear(true).transition);
}

void test_search_after_clear_needs_a_fresh_sun_found_count() {
    ModeManager m = in_search();
    run(m, mp.sun_found_cycles - 1, sun(true));
    m.cycle(fault(FaultCode::SensorOpenL));
    m.on_clear(true);
    m.cycle(sun(true));
    assert_mode(Mode::Search, m.mode());
}

// --- INJECT and CAL commands ---

void test_inject_is_accepted_outside_fault_without_a_transition() {
    assert_accepted_without_change(in_search().on_inject());
    assert_accepted_without_change(in_tracking().on_inject());
    assert_accepted_without_change(in_safe().on_inject());
}

void test_inject_in_fault_is_refused() {
    assert_refused(Refusal::FaultLatched, in_fault().on_inject());
}

void test_cal_is_accepted_in_safe_without_a_transition() {
    assert_accepted_without_change(in_safe().on_cal());
}

void test_cal_outside_safe_is_refused() {
    assert_refused(Refusal::NotInSafe, in_search().on_cal());
    assert_refused(Refusal::NotInSafe, in_tracking().on_cal());
    assert_refused(Refusal::NotInSafe, in_fault().on_cal());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_starts_in_boot);
    RUN_TEST(test_boot_with_passing_selftest_goes_to_search);
    RUN_TEST(test_boot_with_failing_selftest_goes_to_fault);
    RUN_TEST(test_boot_runs_only_once);
    RUN_TEST(test_cycle_before_boot_does_nothing);
    RUN_TEST(test_search_stays_while_sun_is_present_for_too_few_cycles);
    RUN_TEST(test_search_goes_to_tracking_when_sun_is_present_for_enough_cycles);
    RUN_TEST(test_one_cycle_without_sun_restarts_the_sun_found_count);
    RUN_TEST(test_search_stays_below_the_sweep_limit);
    RUN_TEST(test_search_goes_to_safe_after_the_sweep_limit);
    RUN_TEST(test_sun_found_wins_over_no_sun_in_the_same_cycle);
    RUN_TEST(test_tracking_stays_while_sun_is_absent_for_too_few_cycles);
    RUN_TEST(test_tracking_goes_to_search_when_sun_is_absent_for_enough_cycles);
    RUN_TEST(test_one_cycle_with_sun_restarts_the_sun_lost_count);
    RUN_TEST(test_tracking_ignores_the_sweep_count);
    RUN_TEST(test_search_after_sun_lost_needs_a_fresh_sun_found_count);
    RUN_TEST(test_safe_stays_passive_when_sun_is_present);
    RUN_TEST(test_fault_from_search_goes_to_fault);
    RUN_TEST(test_fault_from_tracking_goes_to_fault);
    RUN_TEST(test_fault_from_safe_goes_to_fault);
    RUN_TEST(test_fault_wins_over_sun_found_in_the_same_cycle);
    RUN_TEST(test_fault_latches_when_the_condition_goes_away);
    RUN_TEST(test_safe_command_from_search_goes_to_safe);
    RUN_TEST(test_safe_command_from_tracking_goes_to_safe);
    RUN_TEST(test_safe_command_in_safe_is_accepted_without_a_transition);
    RUN_TEST(test_safe_command_in_fault_is_refused);
    RUN_TEST(test_search_command_from_safe_goes_to_search);
    RUN_TEST(test_search_command_from_tracking_goes_to_search);
    RUN_TEST(test_search_command_in_search_is_accepted_without_a_transition);
    RUN_TEST(test_search_command_in_fault_is_refused);
    RUN_TEST(test_clear_outside_fault_is_refused);
    RUN_TEST(test_clear_with_failing_checks_is_refused_and_stays_in_fault);
    RUN_TEST(test_clear_with_passing_checks_goes_to_search);
    RUN_TEST(test_search_after_clear_needs_a_fresh_sun_found_count);
    RUN_TEST(test_inject_is_accepted_outside_fault_without_a_transition);
    RUN_TEST(test_inject_in_fault_is_refused);
    RUN_TEST(test_cal_is_accepted_in_safe_without_a_transition);
    RUN_TEST(test_cal_outside_safe_is_refused);
    return UNITY_END();
}

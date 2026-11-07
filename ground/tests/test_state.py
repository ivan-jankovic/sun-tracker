from ground import limits
from ground.protocol import CalEvent, Log, ParseError
from ground.state import GroundState, Thresholds


def tm(seq: int, t_ms: int, mode: str = "SEARCH", fault: str = "-") -> str:
    return f"TM,{seq},{t_ms},{mode},1000,1000,0.000,90.0,{fault},-"


def test_nothing_is_known_before_the_first_line():
    state = GroundState()
    assert state.mode is None
    assert state.fault is None
    assert state.thresholds == Thresholds(limits.SUN_ENTER_MV, limits.SUN_EXIT_MV, "assumed")
    assert len(state.telemetry) == 0


def test_telemetry_sets_mode_and_fault():
    state = GroundState()
    state.feed(1.0, tm(5, 1000, mode="FAULT", fault="INJECTED_L"))
    assert state.mode == "FAULT"
    assert state.fault == "INJECTED_L"
    assert state.telemetry[-1].t_ms == 1000


def test_telemetry_older_than_the_window_is_dropped():
    state = GroundState(window_ms=1000)
    for i in range(30):
        state.feed(0.0, tm(i, i * 100))
    assert [m.t_ms for m in state.telemetry] == list(range(1900, 3000, 100))


def test_events_set_mode_and_fault_between_telemetry_lines():
    state = GroundState()
    state.feed(0.0, tm(0, 100))
    state.feed(0.0, "EV,1,120,FAULT,SENSOR_OPEN_R")
    state.feed(0.0, "EV,2,120,MODE,TRACKING,FAULT,fault")
    assert (state.mode, state.fault) == ("FAULT", "SENSOR_OPEN_R")
    state.feed(0.0, "ACK,3,CLEAR")
    state.feed(0.0, "EV,4,200,MODE,FAULT,SEARCH,cmd_clear")
    assert (state.mode, state.fault) == ("SEARCH", None)


def test_cal_sets_the_thresholds():
    state = GroundState()
    state.feed(0.0, "EV,0,12650,MODE,SEARCH,SAFE,no_sun")
    state.feed(0.0, "ACK,1,CAL")
    state.feed(0.0, "EV,2,500,CAL,573,1173,873")
    assert state.thresholds == Thresholds(1173, 873, "cal")


def test_seeing_the_boot_confirms_the_default_thresholds():
    state = GroundState()
    state.feed(0.0, "EV,0,24,MODE,BOOT,SEARCH,boot")
    assert state.thresholds == Thresholds(limits.SUN_ENTER_MV, limits.SUN_EXIT_MV, "boot")
    assert state.mode == "SEARCH"


def test_a_restart_clears_telemetry_fault_and_cal():
    state = GroundState()
    state.feed(0.0, "EV,10,5000,CAL,573,1173,873")
    state.feed(0.0, tm(11, 5050, mode="FAULT", fault="INJECTED_L"))
    state.feed(0.0, "EV,0,24,MODE,BOOT,SEARCH,boot")
    assert state.restarts == 1
    assert len(state.telemetry) == 0
    assert state.fault is None
    assert state.thresholds.source == "boot"
    assert state.thresholds.enter_mv == limits.SUN_ENTER_MV


def test_a_failed_selftest_at_boot_survives_the_restart_reset():
    # At boot EV FAULT comes before EV MODE, so the restart is seen on the fault line.
    state = GroundState()
    state.feed(0.0, tm(40, 9000))
    state.feed(0.0, "EV,0,24,FAULT,SELFTEST_SHORT")
    state.feed(0.0, "EV,1,24,MODE,BOOT,FAULT,fault")
    assert (state.mode, state.fault) == ("FAULT", "SELFTEST_SHORT")


def test_lost_lines_are_counted():
    state = GroundState()
    state.feed(0.0, tm(0, 100))
    state.feed(0.0, tm(3, 400))
    assert state.lost == 2


def test_bad_lines_are_counted_and_listed_but_change_nothing():
    state = GroundState()
    state.feed(0.0, tm(0, 100, mode="SAFE"))
    assert state.feed(2.0, "TM,1,200,SAF") is None
    assert state.bad_lines == 1
    assert state.mode == "SAFE"
    assert isinstance(state.events[-1].item, ParseError)
    assert state.events[-1].host_time == 2.0


def test_events_are_listed_without_telemetry():
    state = GroundState()
    state.feed(1.0, "# sun-tracker boot")
    state.feed(2.0, tm(0, 100))
    state.feed(3.0, "EV,1,120,CAL,573,1173,873")
    assert [type(e.item) for e in state.events] == [Log, CalEvent]


def test_only_the_last_events_are_kept():
    state = GroundState(events_kept=3)
    for i in range(5):
        state.feed(float(i), f"ACK,{i},PING")
    assert [e.item.seq for e in state.events] == [2, 3, 4]

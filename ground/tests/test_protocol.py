import pytest

from ground.protocol import (
    Ack,
    CalEvent,
    FaultEvent,
    Log,
    ModeEvent,
    Nack,
    ParseError,
    SeqTracker,
    Telemetry,
    parse_line,
)

# --- valid lines ---


def test_telemetry_from_the_design_example():
    assert parse_line("TM,1042,52100,TRACKING,2215,2180,0.008,97.5,-,-") == Telemetry(
        seq=1042,
        t_ms=52100,
        mode="TRACKING",
        l_mv=2215,
        r_mv=2180,
        err=0.008,
        angle_deg=97.5,
        fault=None,
        sun_deg=None,
    )


def test_telemetry_with_a_fault_and_the_simulated_light():
    tm = parse_line("TM,958,93958,FAULT,142,2041,-0.870,30.5,INJECTED_L,30.0")
    assert tm.fault == "INJECTED_L"
    assert tm.sun_deg == 30.0
    assert tm.err == -0.870


def test_telemetry_with_a_negative_simulated_light_angle():
    assert parse_line("TM,0,52100,TRACKING,2215,2180,0.008,97.5,-,-90.0").sun_deg == -90.0


def test_mode_event():
    assert parse_line("EV,0,24,MODE,BOOT,SEARCH,boot") == ModeEvent(
        seq=0, t_ms=24, from_mode="BOOT", to_mode="SEARCH", reason="boot"
    )


def test_fault_event():
    assert parse_line("EV,956,93958,FAULT,INJECTED_L") == FaultEvent(
        seq=956, t_ms=93958, code="INJECTED_L"
    )


def test_cal_event():
    assert parse_line("EV,460,45766,CAL,3978,4578,4278") == CalEvent(
        seq=460, t_ms=45766, room_mv=3978, enter_mv=4578, exit_mv=4278
    )


def test_ack_keeps_the_echoed_command():
    assert parse_line("ACK,807,SIM SUN 30") == Ack(seq=807, command="SIM SUN 30")


def test_nack():
    assert parse_line("NACK,12,CAL,not_in_safe") == Nack(
        seq=12, command="CAL", reason="not_in_safe"
    )


def test_log_line():
    assert parse_line("# sun-tracker esp32dev") == Log("sun-tracker esp32dev")


@pytest.mark.parametrize("ending", ["\n", "\r\n", ""])
def test_line_endings_are_ignored(ending):
    assert parse_line("ACK,1,PING" + ending) == Ack(seq=1, command="PING")


# --- refused lines ---


@pytest.mark.parametrize(
    "line",
    [
        # From the serial monitor logs: typed characters echoed into received lines.
        "STM,779,76458,TRACKING,1948,2021,-0.018,90.3,-,90.0",
        " TM,784,76958,TRACKING,1948,2030,-0.021,90.3,-,90.0",
        "TM,790,77558,TRACKING,1952,2027,N-0.019,90.3,-,90.0",
        "TM,380,37976,SAFE,1937,2037,-0.0S25,90.0,-,-",
        # From the same log: a line cut in two.
        "TM,806,79158,TRACKING,1944,2025,",
        "-0.020,90.3,-,90.0",
    ],
)
def test_corrupted_lines_from_the_real_logs_are_refused(line):
    with pytest.raises(ParseError):
        parse_line(line)


@pytest.mark.parametrize(
    "line, reason",
    [
        ("TM,1,100,SEARCH,1000,1000,0.000,94.5,-", "malformed TM line"),
        ("EV,1,100,MODE,SAFE,SEARCH", "malformed EV line"),
        ("EV,1,100,WEATHER,SUNNY", "malformed EV line"),
        ("ACK,1", "malformed ACK line"),
        ("NACK,1,CAL", "malformed NACK line"),
        ("HELLO", "unknown line type"),
        ("", "unknown line type"),
    ],
)
def test_wrong_shapes_are_refused_with_a_reason(line, reason):
    with pytest.raises(ParseError) as error:
        parse_line(line)
    assert error.value.reason == reason


@pytest.mark.parametrize(
    "line",
    [
        "TM,1,100,DANCING,1000,1000,0.000,94.5,-,-",  # unknown mode
        "TM,1,100,FAULT,1000,1000,0.000,94.5,ON_FIRE,-",  # unknown fault code
        "EV,1,100,MODE,SAFE,SEARCH,because",  # unknown reason
        "NACK,1,CAL,no_thanks",  # unknown NACK reason
        "TM,-1,100,SEARCH,1000,1000,0.000,94.5,-,-",  # negative seq
        "TM,1,100,SEARCH,1e3,1000,0.000,94.5,-,-",  # not a plain integer
        "TM,1,100,SEARCH,1000,1000,nan,94.5,-,-",  # not a plain decimal
        "TM,1,100,SEARCH,1000,1000,0.0.0,94.5,-,-",
        "TM,1,100,SEARCH,+1000,1000,0.000,94.5,-,-",
        "TM,1,100,SEARCH,1_000,1000,0.000,94.5,-,-",
    ],
)
def test_values_the_protocol_does_not_define_are_refused(line):
    with pytest.raises(ParseError):
        parse_line(line)


def test_parse_error_keeps_the_line():
    with pytest.raises(ParseError) as error:
        parse_line("HELLO")
    assert error.value.line == "HELLO"


# --- lost lines ---


def test_consecutive_numbers_lose_nothing():
    seq = SeqTracker()
    assert [seq.feed(n) for n in range(5)] == [0, 0, 0, 0, 0]
    assert seq.lost == 0


def test_the_first_number_seen_does_not_count_as_a_gap():
    seq = SeqTracker()
    assert seq.feed(696) == 0


def test_a_gap_counts_the_missing_lines():
    seq = SeqTracker()
    seq.feed(10)
    assert seq.feed(14) == 3
    assert seq.lost == 3


def test_a_jump_back_is_a_restart_not_a_loss():
    seq = SeqTracker()
    for n in range(100, 105):
        seq.feed(n)
    assert seq.feed(0) == 0
    assert seq.restarts == 1
    assert seq.feed(1) == 0
    assert seq.lost == 0


def test_after_a_restart_gaps_are_counted_again():
    seq = SeqTracker()
    seq.feed(50)
    seq.feed(0)
    assert seq.feed(3) == 2
    assert seq.lost == 2

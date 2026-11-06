"""The serial protocol of docs/design.md, parsed into typed messages.

No serial port and no plotting here, so the parser can be tested on its own.
Parsing is strict: a value the protocol does not define is a ParseError, so a
mismatch between firmware and ground station shows up at once.
"""

from dataclasses import dataclass

MODES = frozenset({"BOOT", "SAFE", "SEARCH", "TRACKING", "FAULT"})
REASONS = frozenset(
    {"boot", "sun_found", "sun_lost", "no_sun", "cmd_safe", "cmd_search", "cmd_clear", "fault"}
)
FAULT_CODES = frozenset(
    {"SELFTEST_SHORT", "SENSOR_OPEN_L", "SENSOR_OPEN_R", "INJECTED_L", "INJECTED_R"}
)
NACK_REASONS = frozenset(
    {
        "unknown",
        "bad_arg",
        "fault_latched",
        "not_in_fault",
        "not_in_safe",
        "cal_out_of_range",
        "not_sim",
        "selftest_failed",
    }
)
NONE = "-"  # the protocol's "no value": no fault, or no simulated light


@dataclass(frozen=True, slots=True)
class Telemetry:
    seq: int
    t_ms: int
    mode: str
    l_mv: int
    r_mv: int
    err: float
    angle_deg: float  # commanded
    fault: str | None
    sun_deg: float | None  # esp32dev-sim only


@dataclass(frozen=True, slots=True)
class ModeEvent:
    seq: int
    t_ms: int
    from_mode: str
    to_mode: str
    reason: str


@dataclass(frozen=True, slots=True)
class FaultEvent:
    seq: int
    t_ms: int
    code: str


@dataclass(frozen=True, slots=True)
class CalEvent:
    seq: int
    t_ms: int
    room_mv: int
    enter_mv: int
    exit_mv: int


@dataclass(frozen=True, slots=True)
class Ack:
    seq: int
    command: str


@dataclass(frozen=True, slots=True)
class Nack:
    seq: int
    command: str
    reason: str


@dataclass(frozen=True, slots=True)
class Log:
    """A line starting with '#': for people, not part of the protocol."""

    text: str


type Message = Telemetry | ModeEvent | FaultEvent | CalEvent | Ack | Nack | Log
type Numbered = Telemetry | ModeEvent | FaultEvent | CalEvent | Ack | Nack


class ParseError(ValueError):
    def __init__(self, line: str, reason: str) -> None:
        super().__init__(f"{reason}: {line!r}")
        self.line = line
        self.reason = reason


def parse_line(line: str) -> Message:
    """Parses one received line, with or without its line end."""
    line = line.rstrip("\r\n")
    if line.startswith("#"):
        return Log(line[1:].strip())

    fields = line.split(",")
    match fields:
        case ["TM", seq, t_ms, mode, l_mv, r_mv, err, angle, fault, sun]:
            return Telemetry(
                seq=_uint(line, seq),
                t_ms=_uint(line, t_ms),
                mode=_one_of(line, mode, MODES, "mode"),
                l_mv=_int(line, l_mv),
                r_mv=_int(line, r_mv),
                err=_float(line, err),
                angle_deg=_float(line, angle),
                fault=None if fault == NONE else _one_of(line, fault, FAULT_CODES, "fault"),
                sun_deg=None if sun == NONE else _float(line, sun),
            )
        case ["EV", seq, t_ms, "MODE", from_mode, to_mode, reason]:
            return ModeEvent(
                seq=_uint(line, seq),
                t_ms=_uint(line, t_ms),
                from_mode=_one_of(line, from_mode, MODES, "mode"),
                to_mode=_one_of(line, to_mode, MODES, "mode"),
                reason=_one_of(line, reason, REASONS, "reason"),
            )
        case ["EV", seq, t_ms, "FAULT", code]:
            return FaultEvent(
                seq=_uint(line, seq),
                t_ms=_uint(line, t_ms),
                code=_one_of(line, code, FAULT_CODES, "fault"),
            )
        case ["EV", seq, t_ms, "CAL", room_mv, enter_mv, exit_mv]:
            return CalEvent(
                seq=_uint(line, seq),
                t_ms=_uint(line, t_ms),
                room_mv=_int(line, room_mv),
                enter_mv=_int(line, enter_mv),
                exit_mv=_int(line, exit_mv),
            )
        case ["ACK", seq, command]:
            return Ack(seq=_uint(line, seq), command=command)
        case ["NACK", seq, command, reason]:
            return Nack(
                seq=_uint(line, seq),
                command=command,
                reason=_one_of(line, reason, NACK_REASONS, "NACK reason"),
            )
        case [kind, *_] if kind in {"TM", "EV", "ACK", "NACK"}:
            raise ParseError(line, f"malformed {kind} line")
        case _:
            raise ParseError(line, "unknown line type")


class SeqTracker:
    """Counts lost lines from the firmware's sequence numbers.

    Every TM, EV, ACK and NACK line takes the next number. A gap means lines were
    lost, including lines that arrived but could not be parsed. A jump backwards
    means the board restarted (its counter starts at 0 at boot), so it starts a new
    count instead of counting as lost.
    """

    def __init__(self) -> None:
        self.lost = 0
        self.restarts = 0
        self._expected: int | None = None

    def feed(self, seq: int) -> int:
        """Records one sequence number; returns how many lines were lost before it."""
        gap = 0
        if self._expected is not None:
            if seq < self._expected:
                self.restarts += 1
            else:
                gap = seq - self._expected
        self.lost += gap
        self._expected = seq + 1
        return gap


def _uint(line: str, text: str) -> int:
    value = _int(line, text)
    if value < 0:
        raise ParseError(line, f"negative value {text!r}")
    return value


def _int(line: str, text: str) -> int:
    # int() would also accept " 12", "1_000" and "+12"; the firmware never sends those.
    if not text.removeprefix("-").isdigit() or not text.isascii():
        raise ParseError(line, f"not an integer: {text!r}")
    return int(text)


def _float(line: str, text: str) -> float:
    # The firmware sends plain decimals such as "-0.250"; refuse "nan", "1e3" and the like.
    digits = text.removeprefix("-").replace(".", "", 1)
    if not digits.isdigit() or not digits.isascii():
        raise ParseError(line, f"not a number: {text!r}")
    return float(text)


def _one_of(line: str, text: str, allowed: frozenset[str], what: str) -> str:
    if text not in allowed:
        raise ParseError(line, f"unknown {what} {text!r}")
    return text

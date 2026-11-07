"""What the ground station knows about the board, built from received lines.

No serial port and no plotting here: the window only draws this state, and the
tests feed it lines directly.
"""

from collections import deque
from dataclasses import dataclass
from typing import Literal

from ground import limits
from ground.protocol import (
    CalEvent,
    FaultEvent,
    Log,
    ModeEvent,
    Numbered,
    ParseError,
    SeqTracker,
    Telemetry,
    parse_line,
)

WINDOW_MS = 30_000  # telemetry kept for the plots, in board time
EVENTS_KEPT = 20


@dataclass(frozen=True, slots=True)
class Thresholds:
    """The sun-present thresholds on L + R, and how the ground station knows them.

    "assumed": the boot defaults, but the board may have run CAL before we connected.
    "boot": the boot defaults, and we saw the board boot.
    "cal": from an EV CAL line.
    """

    enter_mv: int
    exit_mv: int
    source: Literal["assumed", "boot", "cal"]


@dataclass(frozen=True, slots=True)
class Event:
    """A line worth listing for the operator: anything but telemetry."""

    host_time: float
    item: Numbered | Log | ParseError


class GroundState:
    def __init__(self, window_ms: int = WINDOW_MS, events_kept: int = EVENTS_KEPT) -> None:
        self.window_ms = window_ms
        self.telemetry: deque[Telemetry] = deque()
        self.events: deque[Event] = deque(maxlen=events_kept)
        self.mode: str | None = None  # unknown until the first TM or EV MODE
        self.fault: str | None = None
        self.thresholds = Thresholds(limits.SUN_ENTER_MV, limits.SUN_EXIT_MV, "assumed")
        self.bad_lines = 0
        self._seq = SeqTracker()

    @property
    def lost(self) -> int:
        return self._seq.lost

    @property
    def restarts(self) -> int:
        return self._seq.restarts

    def feed(self, host_time: float, line: str) -> Numbered | Log | None:
        """Takes one received line; returns its message, or None if it did not parse."""
        try:
            message = parse_line(line)
        except ParseError as error:
            self.bad_lines += 1
            self.events.append(Event(host_time, error))
            return None
        if isinstance(message, Log):
            self.events.append(Event(host_time, message))
            return message

        restarts = self._seq.restarts
        self._seq.feed(message.seq)
        if self._seq.restarts != restarts:
            self._board_restarted()

        match message:
            case Telemetry():
                self._add_telemetry(message)
                self.mode = message.mode
                self.fault = message.fault
                return message
            case ModeEvent(reason="boot"):
                self.thresholds = Thresholds(limits.SUN_ENTER_MV, limits.SUN_EXIT_MV, "boot")
                self.mode = message.to_mode
            case ModeEvent(from_mode="FAULT"):
                self.mode = message.to_mode
                self.fault = None
            case ModeEvent():
                self.mode = message.to_mode
            case FaultEvent():
                self.fault = message.code
            case CalEvent():
                self.thresholds = Thresholds(message.enter_mv, message.exit_mv, "cal")
        self.events.append(Event(host_time, message))
        return message

    def _add_telemetry(self, tm: Telemetry) -> None:
        self.telemetry.append(tm)
        oldest_ms = tm.t_ms - self.window_ms
        while self.telemetry[0].t_ms < oldest_ms:
            self.telemetry.popleft()

    def _board_restarted(self) -> None:
        # The board's time starts again at 0 and CAL is not kept over a restart.
        self.telemetry.clear()
        self.fault = None
        self.thresholds = Thresholds(limits.SUN_ENTER_MV, limits.SUN_EXIT_MV, "boot")

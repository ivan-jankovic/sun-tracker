"""The session log: every received and sent line, as CSV, so a session can be replayed.

The log keeps raw lines, not parsed values, so an improved parser can read old
logs again, including lines that were refused at the time.
"""

import csv
import time
from collections.abc import Iterator
from dataclasses import dataclass
from pathlib import Path
from typing import Literal, Self

type Direction = Literal["rx", "tx"]
HEADER = ("host_time", "dir", "line")


@dataclass(frozen=True, slots=True)
class Entry:
    host_time: float  # seconds since the epoch
    direction: Direction  # rx: from the board, tx: to the board
    line: str


class SessionLog:
    def __init__(self, path: Path) -> None:
        self.path = path
        self._file = path.open("w", newline="", encoding="utf-8")
        self._writer = csv.writer(self._file)
        self._writer.writerow(HEADER)

    @classmethod
    def create(cls, directory: Path) -> Self:
        """A new log in `directory`, named after the local start time."""
        directory.mkdir(parents=True, exist_ok=True)
        return cls(directory / time.strftime("%Y%m%d-%H%M%S.csv"))

    def rx(self, host_time: float, line: str) -> None:
        self._write(host_time, "rx", line)

    def tx(self, host_time: float, line: str) -> None:
        self._write(host_time, "tx", line)

    def close(self) -> None:
        self._file.close()

    def __enter__(self) -> Self:
        return self

    def __exit__(self, *exc: object) -> None:
        self.close()

    def _write(self, host_time: float, direction: Direction, line: str) -> None:
        self._writer.writerow((f"{host_time:.3f}", direction, line))
        self._file.flush()  # a crash or a pulled cable keeps everything up to here


def read_session(path: Path) -> Iterator[Entry]:
    with path.open(newline="", encoding="utf-8") as file:
        reader = csv.reader(file)
        if tuple(next(reader, ())) != HEADER:
            raise ValueError(f"not a session log: {path}")
        for host_time, direction, line in reader:
            if direction not in ("rx", "tx"):
                raise ValueError(f"unknown direction {direction!r} in {path}")
            yield Entry(float(host_time), direction, line)

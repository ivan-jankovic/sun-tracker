"""The USB serial link to the board.

A background thread reads lines, so the main thread stays free for the plot
window. Received lines wait in a queue with their arrival time.
"""

import queue
import threading
import time
from dataclasses import dataclass
from typing import Self

import serial

BAUD = 115200
READ_TIMEOUT_S = 0.1  # how long one read may wait, so close() is never stuck


@dataclass(frozen=True, slots=True)
class Received:
    host_time: float  # seconds since the epoch, when the line end arrived
    line: str  # without the line end


class SerialLink:
    def __init__(self, port: serial.SerialBase) -> None:
        self._port = port
        self._lines: queue.Queue[Received] = queue.Queue()
        self._stop = threading.Event()
        self._reader = threading.Thread(target=self._read_loop, name="serial-reader", daemon=True)
        self.error: OSError | None = None  # set if the port failed, e.g. cable pulled

    @classmethod
    def open(cls, url: str) -> Self:
        """Opens a port by name (/dev/cu.wchusbserial1440) or pyserial URL (loop://)."""
        return cls(serial.serial_for_url(url, baudrate=BAUD, timeout=READ_TIMEOUT_S))

    def start(self) -> None:
        self._reader.start()

    def send(self, command: str) -> str:
        """Sends one telecommand; returns the line as sent, without the line end."""
        if "\n" in command or "\r" in command:
            raise ValueError(f"a command is one line: {command!r}")
        self._port.write(command.encode("ascii") + b"\n")
        return command

    def receive(self, timeout: float | None = None) -> Received | None:
        """The next received line, or None if none arrives within the timeout."""
        try:
            return self._lines.get(timeout=timeout)
        except queue.Empty:
            return None

    def drain(self) -> list[Received]:
        """Every line received so far, without waiting."""
        lines = []
        while (received := self.receive(timeout=0)) is not None:
            lines.append(received)
        return lines

    @property
    def alive(self) -> bool:
        return self.error is None and not self._stop.is_set()

    def close(self) -> None:
        self._stop.set()
        if self._reader.is_alive():
            self._reader.join()
        self._port.close()

    def __enter__(self) -> Self:
        self.start()
        return self

    def __exit__(self, *exc: object) -> None:
        self.close()

    def _read_loop(self) -> None:
        # Not readline(): on a timeout it returns half a line, which would then be
        # parsed as two broken lines. Lines are split here, on complete line ends only.
        pending = b""
        while not self._stop.is_set():
            try:
                pending += self._port.read(self._port.in_waiting or 1)
            # Not only SerialException: in_waiting is a bare ioctl, which raises a plain
            # OSError when the device vanishes. SerialException is an OSError too.
            except OSError as error:
                self.error = error
                return
            *lines, pending = pending.split(b"\n")
            for raw in lines:
                # Non-ASCII bytes become U+FFFD, so the parser refuses the line.
                line = raw.decode("ascii", errors="replace").rstrip("\r")
                if line:
                    self._lines.put(Received(time.time(), line))

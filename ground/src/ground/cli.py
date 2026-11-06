"""`uv run ground`: connects to the board, logs the session, shows what arrives.

Until the plot window exists, received messages are printed in the terminal and
commands are typed there: one TM per second, every other line as it arrives.
"""

import argparse
import queue
import sys
import threading
import time
from pathlib import Path

from serial.tools import list_ports

from ground.link import SerialLink
from ground.log import SessionLog
from ground.protocol import Log, ParseError, SeqTracker, Telemetry, parse_line

POLL_S = 0.05
TM_EVERY = 10  # print one TM in ten: 1 per second at 10 Hz


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="ground", description="sun-tracker ground station")
    parser.add_argument("--port", help="serial port, e.g. /dev/cu.wchusbserial1440")
    parser.add_argument("--list", action="store_true", help="list serial ports and exit")
    parser.add_argument("--log-dir", type=Path, default=Path("logs"), help="default: ./logs")
    args = parser.parse_args(argv)

    if args.list:
        for port in list_ports.comports():
            print(f"{port.device}\t{port.description}")
        return 0
    if args.port is None:
        parser.error("--port is required (see --list)")

    with SerialLink.open(args.port) as link, SessionLog.create(args.log_dir) as log:
        print(f"# connected to {args.port}, logging to {log.path}. Type commands, Ctrl+C to quit.")
        typed = _read_stdin()
        seq = SeqTracker()
        tm_count = 0
        try:
            while link.alive:
                while not typed.empty():
                    log.tx(time.time(), link.send(typed.get()))
                for received in link.drain():
                    log.rx(received.host_time, received.line)
                    tm_count += _show(received.line, seq, tm_count)
                time.sleep(POLL_S)
        except KeyboardInterrupt:
            pass
        if link.error is not None:
            print(f"# link lost: {link.error}", file=sys.stderr)
            return 1
    print(f"# {seq.lost} lines lost, {seq.restarts} board restarts")
    return 0


def _show(line: str, seq: SeqTracker, tm_count: int) -> int:
    """Prints one received line; returns 1 if it was telemetry."""
    try:
        message = parse_line(line)
    except ParseError as error:
        print(f"! {error}")
        return 0
    if isinstance(message, Log):
        print(f"# {message.text}")
        return 0
    if (lost := seq.feed(message.seq)) > 0:
        print(f"! {lost} lines lost")
    if isinstance(message, Telemetry):
        if tm_count % TM_EVERY == 0:
            print(line)
        return 1
    print(line)
    return 0


def _read_stdin() -> queue.Queue[str]:
    """Typed commands, one per line, read in the background."""
    typed: queue.Queue[str] = queue.Queue()

    def read() -> None:
        for line in sys.stdin:
            if command := line.strip():
                typed.put(command)

    threading.Thread(target=read, name="stdin-reader", daemon=True).start()
    return typed

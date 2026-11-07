"""`uv run ground`: connects to the board, logs the session, shows what arrives.

By default it opens the plot window. With --text, received messages are printed
in the terminal instead: one TM per second, every other line as it arrives.
Either way, commands can be typed in the terminal, for anything without a
button. --replay shows a session log in the window.
"""

import argparse
import queue
import sys
import threading
import time
from pathlib import Path
from typing import TextIO

import serial
from serial.tools import list_ports

from ground.link import Received, SerialLink
from ground.log import SessionLog
from ground.protocol import Log, Telemetry
from ground.state import GroundState

POLL_S = 0.05
TM_EVERY = 10  # print one TM in ten: 1 per second at 10 Hz


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="ground", description="sun-tracker ground station")
    parser.add_argument("--port", help="serial port, e.g. /dev/cu.wchusbserial1440")
    parser.add_argument("--list", action="store_true", help="list serial ports and exit")
    parser.add_argument("--log-dir", type=Path, default=Path("logs"), help="default: ./logs")
    parser.add_argument("--text", action="store_true", help="terminal only, no plot window")
    parser.add_argument("--replay", type=Path, metavar="CSV", help="show a session log")
    args = parser.parse_args(argv)

    if args.list:
        _print_ports(sys.stdout)
        return 0
    # The window is imported only when needed, so --text and --list work without a display.
    if args.replay is not None:
        from ground.window import run_replay

        run_replay(args.replay)
        return 0
    if args.port is None:
        parser.error("--port or --replay is required (see --list)")

    try:
        link = SerialLink.open(args.port)
    except serial.SerialException as error:
        # Usually the board is unplugged or the name changed: show what is there instead.
        print(f"# cannot open {args.port}: {error.strerror or error}", file=sys.stderr)
        print("# ports found now (the board is cu.wchusbserial* or cu.usbserial-*):", file=sys.stderr)
        _print_ports(sys.stderr)
        return 1

    with link, SessionLog.create(args.log_dir) as log:
        print(f"# connected to {args.port}, logging to {log.path}")
        print("# Type commands here, Ctrl+C to quit.")
        typed = _read_stdin()
        try:
            if args.text:
                state = _run_text(link, log, typed)
            else:
                from ground.window import run_live

                state = run_live(link, log, typed)
        except KeyboardInterrupt:
            return 0
        if link.error is not None:
            print(f"# link lost: {link.error}", file=sys.stderr)
            return 1
    print(f"# {state.lost} lines lost, {state.bad_lines} bad, {state.restarts} board restarts")
    return 0


def _print_ports(out: TextIO) -> None:
    for port in list_ports.comports():
        print(f"{port.device}\t{port.description}", file=out)


def _run_text(link: SerialLink, log: SessionLog, typed: queue.Queue[str]) -> GroundState:
    state = GroundState()
    tm_count = 0
    try:
        while link.alive:
            while not typed.empty():
                log.tx(time.time(), link.send(typed.get()))
            for received in link.drain():
                log.rx(received.host_time, received.line)
                tm_count += _show(received, state, tm_count)
            time.sleep(POLL_S)
    except KeyboardInterrupt:
        pass
    return state


def _show(received: Received, state: GroundState, tm_count: int) -> int:
    """Prints one received line; returns 1 if it was telemetry."""
    lost = state.lost
    message = state.feed(received.host_time, received.line)
    if message is None:
        print(f"! {state.events[-1].item}")
        return 0
    if state.lost > lost:
        print(f"! {state.lost - lost} lines lost")
    if isinstance(message, Log):
        print(f"# {message.text}")
        return 0
    if isinstance(message, Telemetry):
        if tm_count % TM_EVERY == 0:
            print(received.line)
        return 1
    print(received.line)
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

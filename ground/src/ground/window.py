"""The plot window: live telemetry, status, events and the command buttons.

The window only draws a GroundState and hands commands to `send`, so the same
window serves the live link and a replayed session log.

A full redraw of this figure takes about 0.6 s on a Retina screen, too slow for
10 Hz and long enough to block the GUI thread. So the window blits: the axes,
grids and legends are drawn once into a background image, and each refresh only
pastes it back and draws the lines on top. This is why the time axis is fixed
(seconds before the latest telemetry) instead of scrolling, and why the text,
which is slow to lay out, is redrawn only when it changes.
"""

import math
import queue
import time
from collections.abc import Callable, Iterable
from pathlib import Path

import matplotlib.pyplot as plt
from matplotlib.artist import Artist
from matplotlib.axes import Axes
from matplotlib.backend_bases import DrawEvent
from matplotlib.text import Text
from matplotlib.widgets import Button, Slider

from ground import limits
from ground.link import SerialLink
from ground.log import Entry, SessionLog, read_session
from ground.protocol import ParseError
from ground.state import WINDOW_MS, GroundState, Thresholds

REFRESH_MS = 100  # the board sends TM at 10 Hz
EVENTS_SHOWN = 14
EVENT_CHARS = 54  # what fits the event list at its font size
SENSOR_MAX_MV = 6800  # L + R reaches at most 2 x 3300 mV

# Colours from the dataviz reference palette. L, R and L + R take the first three
# categorical slots, which stay apart for colour-blind viewers. Mode colours are
# status colours, always shown with the mode's name.
SURFACE = "#fcfcfb"
INK = "#0b0b0b"
INK_SECONDARY = "#52514e"
INK_MUTED = "#8a8984"
GRID = "#e4e3df"
DEADBAND_FILL = "#ecebe7"
BUTTON = "#ecebe7"
L_COLOUR, R_COLOUR, SUM_COLOUR = "#2a78d6", "#eb6834", "#1baf7a"
SIM_SUN_COLOUR = "#6250d6"
MODE_COLOURS = {"TRACKING": "#0ca30c", "SEARCH": "#d9d8d4", "SAFE": "#fab219", "FAULT": "#d03b3b"}
NO_MODE_COLOUR = "#ecebe7"

COMMANDS = ("SAFE", "SEARCH", "CAL", "CLEAR", "INJECT L", "INJECT R")
THRESHOLD_SOURCES = {
    "assumed": "boot defaults, not confirmed",
    "boot": "boot defaults",
    "cal": "from CAL",
}

type Send = Callable[[str], None]


class Window:
    def __init__(self, state: GroundState, send: Send | None, title: str) -> None:
        """`send` is None for a replay, and the command controls are then left out."""
        self.state = state
        self.link_status = "ok"
        self.link_lost = False  # then the board's mode is unknown, whatever it last sent
        self._send = send
        self._sim_sun_pending: float | None = None
        self._slider_synced_at: int | None = None  # board restarts count when last synced
        self._moving: list[Artist] = []  # redrawn on every refresh
        self._texts: list[Text] = []  # redrawn only when one of them changes
        self._background = None  # the figure without the two lists above
        self._text_layer = None  # the background with the texts

        # No navigation toolbar: zooming and panning fight the live refresh.
        with plt.rc_context({"toolbar": "None"}):
            self.fig = plt.figure(figsize=(13, 8), facecolor=SURFACE)
        if self.fig.canvas.manager is not None:
            self.fig.canvas.manager.set_window_title(title)
        self._build_plots()
        self._build_status()
        self.buttons: dict[str, Button] = {}
        if send is not None:
            self._build_commands()
        for artist in self._moving + self._texts:
            artist.set_animated(True)  # left out of full redraws
        self.fig.canvas.mpl_connect("draw_event", self._on_draw)

    # Plots, left: pointing, sensors, error, on one fixed time axis.

    def _build_plots(self) -> None:
        left, width, height = 0.07, 0.57, 0.25
        self.ax_angle = self.fig.add_axes((left, 0.71, width, height))
        self.ax_sensors = self.fig.add_axes((left, 0.39, width, height), sharex=self.ax_angle)
        self.ax_error = self.fig.add_axes((left, 0.07, width, height), sharex=self.ax_angle)
        self.ax_angle.set_xlim(-WINDOW_MS / 1000, 0)

        ax = self.ax_angle
        _style(ax, "Pointing", "angle (deg)")
        # The whole SIM SUN range, so a light behind the platform (-90) stays visible.
        ax.set_ylim(limits.SIM_SUN_MIN_DEG, limits.SIM_SUN_MAX_DEG)
        ax.set_yticks(range(-180, 181, 45))
        ax.axhspan(limits.SIM_SUN_MIN_DEG, limits.ANGLE_MIN_DEG, color=DEADBAND_FILL, lw=0,
                   label="out of reach")  # fmt: skip
        ax.axhspan(limits.ANGLE_MAX_DEG, limits.SIM_SUN_MAX_DEG, color=DEADBAND_FILL, lw=0)
        ax.axhline(limits.ANGLE_MIN_DEG, color=INK_MUTED, lw=1, ls="--", label="end stops")
        ax.axhline(limits.ANGLE_MAX_DEG, color=INK_MUTED, lw=1, ls="--")
        (self.angle_line,) = ax.plot([], [], color=INK, lw=2, label="commanded")
        (self.sun_line,) = ax.plot([], [], color=SIM_SUN_COLOUR, lw=2, label="simulated light")
        _legend(ax)

        ax = self.ax_sensors
        _style(ax, "Sensors", "mV")
        ax.set_ylim(0, SENSOR_MAX_MV)
        (self.l_line,) = ax.plot([], [], color=L_COLOUR, lw=2, label="L")
        (self.r_line,) = ax.plot([], [], color=R_COLOUR, lw=2, label="R")
        (self.sum_line,) = ax.plot([], [], color=SUM_COLOUR, lw=2, label="L + R")
        # The thresholds apply to L + R, so they are drawn against that line.
        self.enter_line = ax.axhline(0, color=INK_SECONDARY, lw=1, ls="--", label="sun enter")
        self.exit_line = ax.axhline(0, color=INK_SECONDARY, lw=1, ls=":", label="sun exit")
        _legend(ax)

        ax = self.ax_error
        # e = (L - R) / (L + R), named for readers who do not know control terms.
        _style(ax, "Sensor balance  (0 = facing the light)", "balance (e)")
        ax.set_ylim(-1.05, 1.05)
        for y, va, hint in (
            (0.98, "top", "+ more light on L: turn up"),
            (0.02, "bottom", "− more light on R: turn down"),
        ):
            ax.text(0.005, y, hint, transform=ax.transAxes, va=va, fontsize=8, color=INK_MUTED)
        ax.set_xlabel("seconds before the latest telemetry", color=INK_SECONDARY)
        ax.axhspan(-limits.DEADBAND, limits.DEADBAND, color=DEADBAND_FILL, lw=0, label="dead band")
        (self.error_line,) = ax.plot([], [], color=INK, lw=2, label="balance")
        _legend(ax)

        self._moving += [
            self.angle_line,
            self.sun_line,
            self.l_line,
            self.r_line,
            self.sum_line,
            self.enter_line,
            self.exit_line,
            self.error_line,
        ]

    # Status and events, right.

    def _build_status(self) -> None:
        x = 0.69
        self.mode_label = self.fig.text(
            x, 0.955, "", fontsize=16, weight="bold", color=INK, va="top",
            bbox={"boxstyle": "round,pad=0.4", "facecolor": NO_MODE_COLOUR, "edgecolor": "none"},
        )  # fmt: skip
        # One short line, changing on every refresh, so kept apart from the slow texts.
        self.clock_text = self.fig.text(
            0.98, 0.945, "", fontsize=9, family="monospace", color=INK_SECONDARY, ha="right",
            va="top",
        )  # fmt: skip
        self.status_text = self.fig.text(
            x, 0.88, "", fontsize=9, family="monospace", color=INK, va="top", linespacing=1.6
        )
        self.fig.text(x, 0.72, "Last events", fontsize=10, weight="bold", color=INK, va="top")
        self.events_text = self.fig.text(
            x, 0.69, "", fontsize=8, family="monospace", color=INK_SECONDARY, va="top",
            linespacing=1.5,
        )  # fmt: skip
        self._moving.append(self.clock_text)
        self._texts += [self.mode_label, self.status_text, self.events_text]

    # Commands, bottom right. The widgets must stay referenced, or they stop responding.

    def _build_commands(self) -> None:
        x, width, height = 0.69, 0.135, 0.045
        for i, command in enumerate(COMMANDS):
            column, row = i % 2, i // 2
            ax = self.fig.add_axes((x + column * (width + 0.01), 0.22 - row * 0.055, width, height))
            # No hover colour: the next refresh would paste the plain button back over it.
            button = Button(ax, command, color=BUTTON, hovercolor=BUTTON)
            button.on_clicked(lambda _event, c=command: self._command(c))
            self.buttons[command] = button

        ax = self.fig.add_axes((x + 0.06, 0.04, 0.18, 0.03))  # room on the right for "-180"
        self.sim_sun = Slider(
            ax, "SIM SUN", limits.SIM_SUN_MIN_DEG, limits.SIM_SUN_MAX_DEG, valinit=0, valstep=1,
            color=SIM_SUN_COLOUR, initcolor="none",
        )  # fmt: skip
        self.sim_sun.drawon = False  # drawn by the blit below, not by a full redraw per step
        self.sim_sun.on_changed(self._slider_moved)
        self._moving.append(self.sim_sun.ax)

    def _command(self, command: str) -> None:
        if self._send is not None:
            self._send(command)

    def _slider_moved(self, value: float) -> None:
        # Sent from flush_commands(), at most once per refresh, not on every drag step.
        self._sim_sun_pending = value
        self._blit()

    def flush_commands(self) -> None:
        """Sends the slider's latest position, if it moved since the last call."""
        if self._sim_sun_pending is not None:
            self._command(f"SIM SUN {self._sim_sun_pending:.0f}")
            self._sim_sun_pending = None

    def refresh(self) -> None:
        tms = self.state.telemetry
        latest_ms = tms[-1].t_ms if tms else 0
        t_s = [(m.t_ms - latest_ms) / 1000 for m in tms]
        self.angle_line.set_data(t_s, [m.angle_deg for m in tms])
        self.sun_line.set_data(t_s, [math.nan if m.sun_deg is None else m.sun_deg for m in tms])
        self.l_line.set_data(t_s, [m.l_mv for m in tms])
        self.r_line.set_data(t_s, [m.r_mv for m in tms])
        self.sum_line.set_data(t_s, [m.l_mv + m.r_mv for m in tms])
        self.error_line.set_data(t_s, [m.err for m in tms])

        self._sync_slider()
        thresholds = self.state.thresholds
        self.enter_line.set_ydata([thresholds.enter_mv] * 2)
        self.exit_line.set_ydata([thresholds.exit_mv] * 2)
        self.clock_text.set_text(f"board time {latest_ms / 1000:.1f} s" if tms else "")

        mode = None if self.link_lost else self.state.mode
        label = "LINK LOST" if self.link_lost else mode or "NO DATA"
        if self._set_text(self.mode_label, label):
            colour = MODE_COLOURS.get(mode or "", NO_MODE_COLOUR)
            self.mode_label.get_bbox_patch().set_facecolor(colour)
        self._set_text(self.status_text, self._status_lines(thresholds))
        self._set_text(self.events_text, "\n".join(_event_lines(self.state)))
        self._blit()

    def _sync_slider(self) -> None:
        """Moves the slider to the board's light angle, without sending it back.

        Once at the start and once after each board restart: the board may have kept
        an angle from an earlier session, and a restart puts it back to its default.
        After that the slider is the source, so a drag is never pulled back.
        """
        tms = self.state.telemetry
        if self._send is None or not tms or tms[-1].sun_deg is None:
            return
        if self._slider_synced_at == self.state.restarts:
            return
        self.sim_sun.eventson = False
        self.sim_sun.set_val(tms[-1].sun_deg)
        self.sim_sun.eventson = True
        self._slider_synced_at = self.state.restarts

    def _set_text(self, artist: Text, text: str) -> bool:
        if artist.get_text() == text:
            return False
        artist.set_text(text)
        self._text_layer = None
        return True

    def _status_lines(self, thresholds: Thresholds) -> str:
        state = self.state
        return "\n".join(
            [
                f"fault   {state.fault or 'none'}",
                f"sun     L+R enter {thresholds.enter_mv} / exit {thresholds.exit_mv} mV",
                f"        {THRESHOLD_SOURCES[thresholds.source]}",
                f"lines   lost {state.lost}, bad {state.bad_lines}, restarts {state.restarts}",
                f"link    {self.link_status}",
            ]
        )

    def _on_draw(self, _event: DrawEvent) -> None:
        # A full redraw happened (first show, resize): it left out the animated
        # artists, so take the new background and draw them on top.
        self._background = self.fig.canvas.copy_from_bbox(self.fig.bbox)
        self._text_layer = None
        self._blit()

    def _blit(self) -> None:
        if self._background is None:
            return  # not drawn yet: the first full redraw will call this again
        canvas = self.fig.canvas
        if self._text_layer is None:
            canvas.restore_region(self._background)
            for text in self._texts:
                self.fig.draw_artist(text)
            self._text_layer = canvas.copy_from_bbox(self.fig.bbox)
        else:
            canvas.restore_region(self._text_layer)
        for artist in self._moving:
            self.fig.draw_artist(artist)
        canvas.blit(self.fig.bbox)

    def run(self, tick: Callable[[], None]) -> None:
        """Calls `tick` every REFRESH_MS until the window is closed."""
        timer = self.fig.canvas.new_timer(interval=REFRESH_MS)
        timer.add_callback(tick)
        timer.start()
        plt.show()
        timer.stop()


def _style(ax: Axes, title: str, ylabel: str) -> None:
    ax.set_facecolor(SURFACE)
    ax.set_title(title, loc="left", fontsize=11, weight="bold", color=INK)
    ax.set_ylabel(ylabel, color=INK_SECONDARY)
    ax.tick_params(colors=INK_SECONDARY, labelsize=8)
    ax.grid(color=GRID, lw=0.8)
    for side in ("top", "right"):
        ax.spines[side].set_visible(False)
    for side in ("left", "bottom"):
        ax.spines[side].set_color(INK_MUTED)


def _legend(ax: Axes) -> None:
    # Above the plot, right of the title, so it never covers data.
    ax.legend(
        loc="lower right", bbox_to_anchor=(1, 0.98), fontsize=8, frameon=False, ncols=5,
        borderaxespad=0,
    )  # fmt: skip


def _event_lines(state: GroundState) -> list[str]:
    lines = []
    for event in list(state.events)[-EVENTS_SHOWN:]:
        clock = time.strftime("%H:%M:%S", time.localtime(event.host_time))
        mark = "!" if isinstance(event.item, ParseError) else " "
        text = event.line if len(event.line) <= EVENT_CHARS else event.line[: EVENT_CHARS - 1] + "…"
        lines.append(f"{clock}{mark}{text}")
    return lines


def run_live(link: SerialLink, log: SessionLog, typed: queue.Queue[str]) -> GroundState:
    """The window on the live link, until the window is closed.

    `typed` holds commands typed in the terminal, for anything without a button.
    """
    state = GroundState()

    def send(command: str) -> None:
        try:
            log.tx(time.time(), link.send(command))
        except OSError as error:  # SerialException included
            window.link_status = f"send failed: {error}"

    window = Window(state, send, title=f"sun-tracker ground station, log {log.path.name}")

    def tick() -> None:
        while not typed.empty():
            send(typed.get())
        for received in link.drain():
            log.rx(received.host_time, received.line)
            state.feed(received.host_time, received.line)
        window.flush_commands()
        if link.error is not None:
            window.link_status = f"LOST: {link.error}"
            window.link_lost = True
        window.refresh()

    window.refresh()
    window.run(tick)
    return state


class Replay:
    """Feeds the received lines of a session log at their recorded pace."""

    def __init__(self, entries: Iterable[Entry]) -> None:
        # Sent lines are skipped: the board's ACK or NACK shows what was sent.
        self._entries = [e for e in entries if e.direction == "rx"]
        self._next = 0

    @property
    def finished(self) -> bool:
        return self._next == len(self._entries)

    def feed_until(self, state: GroundState, elapsed_s: float) -> None:
        """Feeds every line recorded up to `elapsed_s` after the first one."""
        if self.finished:
            return
        until = self._entries[0].host_time + elapsed_s
        while not self.finished and self._entries[self._next].host_time <= until:
            entry = self._entries[self._next]
            state.feed(entry.host_time, entry.line)
            self._next += 1


def run_replay(path: Path) -> GroundState:
    """The window on a session log, without commands. It stays open at the end."""
    state = GroundState()
    replay = Replay(read_session(path))
    window = Window(state, None, title=f"sun-tracker replay, {path.name}")
    window.link_status = "replay"
    start = time.monotonic()

    def tick() -> None:
        replay.feed_until(state, time.monotonic() - start)
        window.link_status = "replay finished" if replay.finished else "replay"
        window.refresh()

    window.refresh()
    window.run(tick)
    return state

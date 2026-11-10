"""Smoke tests of the plot window, drawn off-screen with the Agg backend."""

import io
import warnings

import matplotlib

matplotlib.use("Agg")

import matplotlib.pyplot as plt  # noqa: E402
import pytest  # noqa: E402
from matplotlib.backend_bases import MouseEvent  # noqa: E402
from matplotlib.colors import to_rgba  # noqa: E402

from ground.log import Entry  # noqa: E402
from ground.state import GroundState  # noqa: E402
from ground.window import COMMANDS, EVENT_CHARS, NO_MODE_COLOUR, Replay, Window  # noqa: E402

SESSION = [
    "# sun-tracker boot",
    "EV,0,24,MODE,BOOT,SEARCH,boot",
    "TM,1,100,SEARCH,600,610,-0.008,92.3,-,40.0",
    "TM,2,200,SEARCH,620,630,-0.008,94.5,-,40.0",
    "EV,3,250,CAL,573,1173,873",
    "TM,4,300,FAULT,142,1900,-1.000,94.5,INJECTED_L,40.0",
    "EV,5,300,FAULT,INJECTED_L",
]


@pytest.fixture
def sent():
    return []


@pytest.fixture
def window(sent):
    state = GroundState()
    for i, line in enumerate(SESSION):
        state.feed(1000.0 + i, line)
    shown = Window(state, sent.append, title="test")
    shown.refresh()
    yield shown
    plt.close(shown.fig)


def click(window: Window, ax) -> None:
    canvas = window.fig.canvas
    x, y = ax.transAxes.transform((0.5, 0.5))
    for name in ("button_press_event", "button_release_event"):
        canvas.callbacks.process(name, MouseEvent(name, canvas, x, y, button=1))


def full_redraws(window: Window) -> list[object]:
    """Records every full redraw of the window from now on."""
    redraws: list[object] = []
    window.fig.canvas.mpl_connect("draw_event", redraws.append)
    return redraws


def test_a_session_draws_without_errors(window):
    window.fig.canvas.draw()  # full redraw, then the blitted lines and texts on top
    window.fig.savefig(io.BytesIO(), format="png")


def test_a_refresh_does_not_redraw_the_whole_figure(window):
    # A full redraw takes about 0.6 s on a Retina screen and blocks the GUI thread.
    window.fig.canvas.draw()
    redraws = full_redraws(window)
    window.state.feed(2000.0, "TM,6,400,SEARCH,700,710,-0.007,94.5,-,40.0")
    window.state.feed(2000.1, "EV,7,450,MODE,FAULT,SEARCH,cmd_clear")
    window.refresh()
    assert redraws == []


def test_a_refresh_after_the_window_closed_does_nothing(window):
    # Closing the window swaps in matplotlib's base canvas, which cannot blit,
    # and the refresh timer can still fire once after that.
    window.fig.canvas.draw()
    plt.close(window.fig)
    window.refresh()


def test_dragging_the_slider_does_not_redraw_the_whole_figure(window):
    window.fig.canvas.draw()
    redraws = full_redraws(window)
    window.sim_sun.set_val(30)
    window.fig.canvas.flush_events()
    assert redraws == []


def test_the_plots_show_the_telemetry_and_the_cal_thresholds(window):
    # Time runs up to 0, the latest telemetry.
    assert list(window.l_line.get_xdata()) == [-0.2, -0.1, 0.0]
    assert list(window.sum_line.get_ydata()) == [1210, 1250, 2042]
    assert list(window.enter_line.get_ydata()) == [1173, 1173]
    assert list(window.exit_line.get_ydata()) == [873, 873]


def test_the_status_shows_mode_fault_and_where_the_thresholds_come_from(window):
    assert window.mode_label.get_text() == "FAULT"
    status = window.status_text.get_text()
    assert "INJECTED_L" in status
    assert "from CAL" in status


def test_a_lost_link_replaces_the_last_mode(window):
    # The last mode received may no longer be true, so it is not shown as current.
    window.link_lost = True
    window.refresh()
    assert window.mode_label.get_text() == "LINK LOST"
    assert window.mode_label.get_bbox_patch().get_facecolor() == to_rgba(NO_MODE_COLOUR)


def test_every_command_button_sends_its_command(window, sent):
    for command in COMMANDS:
        click(window, window.buttons[command].ax)
    assert sent == list(COMMANDS)


def legend_entries(window: Window) -> list[str]:
    return [text.get_text() for text in window.ax_angle.get_legend().get_texts()]


@pytest.fixture
def board_window(sent):
    """A window fed by the real board, whose telemetry has no simulated light."""
    state = GroundState()
    state.feed(1000.0, "EV,0,24,MODE,BOOT,SEARCH,boot")
    state.feed(1000.1, "TM,1,100,SEARCH,600,610,-0.008,92.3,-,-")
    shown = Window(state, sent.append, title="test")
    shown.refresh()
    yield shown
    plt.close(shown.fig)


def test_the_simulation_build_shows_its_light_and_the_slider(window):
    assert "simulated light" in legend_entries(window)
    assert window.sim_sun.ax.get_visible()


def test_the_real_board_shows_no_simulated_light_and_no_slider(board_window):
    assert "simulated light" not in legend_entries(board_window)
    assert not board_window.sim_sun.ax.get_visible()


def test_the_hidden_slider_sends_nothing_when_clicked(board_window, sent):
    click(board_window, board_window.sim_sun.ax)
    board_window.flush_commands()
    assert sent == []


def test_a_change_of_build_redraws_the_window_once(board_window):
    board_window.fig.canvas.draw()
    redraws = full_redraws(board_window)
    board_window.state.feed(2000.0, "EV,0,24,MODE,BOOT,SEARCH,boot")
    board_window.state.feed(2000.1, "TM,1,100,SEARCH,600,610,-0.008,92.3,-,40.0")
    board_window.refresh()
    board_window.refresh()
    assert len(redraws) == 1
    assert board_window.sim_sun.ax.get_visible()


def test_the_slider_starts_where_the_board_has_its_light(window, sent):
    # SESSION ends with the light at 40.0
    assert window.sim_sun.val == 40.0
    window.flush_commands()
    assert sent == []


def test_after_the_first_sync_the_slider_is_not_pulled_back(window, sent):
    window.sim_sun.set_val(120)
    window.state.feed(2000.0, "TM,6,400,SEARCH,700,710,-0.007,94.5,-,40.0")
    window.refresh()
    assert window.sim_sun.val == 120


def test_the_slider_follows_a_board_restart(window, sent):
    window.sim_sun.set_val(120)
    window.state.feed(2000.0, "EV,0,24,MODE,BOOT,SEARCH,boot")
    window.state.feed(2000.1, "TM,1,100,SEARCH,700,710,-0.007,90.0,-,90.0")
    window.refresh()
    assert window.sim_sun.val == 90.0


def test_the_slider_sends_only_its_latest_position(window, sent):
    window.sim_sun.set_val(30)
    window.sim_sun.set_val(-45)
    window.flush_commands()
    window.flush_commands()
    assert sent == ["SIM SUN -45"]


def test_bad_and_long_lines_are_marked_and_cut_in_the_event_list(window):
    window.state.feed(2000.0, "TM,6,400,SAF")
    window.state.feed(2001.0, "# " + "x" * 100)
    window.refresh()
    *_, bad, long = window.events_text.get_text().splitlines()
    assert bad.endswith("!TM,6,400,SAF")
    assert len(long) == len("00:00:00 ") + EVENT_CHARS
    assert long.endswith("…")


def test_unprintable_characters_are_shown_as_question_marks(window):
    window.state.feed(2000.0, "i$\x0f�RTM,1715,171458,SAFE")
    window.refresh()
    assert window.events_text.get_text().splitlines()[-1].endswith("!i$??RTM,1715,171458,SAFE")
    with warnings.catch_warnings():
        warnings.simplefilter("error")  # a glyph missing from the font is a warning
        window.fig.canvas.draw()


def test_a_replay_window_has_no_commands():
    shown = Window(GroundState(), None, title="replay")
    shown.refresh()
    assert shown.buttons == {}
    plt.close(shown.fig)


def test_a_replay_feeds_received_lines_at_their_recorded_pace():
    state = GroundState()
    replay = Replay(
        [
            Entry(100.0, "rx", "ACK,0,PING"),
            Entry(100.5, "tx", "SAFE"),
            Entry(101.0, "rx", "ACK,1,SAFE"),
        ]
    )
    replay.feed_until(state, 0.5)
    assert [e.line for e in state.events] == ["ACK,0,PING"]
    assert not replay.finished
    replay.feed_until(state, 1.0)
    assert [e.line for e in state.events] == ["ACK,0,PING", "ACK,1,SAFE"]
    assert replay.finished

"""The serial link against pyserial's loop:// port, which returns whatever is written."""

import time

import pytest
import serial

from ground.link import READ_TIMEOUT_S, SerialLink

WAIT_S = 1.0


@pytest.fixture
def port():
    loop = serial.serial_for_url("loop://", timeout=READ_TIMEOUT_S)
    yield loop
    loop.close()


@pytest.fixture
def link(port):
    with SerialLink(port) as running:
        yield running


def test_a_sent_command_goes_out_as_one_line(link):
    assert link.send("SIM SUN 45") == "SIM SUN 45"
    assert link.receive(WAIT_S).line == "SIM SUN 45"


def test_a_line_split_across_reads_arrives_whole(port, link):
    port.write(b"TM,1,100,SEA")
    time.sleep(3 * READ_TIMEOUT_S)  # the reader times out in the middle of the line
    port.write(b"RCH,1000,1000,0.000,94.5,-,-\n")
    assert link.receive(WAIT_S).line == "TM,1,100,SEARCH,1000,1000,0.000,94.5,-,-"
    assert link.receive(0.2) is None


def test_crlf_is_removed_and_empty_lines_are_skipped(port, link):
    port.write(b"\r\nACK,1,PING\r\n\n")
    assert link.receive(WAIT_S).line == "ACK,1,PING"
    assert link.receive(0.2) is None


def test_several_lines_in_one_read_arrive_in_order(port, link):
    port.write(b"ACK,1,PING\nACK,2,PING\nACK,3,PING\n")
    time.sleep(0.2)
    assert [r.line for r in link.drain()] == ["ACK,1,PING", "ACK,2,PING", "ACK,3,PING"]


def test_received_lines_carry_their_arrival_time(port, link):
    before = time.time()
    port.write(b"ACK,1,PING\n")
    received = link.receive(WAIT_S)
    assert before <= received.host_time <= time.time()


def test_non_ascii_bytes_are_replaced_not_crashed_on(port, link):
    port.write(b"TM,\xff\xfe\n")
    assert link.receive(WAIT_S).line == "TM,��"


def test_the_first_line_after_opening_is_skipped(port):
    # The port opened in the middle of a line, after junk read at the wrong speed.
    with SerialLink(port, skip_first_line=True) as link:
        port.write(b"i$\x0f\xe2r\x8fRTM,1715,171458,SAFE\nACK,1716,PING\n")
        assert link.receive(WAIT_S).line == "ACK,1716,PING"
        assert link.receive(0.2) is None


def test_skipping_waits_for_the_first_line_end(port):
    with SerialLink(port, skip_first_line=True) as link:
        port.write(b"0.000,90.0,-,90.0")
        time.sleep(3 * READ_TIMEOUT_S)  # the reader times out before the line end
        port.write(b"\nACK,1,PING\nACK,2,PING\n")
        assert link.receive(WAIT_S).line == "ACK,1,PING"
        assert link.receive(WAIT_S).line == "ACK,2,PING"


def test_an_opened_port_skips_its_first_line():
    with SerialLink.open("loop://") as link:
        link.send("tail of a line")
        link.send("PING")
        assert link.receive(WAIT_S).line == "PING"


def test_a_command_with_a_line_end_is_refused(link):
    with pytest.raises(ValueError):
        link.send("SAFE\nCLEAR")


def test_close_stops_the_reader_promptly(port):
    link = SerialLink(port)
    link.start()
    start = time.monotonic()
    link.close()
    assert time.monotonic() - start < 2 * READ_TIMEOUT_S + 0.5
    assert not link.alive


class FailingPort:
    """A port that fails like a pulled USB cable."""

    in_waiting = 0

    def read(self, size: int) -> bytes:
        raise serial.SerialException("device disconnected")

    def close(self) -> None:
        pass


class VanishedPort(FailingPort):
    """A port whose device is gone: pyserial's in_waiting raises a plain OSError."""

    @property
    def in_waiting(self) -> int:
        raise OSError(6, "Device not configured")


def wait_until_dead(link: SerialLink) -> None:
    deadline = time.monotonic() + WAIT_S
    while link.alive and time.monotonic() < deadline:
        time.sleep(0.01)


def test_a_failing_port_is_reported_not_silent():
    link = SerialLink(FailingPort())
    link.start()
    wait_until_dead(link)
    assert not link.alive
    assert "disconnected" in str(link.error)
    link.close()


def test_a_vanished_device_is_reported_not_silent():
    link = SerialLink(VanishedPort())
    link.start()
    wait_until_dead(link)
    assert not link.alive
    assert "Device not configured" in str(link.error)
    link.close()

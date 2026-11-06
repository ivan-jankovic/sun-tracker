import re

import pytest

from ground.log import Entry, SessionLog, read_session


def test_a_new_log_is_named_after_its_start_time(tmp_path):
    with SessionLog.create(tmp_path / "logs") as log:
        assert re.fullmatch(r"\d{8}-\d{6}\.csv", log.path.name)
        assert log.path.parent == tmp_path / "logs"


def test_received_and_sent_lines_read_back_in_order(tmp_path):
    with SessionLog.create(tmp_path) as log:
        log.rx(1000.0, "EV,0,24,MODE,BOOT,SEARCH,boot")
        log.tx(1000.5, "SAFE")
        log.rx(1000.6, "ACK,1,SAFE")
    assert list(read_session(log.path)) == [
        Entry(1000.0, "rx", "EV,0,24,MODE,BOOT,SEARCH,boot"),
        Entry(1000.5, "tx", "SAFE"),
        Entry(1000.6, "rx", "ACK,1,SAFE"),
    ]


def test_lines_with_commas_and_quotes_survive_the_csv(tmp_path):
    raw = 'TM,1,100,SEARCH,1000,1000,"0.000",94.5,-,-'
    with SessionLog.create(tmp_path) as log:
        log.rx(1.0, raw)
    assert next(read_session(log.path)).line == raw


def test_each_line_is_on_disk_before_the_log_is_closed(tmp_path):
    log = SessionLog.create(tmp_path)
    log.rx(1.0, "ACK,1,PING")
    assert "ACK,1,PING" in log.path.read_text()
    log.close()


def test_times_are_kept_to_the_millisecond(tmp_path):
    with SessionLog.create(tmp_path) as log:
        log.rx(1762400000.1234, "ACK,1,PING")
    assert next(read_session(log.path)).host_time == 1762400000.123


def test_a_file_that_is_not_a_session_log_is_refused(tmp_path):
    other = tmp_path / "other.csv"
    other.write_text("a,b,c\n")
    with pytest.raises(ValueError):
        list(read_session(other))

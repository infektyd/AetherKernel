import pathlib
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts/serial"))

import serialdash


def test_text_rows_are_safe_for_curses_when_serial_contains_nul() -> None:
    dash = serialdash.Dashboard("/tmp/aether-test.log")

    dash._feed(b"hello\x00world\x1b[31m\n")
    rows = dash._visible_logical_lines()

    assert len(rows) == 1
    draw_text, entry, _attr = rows[0]
    assert entry is not None
    assert entry.data == b"hello\x00world\x1b[31m"
    assert "\x00" not in draw_text
    assert "\x1b" not in draw_text
    assert "hello.world.[31m" in draw_text


def test_hex_rows_preserve_raw_nul_byte() -> None:
    dash = serialdash.Dashboard("/tmp/aether-test.log")
    dash._feed(b"hello\x00world\n")
    dash.hex_mode = True

    rows = dash._visible_logical_lines()

    assert len(rows) == 1
    draw_text, entry, _attr = rows[0]
    assert entry is not None
    assert entry.data == b"hello\x00world"
    assert "68 65 6c 6c 6f 00 77 6f 72 6c 64" in draw_text


def test_feed_tracks_control_byte_telemetry_and_rate_samples() -> None:
    dash = serialdash.Dashboard("/tmp/aether-test.log")

    dash._feed(b"a\x00b\n")

    assert dash.total_bytes == 4
    assert dash.control_bytes == 1
    assert dash.nul_bytes == 1
    assert dash._recent_bytes() == 4
    assert dash._rate_sparkline(8)


def test_currentel_milestone_accepts_short_hex() -> None:
    dash = serialdash.Dashboard("/tmp/aether-test.log")

    dash._feed(b"CurrentEL = 0x4\n")

    assert dash.milestone_el1 is True


def test_currentel_milestone_accepts_padded_uart_hex() -> None:
    dash = serialdash.Dashboard("/tmp/aether-test.log")

    dash._feed(b"CurrentEL = 0x0000000000000004\n")

    assert dash.milestone_el1 is True


def test_poll_io_does_not_select_on_regular_log_fd(monkeypatch) -> None:
    with tempfile.NamedTemporaryFile() as f:
        f.write(b"")
        f.flush()
        dash = serialdash.Dashboard(f.name)
        assert dash.follower.try_open()
        log_fd = dash.follower.fd
        assert log_fd is not None
        captured_fds: list[int] = []

        def fake_select(fds, _write, _error, _timeout):
            captured_fds.extend(fds)
            return [], [], []

        monkeypatch.setattr(serialdash.select, "select", fake_select)

        dash.poll_io(0)

        assert log_fd not in captured_fds

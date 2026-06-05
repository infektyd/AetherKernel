import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

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

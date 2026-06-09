#!/usr/bin/env python3
"""Read-only curses dashboard for AetherKernel serial output via shared log file."""

from __future__ import annotations

import argparse
import collections
import curses
import os
import re
import select
import signal
import sys
import time

DEFAULT_LOG = "/tmp/aether-serial.log"
PARTIAL_FLUSH_S = 0.250
LIVE_THRESHOLD_S = 2.0
REDRAW_INTERVAL_S = 1.0 / 10.0
MAX_HISTORY_LINES = 10_000
HEX_BYTES_PER_ROW = 16
RECENT_WINDOW_S = 2.0
SPARKLINE_WINDOW_S = 8.0
SPARKLINE_LEVELS = " .:-=+*#@"

# Edit highlight patterns here (first matching rule wins).
HIGHLIGHT_RULES: list[tuple[re.Pattern[str], str]] = [
    (re.compile(r"EXCEPTION|ESR", re.I), "exception"),
    (re.compile(r"CurrentEL", re.I), "current_el"),
    (re.compile(r"AetherKernel", re.I), "banner"),
    (re.compile(r"beat\s+\d+", re.I), "beat"),
]

MILESTONE_EL1 = re.compile(r"CurrentEL\s*=\s*0x0*4\b", re.I)
MILESTONE_EXCEPTION = re.compile(r"EXCEPTION|ESR", re.I)


def _fmt_ts(ts: float) -> str:
    lt = time.localtime(ts)
    ms = int((ts % 1) * 1000)
    return time.strftime("%H:%M:%S", lt) + f".{ms:03d}"


def _line_highlight_attr(text: str, styles: dict[str, int]) -> int:
    for pattern, name in HIGHLIGHT_RULES:
        if pattern.search(text):
            return styles.get(name, 0)
    return 0


def _display_text(text: str) -> str:
    """Make decoded serial text safe for curses without changing raw bytes."""
    out: list[str] = []
    for ch in text:
        code = ord(ch)
        if ch == "\x00":
            out.append(".")
        elif ch == "\t" or code >= 32 and code != 127:
            out.append(ch)
        else:
            out.append(".")
    return "".join(out)


def _compact_path(path: str, max_len: int = 26) -> str:
    if len(path) <= max_len:
        return path
    keep = max(4, max_len - 3)
    return "..." + path[-keep:]


def _control_counts(data: bytes) -> tuple[int, int]:
    nul = data.count(0)
    control = sum(1 for b in data if b < 32 and b not in (9, 10, 13))
    return nul, control


def _hex_rows(data: bytes, width: int) -> list[str]:
    rows: list[str] = []
    for offset in range(0, len(data), HEX_BYTES_PER_ROW):
        chunk = data[offset : offset + HEX_BYTES_PER_ROW]
        hex_part = " ".join(f"{b:02x}" for b in chunk).ljust(HEX_BYTES_PER_ROW * 3 - 1)
        ascii_part = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
        line = f"{offset:08x}  {hex_part}  |{ascii_part}|"
        if len(line) > width and width > 0:
            line = line[: max(0, width - 1)]
        rows.append(line)
    return rows or [""]


class LineEntry:
    __slots__ = ("ts", "data")

    def __init__(self, ts: float, data: bytes) -> None:
        self.ts = ts
        self.data = data

    @property
    def text(self) -> str:
        return self.data.decode("utf-8", errors="replace")


class LogFollower:
    """Tail -f a log file without busy-spinning; never opens the serial device."""

    def __init__(self, path: str) -> None:
        self.path = path
        self._fd: int | None = None
        self._inode: int | None = None
        self._pos = 0

    @property
    def fd(self) -> int | None:
        return self._fd

    def waiting(self) -> bool:
        return self._fd is None

    def close(self) -> None:
        if self._fd is not None:
            try:
                os.close(self._fd)
            except OSError:
                pass
        self._fd = None
        self._inode = None
        self._pos = 0

    def try_open(self) -> bool:
        if not os.path.exists(self.path):
            return False
        try:
            fd = os.open(self.path, os.O_RDONLY)
            st = os.fstat(fd)
            os.lseek(fd, 0, os.SEEK_END)
            self.close()
            self._fd = fd
            self._inode = st.st_ino
            self._pos = os.lseek(fd, 0, os.SEEK_CUR)
            return True
        except OSError:
            self.close()
            return False

    def read_chunk(self) -> bytes:
        if self._fd is None:
            if not self.try_open():
                return b""
        try:
            st = os.fstat(self._fd)
        except OSError:
            self.close()
            return b""

        if st.st_ino != self._inode:
            self.close()
            if not self.try_open():
                return b""
            try:
                st = os.fstat(self._fd)
            except OSError:
                self.close()
                return b""

        if st.st_size < self._pos:
            self._pos = 0
            os.lseek(self._fd, 0, os.SEEK_SET)

        try:
            os.lseek(self._fd, self._pos, os.SEEK_SET)
            chunk = os.read(self._fd, 65536)
        except OSError:
            self.close()
            return b""

        if chunk:
            self._pos += len(chunk)
        return chunk


class Dashboard:
    def __init__(self, log_path: str) -> None:
        self.log_path = log_path
        self.follower = LogFollower(log_path)
        self.lines: collections.deque[LineEntry] = collections.deque(maxlen=MAX_HISTORY_LINES)
        self.partial = b""
        self.partial_last_byte = 0.0
        self.hex_mode = False
        self.scroll_up = 0
        self.running = True
        self.total_bytes = 0
        self.total_lines = 0
        self.first_byte_ts: float | None = None
        self.last_byte_ts: float | None = None
        self.milestone_output = False
        self.milestone_el1 = False
        self.milestone_exception = False
        self.status_msg = ""
        self.last_draw = 0.0
        self._styles: dict[str, int] = {}
        self.nul_bytes = 0
        self.control_bytes = 0
        self.byte_events: collections.deque[tuple[float, int]] = collections.deque()

    def _note_bytes(self, data: bytes) -> None:
        if not data:
            return
        now = time.time()
        if self.first_byte_ts is None:
            self.first_byte_ts = now
        self.last_byte_ts = now
        self.total_bytes += len(data)
        nul, control = _control_counts(data)
        self.nul_bytes += nul
        self.control_bytes += control
        self.byte_events.append((now, len(data)))
        self._trim_byte_events(now)

    def _trim_byte_events(self, now: float | None = None) -> None:
        now = now if now is not None else time.time()
        cutoff = now - SPARKLINE_WINDOW_S
        while self.byte_events and self.byte_events[0][0] < cutoff:
            self.byte_events.popleft()

    def _recent_bytes(self, window_s: float = RECENT_WINDOW_S) -> int:
        now = time.time()
        cutoff = now - window_s
        self._trim_byte_events(now)
        return sum(size for ts, size in self.byte_events if ts >= cutoff)

    def _bytes_per_second(self) -> float:
        if not self.byte_events:
            return 0.0
        now = time.time()
        self._trim_byte_events(now)
        if not self.byte_events:
            return 0.0
        span = max(1.0, min(SPARKLINE_WINDOW_S, now - self.byte_events[0][0]))
        return sum(size for _ts, size in self.byte_events) / span

    def _rate_sparkline(self, width: int) -> str:
        if width <= 0:
            return ""
        now = time.time()
        self._trim_byte_events(now)
        bucket_s = SPARKLINE_WINDOW_S / width
        buckets = [0] * width
        for ts, size in self.byte_events:
            age = max(0.0, now - ts)
            idx = width - 1 - int(age / bucket_s)
            if 0 <= idx < width:
                buckets[idx] += size
        peak = max(buckets)
        if peak <= 0:
            return "." * width
        max_level = len(SPARKLINE_LEVELS) - 1
        return "".join(SPARKLINE_LEVELS[round((value / peak) * max_level)] for value in buckets)

    def _complete_line(self, data: bytes, ts: float | None = None) -> None:
        if not data:
            return
        ts = ts if ts is not None else time.time()
        self.lines.append(LineEntry(ts, data))
        self.total_lines += 1
        self.milestone_output = True
        text = data.decode("utf-8", errors="replace")
        if MILESTONE_EL1.search(text):
            self.milestone_el1 = True
        if MILESTONE_EXCEPTION.search(text):
            self.milestone_exception = True

    def _feed(self, chunk: bytes) -> None:
        if not chunk:
            return
        self._note_bytes(chunk)
        self.partial += chunk
        self.partial_last_byte = time.time()
        while True:
            nl = self.partial.find(b"\n")
            if nl < 0:
                break
            line = self.partial[:nl]
            self.partial = self.partial[nl + 1 :]
            if line.endswith(b"\r"):
                line = line[:-1]
            self._complete_line(line)

    def _maybe_flush_partial(self) -> None:
        if not self.partial:
            return
        if time.time() - self.partial_last_byte >= PARTIAL_FLUSH_S:
            data, self.partial = self.partial, b""
            self._complete_line(data)

    def _is_live(self) -> bool:
        if self.last_byte_ts is None:
            return False
        return (time.time() - self.last_byte_ts) <= LIVE_THRESHOLD_S

    def _elapsed(self) -> str:
        if self.first_byte_ts is None:
            return "0.0s"
        return f"{time.time() - self.first_byte_ts:.1f}s"

    def _connection_state(self) -> str:
        if self.follower.waiting():
            return "waiting"
        return "live" if self._is_live() else "idle"

    def poll_io(self, timeout: float) -> None:
        self._feed(self.follower.read_chunk())
        self._maybe_flush_partial()

        fds: list[int] = []
        stdin_fd: int | None = None
        try:
            stdin_fd = sys.stdin.fileno()
        except (OSError, ValueError):
            stdin_fd = None
        if stdin_fd is not None:
            fds.append(stdin_fd)
        if not fds:
            time.sleep(timeout)
            return
        try:
            ready, _, _ = select.select(fds, [], [], timeout)
        except (ValueError, OSError):
            ready = []

        if stdin_fd in ready:
            return  # caller reads keys

    def handle_key(self, key: int, stdscr: curses.window) -> None:
        if key == -1:
            return
        if key in (ord("q"), ord("Q")):
            self.running = False
            return
        if key in (ord("h"), ord("H")):
            self.hex_mode = not self.hex_mode
            return
        if key in (ord("c"), ord("C")):
            self.lines.clear()
            self.partial = b""
            self.scroll_up = 0
            self.status_msg = "buffer cleared"
            return
        if key in (curses.KEY_PPAGE, ord("k")):
            self.scroll_up += 10
            return
        if key in (curses.KEY_NPAGE, ord("j")):
            self.scroll_up = max(0, self.scroll_up - 10)
            return
        if key in (curses.KEY_UP,):
            self.scroll_up += 1
            return
        if key in (curses.KEY_DOWN,):
            self.scroll_up = max(0, self.scroll_up - 1)
            return
        if key in (curses.KEY_END, ord("G"), ord("g")):
            self.scroll_up = 0
            return
        if key == curses.KEY_RESIZE:
            curses.update_lines_cols()

    def _visible_logical_lines(self) -> list[tuple[str, LineEntry | None, int]]:
        """Expand history to drawable rows (text or hex)."""
        out: list[tuple[str, LineEntry | None, int]] = []
        for entry in self.lines:
            text = entry.text
            attr = _line_highlight_attr(text, self._styles)
            if self.hex_mode:
                prefix = _fmt_ts(entry.ts) + " "
                for row in _hex_rows(entry.data, 200):
                    out.append((prefix + row, entry, attr))
            else:
                out.append((_fmt_ts(entry.ts) + " " + _display_text(text), entry, attr))
        return out

    def draw(self, stdscr: curses.window) -> None:
        try:
            curses.update_lines_cols()
        except curses.error:
            pass
        h, w = stdscr.getmaxyx()
        if h < 5 or w < 20:
            return

        milestone_row = h - 2
        help_row = h - 1
        main_top = 1
        main_bottom = h - 3
        main_h = main_bottom - main_top + 1

        stdscr.clear()
        conn = self._connection_state()
        if conn == "live":
            live_part = "● LIVE"
            live_attr = curses.A_BOLD
        elif conn == "idle":
            live_part = "○ idle"
            live_attr = 0
        else:
            live_part = "… waiting"
            live_attr = curses.A_DIM

        mode = "HEX" if self.hex_mode else "TEXT"
        recent = self._recent_bytes()
        rate = self._bytes_per_second()
        spark = self._rate_sparkline(10)
        header = (
            f" {_compact_path(self.log_path)}  {live_part}  {mode}  "
            f"B={self.total_bytes} L={self.total_lines}  "
            f"{rate:.0f}B/s burst={recent}B  nul={self.nul_bytes} ctl={self.control_bytes}  "
            f"[{spark}] {self._elapsed()} "
        )
        if self.scroll_up:
            header += f" [scroll +{self.scroll_up}]"
        header = _display_text(header).ljust(max(0, w - 1))[: max(0, w - 1)]
        try:
            stdscr.move(0, 0)
            stdscr.clrtoeol()
            stdscr.addstr(0, 0, header, live_attr)
        except curses.error:
            pass

        if self.follower.waiting() and not self.lines:
            msg = f"waiting for {self.log_path}…"
            try:
                stdscr.move(main_top, 0)
                stdscr.clrtoeol()
                stdscr.addstr(main_top, 0, _display_text(msg)[: max(0, w - 1)], curses.A_DIM)
            except curses.error:
                pass
        else:
            logical = self._visible_logical_lines()
            if self.scroll_up == 0:
                visible = logical[-main_h:] if len(logical) > main_h else logical
            else:
                end = len(logical) - self.scroll_up
                end = max(0, end)
                start = max(0, end - main_h)
                visible = logical[start:end]

            row = main_top
            for draw_text, _entry, attr in visible:
                if row > main_bottom:
                    break
                line = _display_text(draw_text)[: max(0, w - 1)]
                try:
                    stdscr.move(row, 0)
                    stdscr.clrtoeol()
                    stdscr.addstr(row, 0, line, attr)
                except curses.error:
                    pass
                row += 1
            while row <= main_bottom:
                try:
                    stdscr.move(row, 0)
                    stdscr.clrtoeol()
                except curses.error:
                    pass
                row += 1

        m1 = "[x]" if self.milestone_output else "[ ]"
        m2 = "[x]" if self.milestone_el1 else "[ ]"
        m3 = "[x]" if self.milestone_exception else "[ ]"
        milestones = f" {m1} output  {m2} EL1 CurrentEL  {m3} exception"
        help_line = " q quit  h hex  c clear  PgUp/Dn scroll  End/G follow"
        if self.status_msg:
            milestones = _display_text(self.status_msg)[: max(0, w - 1)]
            self.status_msg = ""
        for row, text in ((milestone_row, milestones), (help_row, help_line)):
            line = _display_text(text).ljust(max(0, w - 1))[: max(0, w - 1)]
            try:
                stdscr.move(row, 0)
                stdscr.clrtoeol()
                stdscr.addstr(row, 0, line, curses.A_DIM)
            except curses.error:
                pass
        stdscr.refresh()

    def setup_colors(self) -> None:
        if not curses.has_colors():
            return
        curses.start_color()
        curses.use_default_colors()
        curses.init_pair(1, curses.COLOR_CYAN, -1)
        curses.init_pair(2, curses.COLOR_GREEN, -1)
        curses.init_pair(3, curses.COLOR_RED, -1)
        curses.init_pair(4, curses.COLOR_WHITE, -1)
        self._styles = {
            "banner": curses.color_pair(1),
            "current_el": curses.color_pair(2) | curses.A_BOLD,
            "exception": curses.color_pair(3) | curses.A_BOLD,
            "beat": curses.color_pair(4) | curses.A_DIM,
        }

    def run_curses(self, stdscr: curses.window) -> None:
        curses.curs_set(0)
        stdscr.nodelay(True)
        stdscr.keypad(True)
        try:
            stdscr.idlok(False)
            stdscr.idcok(False)
        except curses.error:
            pass
        self.setup_colors()
        self.last_draw = 0.0

        def _on_sigwinch(_signum: int, _frame: object) -> None:
            try:
                curses.update_lines_cols()
            except curses.error:
                pass

        try:
            signal.signal(signal.SIGWINCH, _on_sigwinch)
        except (ValueError, OSError):
            pass

        while self.running:
            self.poll_io(REDRAW_INTERVAL_S)
            now = time.time()
            while True:
                key = stdscr.getch()
                if key == -1:
                    break
                self.handle_key(key, stdscr)

            if now - self.last_draw >= REDRAW_INTERVAL_S:
                self.draw(stdscr)
                self.last_draw = now

        self.draw(stdscr)
        self.follower.close()


def _main_loop(stdscr: curses.window, log_path: str) -> None:
    dash = Dashboard(log_path)
    dash.run_curses(stdscr)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Read-only AetherKernel serial log dashboard (tails shared log file)."
    )
    parser.add_argument(
        "--log",
        default=DEFAULT_LOG,
        help=f"Path to serial log file (default: {DEFAULT_LOG})",
    )
    args = parser.parse_args()
    try:
        curses.wrapper(_main_loop, args.log)
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

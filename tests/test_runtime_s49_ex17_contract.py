"""S49 EX-17: probe_shell timers attests printTimers schema atoms."""

import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]

TIMERS_PROBE_LINE = (
    'probe_shell "timers" '
    '"^timers now=.* freq=.* active_count=.* active_mask=.* sleep_deadline="'
)


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_s49_ex17_timers_probe_shell_present() -> None:
    net_iterate = read_repo("scripts/netboot/net-iterate.sh")

    assert "# probe shell: timers" in net_iterate
    assert TIMERS_PROBE_LINE in net_iterate
    assert "executor_deadline=" not in TIMERS_PROBE_LINE


def test_s49_ex17_timers_probe_after_status_unchanged() -> None:
    """status gate keeps timer_mask= presence only — do not weaken."""
    net_iterate = read_repo("scripts/netboot/net-iterate.sh")

    assert 'probe_shell "status" "^status uptime_ms=.*timer_mask="' in net_iterate
    status_idx = net_iterate.index('probe_shell "status"')
    timers_idx = net_iterate.index(TIMERS_PROBE_LINE)
    assert status_idx < timers_idx


def test_s49_ex17_help_lists_timers_probe() -> None:
    net_iterate = read_repo("scripts/netboot/net-iterate.sh")
    help_line = [
        line
        for line in net_iterate.splitlines()
        if line.strip().startswith('echo "shell probes:')
    ][0]

    assert " status timers " in help_line or help_line.startswith(
        '  echo "shell probes: ./serial-probe.sh status timers'
    )


def test_s49_ex17_print_timers_emits_required_atoms() -> None:
    shell = read_repo("Sources/Application/UARTShell.swift")

    assert "func printTimers()" in shell
    assert 'uartPuts("timers now=")' in shell
    assert 'uartPuts(" freq=")' in shell
    assert 'uartPuts(" active_count=")' in shell
    assert 'uartPuts(" active_mask=")' in shell
    assert 'uartPuts(" sleep_deadline=")' in shell
    assert "executor_deadline=" not in shell

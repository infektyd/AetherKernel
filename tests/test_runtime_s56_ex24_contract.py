"""S56 EX-24: probe_shell queues attests printQueues schema atoms."""

import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]

QUEUES_PROBE_LINE = (
    'probe_shell "queues" '
    '"^queues ready=.* sleepers=.* timer_mask="'
)


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_s56_ex24_queues_probe_shell_present() -> None:
    net_iterate = read_repo("scripts/netboot/net-iterate.sh")

    assert "# probe shell: queues" in net_iterate
    assert QUEUES_PROBE_LINE in net_iterate
    assert "delayed=" not in QUEUES_PROBE_LINE


def test_s56_ex24_queues_probe_after_timers() -> None:
    net_iterate = read_repo("scripts/netboot/net-iterate.sh")

    timers_idx = net_iterate.index('probe_shell "timers"')
    queues_idx = net_iterate.index(QUEUES_PROBE_LINE)
    assert timers_idx < queues_idx


def test_s56_ex24_print_queues_emits_required_atoms() -> None:
    shell = read_repo("Sources/Application/UARTShell.swift")

    assert "func printQueues()" in shell
    assert 'uartPuts("queues ready=")' in shell
    assert 'uartPuts(" sleepers=")' in shell
    assert 'uartPuts(" timer_mask=")' in shell
    assert "executor_ready_count()" in shell
    assert "executor_ready_capacity()" in shell
    assert "timerSleepPendingCount()" in shell
    assert "timerSleepCapacity()" in shell
    assert "delayed=" not in shell.split("func printQueues()")[1].split("func printTasks()")[0]

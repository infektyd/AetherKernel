import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_support_declares_shared_timer_arbiter_api() -> None:
    support = read_repo("Sources/Support/include/Support.h")

    for symbol in (
        "KERNEL_TIMER_CLIENT_SLEEP",
        "KERNEL_TIMER_CLIENT_EXECUTOR",
        "kernel_timer_now",
        "kernel_timer_set_deadline",
        "kernel_timer_clear_deadline",
        "kernel_timer_rearm",
    ):
        assert symbol in support


def test_timer_sleep_uses_fixed_multi_sleeper_queue() -> None:
    timer_sleep = read_repo("Sources/Application/TimerSleep.swift")

    assert "TIMER_SLEEP_CAPACITY: Int = 8" in timer_sleep
    assert "func timerSleepMillis(_ ms: UInt64) async" in timer_sleep
    assert "func timerSleepSeconds(_ secs: UInt64) async" in timer_sleep
    assert "TIMER SLEEP PANIC: sleep queue overflow" in timer_sleep
    assert "concurrent sleeper unsupported" not in timer_sleep


def test_executor_delay_hooks_schedule_against_shared_timer() -> None:
    executor = read_repo("Sources/Support/executor.c")

    assert "swift_task_enqueueGlobalWithDelayImpl" in executor
    assert "delay_schedule_ns" in executor
    assert "kernel_timer_set_deadline(KERNEL_TIMER_CLIENT_EXECUTOR" in executor
    assert "runtime delay hook unsupported" not in executor
    assert "runtime deadline hook unsupported" not in executor


def test_timer_irq_services_sleepers_and_executor_delays() -> None:
    irq = read_repo("Sources/Application/IRQHandler.swift")

    assert "serviceTimerSleepers()" in irq
    assert "executor_on_timer_irq()" in irq


def test_runtime_v2_demo_and_net_iterate_expect_machine_checkable_cadences() -> None:
    app = read_repo("Sources/Application/Application.swift")
    net_iterate = read_repo("scripts/netboot/net-iterate.sh")

    for marker in ("rtv2 fast ", "rtv2 slow ", "rtv2 long "):
        assert marker in app
        assert marker in net_iterate
    assert "async tick 0x0000000000000000" not in net_iterate

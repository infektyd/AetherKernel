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


def test_timer_sleep_header_matches_sealed_executor_timer_truth() -> None:
    timer_sleep = read_repo("Sources/Application/TimerSleep.swift")

    assert "executor owns its own delayed-job client" not in timer_sleep
    assert "EXECUTOR CNTP slot is never armed" in timer_sleep
    assert "executor delay/deadline hooks panic" in timer_sleep


def test_executor_delay_hooks_panic_instead_of_scheduling() -> None:
    executor = read_repo("Sources/Support/executor.c")
    kernel_executor = read_repo("Sources/Application/KernelExecutor.swift")

    # C trampolines still exist and forward into Swift-owned panic paths.
    assert "swift_task_enqueueGlobalWithDelayImpl" in executor
    assert "swift_task_enqueueGlobalWithDeadlineImpl" in executor
    assert "kernel_executor_enqueue_delay_ns" in executor
    assert "kernel_executor_enqueue_deadline_ns" in executor

    # Delay scheduler removed from executor.c (TimerSleep owns timed wakeups).
    assert "delay_schedule_ns" not in executor
    assert "kernel_timer_set_deadline(KERNEL_TIMER_CLIENT_EXECUTOR" not in executor

    # Swift side panics loudly so any runtime path that routes here fails visibly.
    assert (
        'executorPanic("delay enqueue is unsupported (proven dead; use TimerSleep)")'
        in kernel_executor
    )
    assert (
        'executorPanic("deadline enqueue is unsupported (proven dead; use TimerSleep)")'
        in kernel_executor
    )


def test_timer_irq_services_sleepers_executor_irq_is_noop() -> None:
    irq = read_repo("Sources/Application/IRQHandler.swift")
    kernel_executor = read_repo("Sources/Application/KernelExecutor.swift")
    shell = read_repo("Sources/Application/UARTShell.swift")

    assert "serviceTimerSleepers()" in irq
    assert "executor_on_timer_irq()" in irq

    # Delay queue gone: IRQ still calls through but kernel_executor_on_timer_irq is empty.
    assert "func kernel_executor_on_timer_irq() {\n}" in kernel_executor

    # EXECUTOR timer client is never armed — printTimers must not imply a live deadline.
    assert "executor_deadline=" not in shell


def test_support_h_does_not_claim_live_executor_delay_queue() -> None:
    support = read_repo("Sources/Support/include/Support.h")

    assert "executor's delayed jobs" not in support
    assert "Delay/deadline enqueue panic in Swift" in support


def test_concurrency_design_historical_delay_queue_blockquotes_labeled() -> None:
    design = read_repo("docs/CONCURRENCY_DESIGN.md")

    # Current Runtime V2 ground truth.
    assert "delay queue removed; timed wakeups live in TimerSleep only" in design
    assert "executor delay hooks panic; no delayed queue" in design

    # Historical Mach-O research quotes must be labeled superseded, not read as live behavior.
    for marker in (
        "Historical (superseded by Runtime V2 ground truth, S5/S36)",
        "Historical pump recipe (superseded by Runtime V2 ground truth, S5/S36)",
        "Historical (superseded by Runtime V2 ground truth, S5/S36/S40)",
        "historical: route to delay queue or enqueue if due (superseded",
        "historical plan: enqueue runs in task context, the timer IRQ only matures the delay queue",
        "historical plan: IRQ matures the delay queue",
    ):
        assert marker in design

    # V31 ground truth must not claim a live executor timer client.
    assert "sleep and executor timer" not in design
    assert "armed SLEEP client (TimerSleep)" in design
    assert "EXECUTOR CNTP slot never armed" in design


def test_runtime_v2_demo_and_net_iterate_expect_machine_checkable_cadences() -> None:
    app = read_repo("Sources/Application/Application.swift")
    net_iterate = read_repo("scripts/netboot/net-iterate.sh")

    for marker in ("rtv2 fast ", "rtv2 slow ", "rtv2 long "):
        assert marker in app
    for marker in ("rtv2 fast woke ", "rtv2 slow woke ", "rtv2 long woke "):
        assert marker in app
        assert marker in net_iterate
    assert "await timerSleepMillis(250)" in app
    assert "await timerSleepSeconds(1)" in app
    assert "await timerSleepSeconds(2)" in app
    assert app.index("await timerSleepMillis(250)") < app.index("rtv2 fast woke ")
    assert app.index("await timerSleepSeconds(1)") < app.index("rtv2 slow woke ")
    assert app.index("await timerSleepSeconds(2)") < app.index("rtv2 long woke ")
    assert "rtv2 fast woke 0x0000000000000000" in net_iterate
    assert "rtv2 slow woke 0x0000000000000000" in net_iterate
    assert "rtv2 long woke 0x0000000000000000" in net_iterate
    assert "rtv2 fast 0x0000000000000000" not in net_iterate
    assert "async tick 0x0000000000000000" not in net_iterate

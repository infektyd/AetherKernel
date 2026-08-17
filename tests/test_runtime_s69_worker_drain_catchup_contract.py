"""Drain/feed catch-up lives in proven only — same class as job_exec f955483."""

import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]

FEED_PROVEN = "int kernel_scheduler_timer_worker_feed_proven(void)"
WORKER_PROVEN = "int kernel_scheduler_secondary_worker_proven(void)"
FEED_SELFTEST = "int kernel_scheduler_timer_worker_feed_selftest(void)"
WORKER_SELFTEST = "int kernel_scheduler_secondary_worker_selftest(void)"


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def _function_body(source: str, signature: str, next_signature: str) -> str:
    start = source.index(signature)
    end = source.index(next_signature, start + len(signature))
    return source[start:end]


def test_s69_worker_proven_catch_up_not_selftest() -> None:
    scheduler = read_repo("Sources/Support/kernel_scheduler.c")
    feed_proven = _function_body(scheduler, FEED_PROVEN, WORKER_PROVEN)
    worker_proven = _function_body(
        scheduler, WORKER_PROVEN, "static void catch_up_secondary_job_imbalance(void)"
    )
    feed_selftest = _function_body(scheduler, FEED_SELFTEST, "int kernel_scheduler_secondary_job_selftest(void)")
    worker_selftest = _function_body(scheduler, WORKER_SELFTEST, FEED_SELFTEST)

    assert "catch_up_secondary_worker_imbalance()" in feed_proven
    assert "catch_up_secondary_worker_imbalance()" in worker_proven
    assert "catch_up_secondary_worker_imbalance()" not in feed_selftest
    assert "catch_up_secondary_worker_imbalance()" not in worker_selftest
    assert "kernel_scheduler_secondary_worker_imbalance() <= KERNEL_SCHEDULER_CORE_CAPACITY" in feed_proven
    assert "kernel_scheduler_secondary_worker_imbalance() <= KERNEL_SCHEDULER_CORE_CAPACITY" in worker_proven

    catch_start = scheduler.index("static void catch_up_secondary_worker_imbalance(void)")
    catch_end = scheduler.index(FEED_PROVEN, catch_start)
    catch_body = scheduler[catch_start:catch_end]
    assert "if ((spin & 0x3ffU) == 0U)" in catch_body
    assert "route_worker_feed_for_core(core_id)" in catch_body
    assert "catch_up_skip_handoff" in catch_body
    assert "5400000UL" in catch_body
    assert "200000U" in catch_body
    # HDMI/S69: no tight lock-poll of drain counters.
    assert "while (secondary_worker_needs_catch_up()" in catch_body


def test_s69_handoff_proven_catch_up_credits_handoff() -> None:
    scheduler = read_repo("Sources/Support/kernel_scheduler.c")
    shell = read_repo("Sources/Application/UARTShell.swift")
    proven = _function_body(
        scheduler,
        "int kernel_scheduler_secondary_handoff_proven(void)",
        "int kernel_scheduler_backpressure_proven(void)",
    )
    selftest_start = scheduler.index("int kernel_scheduler_secondary_handoff_selftest(void)")
    selftest = scheduler[selftest_start : selftest_start + 2500]
    assert "catch_up_secondary_handoff_imbalance()" in proven
    assert "catch_up_secondary_handoff_imbalance()" not in selftest
    catch_start = scheduler.index("static void catch_up_secondary_handoff_imbalance(void)")
    catch_end = scheduler.index("int kernel_scheduler_secondary_handoff_proven(void)", catch_start)
    catch_body = scheduler[catch_start:catch_end]
    assert "if ((spin & 0x3ffU) == 0U)" in catch_body
    assert "route_worker_feed_for_core(core_id)" in catch_body
    assert "catch_up_skip_handoff = 1" not in catch_body
    sched10 = shell.split("func printScheduler10()")[1].split("func ")[0]
    assert "kernel_scheduler_secondary_handoff_proven()" in sched10
    assert "kernel_scheduler_secondary_wake_proven()" in sched10
    assert "kernel_scheduler_fairness_selftest()" in sched10

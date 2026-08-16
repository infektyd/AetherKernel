"""S69 SCH-04: runqueue_proven must cover per-core leftover predicates."""

import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]

PROVEN_FN = "int kernel_scheduler_runqueue_proven(void)"
NEXT_FN = "int kernel_scheduler_selftest(void)"

PER_CORE_CHECKS = (
    "kernel_scheduler_runqueue_count(core_id) == 0",
    "kernel_scheduler_runqueue_high_water(core_id) >= KERNEL_SCHEDULER_RUNQUEUE_CAPACITY",
    "kernel_scheduler_runqueue_overflow_count(core_id) >= 1",
)

EXISTING_LEFTOVERS = (
    "kernel_scheduler_steal_total() >= 2U",
    "kernel_scheduler_runqueue_high_water_max() >= KERNEL_SCHEDULER_RUNQUEUE_CAPACITY",
)


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def _function_body(source: str, signature: str, next_signature: str | None = None) -> str:
    start = source.index(signature)
    if next_signature is not None:
        end = source.index(next_signature, start + len(signature))
    else:
        end = len(source)
    return source[start:end]


def test_s69_runqueue_proven_includes_per_core_leftovers() -> None:
    scheduler = read_repo("Sources/Support/kernel_scheduler.c")

    proven_body = _function_body(
        scheduler,
        PROVEN_FN,
        NEXT_FN,
    )

    for predicate in PER_CORE_CHECKS:
        assert predicate in proven_body, f"proven missing per-core check: {predicate}"

    for predicate in EXISTING_LEFTOVERS:
        assert predicate in proven_body, f"proven dropped leftover attestation: {predicate}"

    assert "kernel_scheduler_runqueue_selftest()" not in proven_body


def test_s69_bootcert_uses_runqueue_proven_not_selftest() -> None:
    shell = read_repo("Sources/Application/UARTShell.swift")
    bootcert = shell.split("func printBootcert()")[1].split("func ")[0]
    substrate = shell.split("func printSubstrateCertificate()")[1].split("func ")[0]

    assert "let queues = kernel_scheduler_runqueue_proven()" in bootcert
    assert "let queues = kernel_scheduler_runqueue_proven()" in substrate
    assert "kernel_scheduler_runqueue_selftest()" not in bootcert
    assert "kernel_scheduler_runqueue_selftest()" not in substrate


def test_s69_sched8_9_10_still_use_live_selftests() -> None:
    shell = read_repo("Sources/Application/UARTShell.swift")
    sched8 = shell.split("func printScheduler8()")[1].split("func ")[0]
    sched9 = shell.split("func printScheduler9()")[1].split("func ")[0]
    sched10 = shell.split("func printScheduler10()")[1].split("func ")[0]

    assert "kernel_scheduler_backpressure_selftest()" in sched8
    assert "kernel_scheduler_work_steal_selftest()" in sched9
    assert "kernel_scheduler_fairness_selftest()" in sched10

    assert "kernel_scheduler_backpressure_proven()" not in sched8
    assert "kernel_scheduler_work_steal_proven()" not in sched9
    assert "kernel_scheduler_fairness_proven()" not in sched10


def test_s69_timer_feed_is_lockstep_after_all_cores_online() -> None:
    scheduler = read_repo("Sources/Support/kernel_scheduler.c")
    feed_start = scheduler.index("static void route_worker_feed_for_online_secondary_cores(void)")
    feed_end = scheduler.index("void kernel_scheduler_on_timer_irq(void)", feed_start)
    feed_body = scheduler[feed_start:feed_end]

    assert "kernel_smp_online_count() != 4U" in feed_body
    assert "kernel_smp_online_mask() != 0xfU" in feed_body
    assert "secondary_queues_empty()" in feed_body
    assert "route_worker_feed_for_core(core_id)" in feed_body


def test_s69_steal_and_balance_do_not_credit_worker_drains() -> None:
    scheduler = read_repo("Sources/Support/kernel_scheduler.c")
    tick_start = scheduler.index("void kernel_scheduler_secondary_worker_tick(unsigned int core_id)")
    tick_end = scheduler.index("unsigned long kernel_scheduler_dispatch_count(unsigned int core_id)", tick_start)
    tick_body = scheduler[tick_start:tick_end]

    steal = tick_body.index("kernel_scheduler_try_steal_work(core_id)")
    balance = tick_body.index("kernel_scheduler_try_balance_work(core_id)")
    own_drain = tick_body.index("cores[core_id].worker_drains++")
    assert tick_body.count("cores[core_id].worker_drains++") == 1
    assert steal < own_drain
    assert balance < own_drain
    assert "try_steal_work" in tick_body
    assert "try_balance_work" in tick_body


def test_s69_secondary_wait_and_worker_selftest_do_not_lock_poll() -> None:
    scheduler = read_repo("Sources/Support/kernel_scheduler.c")

    wait_start = scheduler.index("static void wait_for_secondary_queues_empty(void)")
    wait_end = scheduler.index("static void route_dispatch_for_core(", wait_start)
    wait_body = scheduler[wait_start:wait_end]
    assert wait_body.index("if ((spin & 0x3ffU) == 0U)") < wait_body.index(
        "secondary_queues_empty()"
    )

    sw_start = scheduler.index("int kernel_scheduler_secondary_worker_selftest(void)")
    sw_end = scheduler.index("int kernel_scheduler_timer_worker_feed_selftest(void)", sw_start)
    sw_body = scheduler[sw_start:sw_end]
    sample = sw_body.index("if ((spin & 0x3ffU) == 0U)")
    drain = sw_body.index("kernel_scheduler_worker_drain_count(1) > drain_before[1]")
    assert sample < drain

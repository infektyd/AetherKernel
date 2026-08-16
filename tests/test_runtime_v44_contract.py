import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]


COMMANDS_V44 = (
    "commands=help,protocol,status,heap,queues,tasks,tasks2,kobjects,drivers,drivercheck,"
    "mailboxes,sendtest,supervisor,health,capcheck,events,runtime,agent,certificate,sched,sched2,sched3,sched4,sched5,sched6,sched7,sched8,sched9,sched10,sched11,sched12,cores,locks,runqueues,diag,irqs,timers,"
    "memcheck,faults,retained,retained-clear,memmap,mmu,pools,poolcheck,"
    "heapfrag,poolstats,frames,heapcheck,framecheck,stress,frameprobe,bootcert,"
    "canceltest,taskcheck,channeltest,bootcheck,soak,heap-invalid-free-test,"
    "heap-double-free-test,panic-test,fault-test,reboot"
)


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_runtime_v44_concurrency_soak_contract_exists() -> None:
    support = read_repo("Sources/Support/include/Support.h")
    scheduler = read_repo("Sources/Support/kernel_scheduler.c")

    for marker in (
        "Runtime V44 bounded SMP concurrency soak protocol",
        "#define KERNEL_SCHEDULER_VERSION 46U",
        "#define KERNEL_SCHEDULER_CONCURRENCY_SOAK_ROUNDS 3U",
        "void kernel_scheduler_enable_concurrency_soak(void);",
        "unsigned int kernel_scheduler_concurrency_soak_enabled(void);",
        "unsigned long kernel_scheduler_concurrency_soak_round_total(void);",
        "unsigned long kernel_scheduler_concurrency_soak_completion_total(void);",
        "unsigned long kernel_scheduler_concurrency_soak_failure_total(void);",
        "unsigned long kernel_scheduler_concurrency_soak_dispatch_total(void);",
        "int kernel_scheduler_concurrency_soak_selftest(void);",
        "int kernel_scheduler_concurrency_soak_proven(void);",
    ):
        assert marker in support

    for marker in (
        "Runtime V44 bounded SMP concurrency soak protocol",
        "concurrency_soak_enabled",
        "concurrency_soak_rounds",
        "concurrency_soak_completions",
        "concurrency_soak_failures",
        "concurrency_soak_dispatch_total",
        "kernel_scheduler_enable_concurrency_soak",
        "kernel_scheduler_concurrency_soak_selftest",
        "kernel_scheduler_concurrency_soak_proven",
        "KERNEL_SCHEDULER_CONCURRENCY_SOAK_ROUNDS",
        "kernel_scheduler_concurrency_soak_round_total() >= KERNEL_SCHEDULER_CONCURRENCY_SOAK_ROUNDS",
        "kernel_scheduler_concurrency_soak_failure_total() == 0U",
        "kernel_scheduler_concurrency_soak_completion_total() >= KERNEL_SCHEDULER_CONCURRENCY_SOAK_ROUNDS",
        "kernel_scheduler_concurrency_soak_dispatch_total() > 0U",
        "reset_concurrency_soak_progress",
    ):
        assert marker in scheduler


def test_runtime_v44_application_boot_marker_and_selftest_exist() -> None:
    app = read_repo("Sources/Application/Application.swift")

    for marker in (
        "Runtime V44 adds bounded SMP concurrency soak under active scheduler load.",
        "kernel_event_emit(KERNEL_EVENT_KIND_BOOT, 64, 0, 0)",
        "runtime v44: bounded smp concurrency soak",
        "kernel_scheduler_enable_concurrency_soak()",
        "let runtimeV44 = kernel_scheduler_concurrency_soak_selftest()",
        "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 44, UInt(runtimeV44), UInt(kernel_scheduler_concurrency_soak_round_total()))",
    ):
        assert marker in app


def test_runtime_v44_shell_sched12_and_certificate_surface_exist() -> None:
    shell = read_repo("Sources/Application/UARTShell.swift")

    for marker in (
        COMMANDS_V44,
        "func printScheduler12()",
        "sched12 ok=",
        " version=44",
        " concurrency=",
        " rounds=",
        " completions=",
        " failures=",
        " dispatches=",
        " total=",
        " capacity=",
        " soak_core1=",
        " soak_core2=",
        " soak_core3=",
        " selftest=",
        'shellBufferSliceEquals(commandStart, commandLen, "sched12")',
    ):
        assert marker in shell

    for marker in (
        "let concurrency = kernel_scheduler_concurrency_soak_selftest()",
        "version=44",
        " concurrency=",
        "concurrency != 0",
        "let concurrency = kernel_scheduler_concurrency_soak_proven()",
        "printScheduler12()",
    ):
        assert marker in shell


def test_runtime_v44_netboot_gates_and_sched12_probe_exist() -> None:
    net_iterate = read_repo("scripts/netboot/net-iterate.sh")
    doctor = read_repo("scripts/netboot/netboot-doctor.sh")
    soak_loop = read_repo("scripts/soak-loop.sh")

    for source in (net_iterate, doctor):
        assert COMMANDS_V44 in source

    assert "schedselftest ok=1 version=44" in net_iterate
    assert "schedselftest ok=1 version=44" in doctor

    for marker in (
        "probe shell: sched12",
        "^sched12 ok=1 version=44 .*concurrency=1 .*rounds=3 .*completions=3 .*failures=0 .*dispatches=[1-9][0-9]* .*total=0 .*capacity=8 .*soak_core1=[1-9][0-9]* .*soak_core2=[1-9][0-9]* .*soak_core3=[1-9][0-9]* .*selftest=1",
        "probe shell: req-sched12",
        "^resp id=45 ok=1 cmd=sched12 end",
        "^bootcert ok=1 version=66 .*concurrency=1 .*priority=1 .*fairness=1 .*stealing=[01] .*backpressure=[01] .*handoff=[01] .*wake=[01] .*job_exec=[01] .*worker_feed=[01] .*secondary_workers=[01] .*preemptive=[01] .*smp_scheduler=[01] .*atomics=1 .*locks=1 .*queues=1 .*smp=1 .*scheduler=1 .*certificate=1 .*agent=1 .*runtime=1 .*syscall=1 .*uaccess=1 .*usermode=1 .*process=1 .*loader=1 .*multiprocess=1 .*sdhci=1 .*card=1 .*block=1 .*fat32=1 .*mailbox=1 .*framebuf=1 .*console=1 .*pcie=1 .*vl805=1 .*xhci=1 .*kbd=[01] .*swift=6.3.2 .*events_lost=0",
        "^certificate ok=1 version=63 substrate=1 .*bootcert=[01] .*concurrency=1 .*priority=1 .*fairness=1 .*stealing=[01] .*backpressure=[01] .*handoff=[01] .*wake=[01] .*job_exec=[01] .*worker_feed=[01] .*secondary_workers=[01] .*preemptive=[01] .*smp_scheduler=[01] .*atomics=1 .*locks=1 .*queues=1 .*smp=1 .*scheduler=1 .*agent=1 .*runtime=1 .*memory=1 .*objects=1 .*tasks=1 .*mailboxes=1 .*supervisor=1 .*handles=1 .*events=[0-9]+ .*cancellations=1 .*channels=1 .*drivers=1 .*pressure=1 .*pools=1 .*mmu=1 .*vmm=1 .*asplit=1 .*el0=1 .*syscall=1 .*uaccess=1 .*usermode=1 .*process=1 .*loader=1 .*multiprocess=1 .*sdhci=1 .*card=1 .*block=1 .*fat32=1 .*mailbox=1 .*framebuf=1 .*console=1 .*pcie=1 .*vl805=1 .*xhci=1 .*swift=6.3.2 .*events_lost=0",
        "stale pre-V44 SD fallback",
    ):
        assert marker in net_iterate

    for marker in (
        "cmd=sched12",
        "^sched12 ok=1 version=44 .*concurrency=1 .*rounds=3 .*completions=3 .*failures=0 .*dispatches=[1-9][0-9]* .*total=0 .*capacity=8 .*soak_core1=[1-9][0-9]* .*soak_core2=[1-9][0-9]* .*soak_core3=[1-9][0-9]* .*selftest=1",
    ):
        assert marker in soak_loop


def test_runtime_v44_concurrency_soak_no_fabricated_drain_credit() -> None:
    scheduler = read_repo("Sources/Support/kernel_scheduler.c")
    soak_start = scheduler.index("int kernel_scheduler_concurrency_soak_selftest(void)")
    soak_end = scheduler.index("int kernel_scheduler_smp_selftest(void)", soak_start)
    soak_body = scheduler[soak_start:soak_end]

    assert "drained = 1" not in soak_body
    assert "if (drained[core_id] == 0)" in soak_body
    assert soak_body.count("concurrency_soak_failures++") >= 2


def test_runtime_v44_secondary_worker_and_seed_selftests_require_live_delta() -> None:
    scheduler = read_repo("Sources/Support/kernel_scheduler.c")

    sw_start = scheduler.index("int kernel_scheduler_secondary_worker_selftest(void)")
    sw_end = scheduler.index("int kernel_scheduler_timer_worker_feed_selftest(void)", sw_start)
    sw_body = scheduler[sw_start:sw_end]
    enqueue_pos = sw_body.index("enqueue_worker_probe_for_core")
    drain_before_pos = sw_body.index("drain_before[core_id] = kernel_scheduler_worker_drain_count")
    assert drain_before_pos < enqueue_pos
    assert "kernel_scheduler_worker_drain_count(1) > drain_before[1]" in sw_body
    assert "worker_feed_count(core_id) == 0" not in sw_body

    feed_start = scheduler.index("int kernel_scheduler_timer_worker_feed_selftest(void)")
    feed_end = scheduler.index("int kernel_scheduler_secondary_job_selftest(void)", feed_start)
    feed_body = scheduler[feed_start:feed_end]
    route_pos = feed_body.index("route_worker_feed_for_core(core_id)")
    feed_before_pos = feed_body.index("feed_before[core_id] = kernel_scheduler_worker_feed_count")
    assert feed_before_pos < route_pos
    assert "kernel_scheduler_worker_feed_count(1) > feed_before[1]" in feed_body
    assert "kernel_scheduler_worker_drain_count(1) > drain_before[1]" in feed_body

    job_start = scheduler.index("int kernel_scheduler_secondary_job_selftest(void)")
    job_end = scheduler.index("int kernel_scheduler_secondary_wake_selftest(void)", job_start)
    job_body = scheduler[job_start:job_end]
    job_route_pos = job_body.index("route_worker_feed_for_core(core_id)")
    exec_before_pos = job_body.index(
        "exec_before[core_id] = kernel_scheduler_secondary_job_execution_count"
    )
    assert exec_before_pos < job_route_pos
    assert "kernel_scheduler_secondary_job_execution_count(1) > exec_before[1]" in job_body
    assert "kernel_scheduler_secondary_job_completion_count(1) > compl_before[1]" in job_body

    wake_start = scheduler.index("int kernel_scheduler_secondary_wake_selftest(void)")
    wake_end = scheduler.index("int kernel_scheduler_secondary_handoff_selftest(void)", wake_start)
    wake_body = scheduler[wake_start:wake_end]
    wake_route_pos = wake_body.index("route_worker_feed_for_core(core_id)")
    signal_before_pos = wake_body.index("signal_before = kernel_scheduler_secondary_wake_signal_total()")
    assert signal_before_pos < wake_route_pos
    assert "kernel_scheduler_secondary_wake_signal_total() > signal_before" in wake_body
    assert "kernel_scheduler_secondary_wake_ack_count(1) > ack_before[1]" in wake_body

    handoff_start = scheduler.index("int kernel_scheduler_secondary_handoff_selftest(void)")
    handoff_body = scheduler[handoff_start:]
    handoff_route_pos = handoff_body.index("route_worker_feed_for_core(core_id)")
    issue_before_pos = handoff_body.index(
        "issue_before[core_id] = kernel_scheduler_secondary_handoff_issue_count"
    )
    assert issue_before_pos < handoff_route_pos
    assert "kernel_scheduler_secondary_handoff_issue_count(1) > issue_before[1]" in handoff_body
    assert (
        "kernel_scheduler_secondary_handoff_completion_count(1) > completion_before[1]"
        in handoff_body
    )


def test_runtime_v44_docs_are_updated_after_hardware_proof() -> None:
    readme = read_repo("README.md")
    runbook = read_repo("docs/RUNBOOK.md")
    design = read_repo("docs/CONCURRENCY_DESIGN.md")

    for source in (readme, runbook, design):
        assert "Runtime V44 bounded SMP concurrency soak protocol" in source
        assert "bootcert ok=1 version=66" in source
        assert "certificate ok=1 version=63" in source
        assert "concurrency=1" in source
        assert "sched12 ok=1 version=44" in source
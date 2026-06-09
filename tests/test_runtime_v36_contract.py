import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]


COMMANDS_V36 = (
    "commands=help,protocol,status,heap,queues,tasks,tasks2,kobjects,drivers,drivercheck,"
    "mailboxes,sendtest,supervisor,health,capcheck,events,runtime,agent,certificate,sched,sched2,sched3,sched4,sched5,sched6,sched7,sched8,sched9,sched10,sched11,sched12,cores,locks,runqueues,diag,irqs,timers,"
    "memcheck,faults,retained,retained-clear,memmap,mmu,pools,poolcheck,"
    "heapfrag,poolstats,frames,heapcheck,framecheck,stress,frameprobe,bootcert,"
    "canceltest,taskcheck,channeltest,bootcheck,soak,heap-invalid-free-test,"
    "heap-double-free-test,panic-test,fault-test,reboot"
)


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_runtime_v36_timer_worker_feed_contract_exists() -> None:
    support = read_repo("Sources/Support/include/Support.h")
    scheduler = read_repo("Sources/Support/kernel_scheduler.c")

    for marker in (
        "Runtime V36 timer-fed secondary scheduler workers",
        "#define KERNEL_SCHEDULER_VERSION 44U",
        "void kernel_scheduler_enable_timer_worker_feed(void);",
        "unsigned int kernel_scheduler_timer_worker_feed_enabled(void);",
        "unsigned long kernel_scheduler_worker_feed_count(unsigned int core_id);",
        "unsigned long kernel_scheduler_worker_feed_drop_count(unsigned int core_id);",
        "unsigned long kernel_scheduler_total_worker_feed_count(void);",
        "unsigned long kernel_scheduler_total_worker_feed_drop_count(void);",
        "unsigned long kernel_scheduler_secondary_worker_feed_total(void);",
        "unsigned long kernel_scheduler_secondary_worker_feed_min(void);",
        "unsigned long kernel_scheduler_secondary_worker_feed_max(void);",
        "unsigned long kernel_scheduler_secondary_worker_feed_imbalance(void);",
        "unsigned long kernel_scheduler_worker_feed_drain_gap(void);",
        "int kernel_scheduler_timer_worker_feed_selftest(void);",
    ):
        assert marker in support

    for marker in (
        "Runtime V36 timer-fed secondary scheduler workers",
        "timer_worker_feed_enabled",
        "worker_feeds",
        "worker_feed_drops",
        "route_worker_feed_for_core",
        "unsigned int token = worker_token_for_core(core_id)",
        "kernel_scheduler_timer_worker_feed_selftest",
    ):
        assert marker in scheduler


def test_runtime_v36_application_boot_marker_and_selftest_exist() -> None:
    app = read_repo("Sources/Application/Application.swift")

    for marker in (
        "Runtime V36 adds timer-fed secondary scheduler workers.",
        "kernel_event_emit(KERNEL_EVENT_KIND_BOOT, 44, 0, 0)",
        "kernel_scheduler_enable_timer_worker_feed()",
        "runtime v36: timer-fed secondary scheduler workers",
        "let runtimeV36 = kernel_scheduler_timer_worker_feed_selftest()",
        "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 36, UInt(runtimeV36), UInt(kernel_scheduler_secondary_worker_feed_total()))",
    ):
        assert marker in app


def test_runtime_v36_shell_sched4_and_certificate_surface_exist() -> None:
    shell = read_repo("Sources/Application/UARTShell.swift")

    for marker in (
        COMMANDS_V36,
        "func printScheduler4()",
        "sched4 ok=",
        " version=36",
        " worker_feed=",
        " secondary_workers=",
        " feeds=",
        " drains=",
        " drops=",
        " gap=",
        " feed_imbalance=",
        " drain_imbalance=",
        " core0_feed=",
        " core1_feed=",
        " core2_feed=",
        " core3_feed=",
        " core0_drain=",
        " core1_drain=",
        " core2_drain=",
        " core3_drain=",
        " selftest=",
        'shellBufferSliceEquals(commandStart, commandLen, "sched4")',
    ):
        assert marker in shell

    for marker in (
        "let workerFeed = kernel_scheduler_timer_worker_feed_selftest()",
        "version=36",
        " worker_feed=",
        "workerFeed != 0",
        "printScheduler4()",
    ):
        assert marker in shell


def test_runtime_v36_netboot_gates_and_sched4_probe_exist() -> None:
    net_iterate = read_repo("scripts/netboot/net-iterate.sh")
    doctor = read_repo("scripts/netboot/netboot-doctor.sh")

    for source in (net_iterate, doctor):
        assert "runtime v36: timer-fed secondary scheduler workers" in source
        assert COMMANDS_V36 in source

    for marker in (
        "probe shell: sched4",
        "^sched4 ok=1 version=36 .*worker_feed=1 .*secondary_workers=1 .*feeds=[1-9][0-9]* .*drains=[1-9][0-9]* .*drops=[0-9][0-9]* .*gap=[0-9][0-9]* .*feed_imbalance=[0-9][0-9]* .*drain_imbalance=[0-9][0-9]* .*core0_feed=0 .*core1_feed=[1-9][0-9]* .*core2_feed=[1-9][0-9]* .*core3_feed=[1-9][0-9]* .*core0_drain=0 .*core1_drain=[1-9][0-9]* .*core2_drain=[1-9][0-9]* .*core3_drain=[1-9][0-9]* .*selftest=1",
        "probe shell: req-sched4",
        "^resp id=37 ok=1 cmd=sched4 end",
        "^bootcert ok=1 version=44 .*concurrency=1 .*priority=1 .*fairness=1 .*stealing=1 .*backpressure=1 .*handoff=1 .*wake=1 .*job_exec=1 .*worker_feed=1 .*secondary_workers=1 .*preemptive=1 .*smp_scheduler=1 .*atomics=1 .*locks=1 .*queues=1 .*smp=1 .*scheduler=1 .*certificate=1 .*agent=1 .*runtime=1 .*events_lost=0",
        "^certificate ok=1 version=44 substrate=1 .*bootcert=1 .*concurrency=1 .*priority=1 .*fairness=1 .*stealing=1 .*backpressure=1 .*handoff=1 .*wake=1 .*job_exec=1 .*worker_feed=1 .*secondary_workers=1 .*preemptive=1 .*smp_scheduler=1 .*atomics=1 .*locks=1 .*queues=1 .*smp=1 .*scheduler=1 .*agent=1 .*runtime=1 .*memory=1 .*objects=1 .*tasks=1 .*mailboxes=1 .*supervisor=1 .*handles=1 .*events=1 .*cancellations=1 .*channels=1 .*drivers=1 .*pressure=1 .*pools=1 .*mmu=1 .*events_lost=0",
        "stale pre-V43 SD fallback",
    ):
        assert marker in net_iterate


def test_runtime_v36_docs_are_updated_after_hardware_proof() -> None:
    readme = read_repo("README.md")
    runbook = read_repo("docs/RUNBOOK.md")
    design = read_repo("docs/CONCURRENCY_DESIGN.md")

    for source in (readme, runbook, design):
        assert "Runtime V36 timer-fed secondary scheduler workers" in source
        assert "bootcert ok=1 version=36" in source
        assert "worker_feed=1" in source
        assert "sched4 ok=1 version=36" in source

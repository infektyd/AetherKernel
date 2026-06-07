import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]


COMMANDS_V39 = (
    "commands=help,protocol,status,heap,queues,tasks,tasks2,kobjects,drivers,drivercheck,"
    "mailboxes,sendtest,supervisor,health,capcheck,events,runtime,agent,certificate,sched,sched2,sched3,sched4,sched5,sched6,sched7,sched8,sched9,sched10,sched11,cores,locks,runqueues,diag,irqs,timers,"
    "memcheck,faults,retained,retained-clear,memmap,mmu,pools,poolcheck,"
    "heapfrag,poolstats,frames,heapcheck,framecheck,stress,frameprobe,bootcert,"
    "canceltest,taskcheck,channeltest,bootcheck,soak,heap-invalid-free-test,"
    "heap-double-free-test,panic-test,fault-test,reboot"
)


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def swift_function(source: str, name: str) -> str:
    marker = f"func {name}("
    start = source.index(marker)
    next_func = source.find("\nfunc ", start + len(marker))
    if next_func == -1:
        return source[start:]
    return source[start:next_func]


def assert_handoff_certificate_warms_scheduler_layers(function_body: str) -> None:
    ordered_markers = (
        "let workerFeed = kernel_scheduler_timer_worker_feed_selftest()",
        "let secondaryWorkers = kernel_scheduler_secondary_worker_selftest()",
        "let jobExec = kernel_scheduler_secondary_job_selftest()",
        "let wake = kernel_scheduler_secondary_wake_selftest()",
        "let smpScheduler = kernel_scheduler_smp_selftest()",
        "let handoff = kernel_scheduler_secondary_handoff_selftest()",
    )
    positions = [function_body.index(marker) for marker in ordered_markers]
    assert positions == sorted(positions)


def test_runtime_v39_secondary_handoff_contract_exists() -> None:
    support = read_repo("Sources/Support/include/Support.h")
    scheduler = read_repo("Sources/Support/kernel_scheduler.c")

    for marker in (
        "Runtime V39 secondary scheduler handoff protocol",
        "#define KERNEL_SCHEDULER_VERSION 43U",
        "void kernel_scheduler_enable_secondary_handoffs(void);",
        "unsigned int kernel_scheduler_secondary_handoffs_enabled(void);",
        "unsigned long kernel_scheduler_secondary_handoff_issue_count(unsigned int core_id);",
        "unsigned long kernel_scheduler_secondary_handoff_completion_count(unsigned int core_id);",
        "unsigned long kernel_scheduler_secondary_handoff_issue_total(void);",
        "unsigned long kernel_scheduler_secondary_handoff_completion_total(void);",
        "unsigned long kernel_scheduler_secondary_handoff_gap(void);",
        "unsigned long kernel_scheduler_secondary_handoff_imbalance(void);",
        "int kernel_scheduler_secondary_handoff_selftest(void);",
    ):
        assert marker in support

    for marker in (
        "Runtime V39 secondary scheduler handoff protocol",
        "secondary_handoffs_enabled",
        "record_secondary_handoff_issue",
        "record_secondary_handoff_completion",
        "kernel_scheduler_secondary_handoff_selftest",
        "kernel_scheduler_secondary_handoff_gap() <= KERNEL_SCHEDULER_CORE_CAPACITY",
    ):
        assert marker in scheduler


def test_runtime_v39_application_boot_marker_and_selftest_exist() -> None:
    app = read_repo("Sources/Application/Application.swift")

    for marker in (
        "Runtime V39 adds secondary scheduler handoff acknowledgements.",
        "kernel_event_emit(KERNEL_EVENT_KIND_BOOT, 43, 0, 0)",
        "kernel_scheduler_enable_secondary_handoffs()",
        "runtime v39: secondary scheduler handoff protocol",
        "let runtimeV39 = kernel_scheduler_secondary_handoff_selftest()",
        "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 39, UInt(runtimeV39), UInt(kernel_scheduler_secondary_handoff_issue_total()))",
    ):
        assert marker in app


def test_runtime_v39_shell_sched7_and_certificate_surface_exist() -> None:
    shell = read_repo("Sources/Application/UARTShell.swift")

    for marker in (
        COMMANDS_V39,
        "func printScheduler7()",
        "sched7 ok=",
        " version=39",
        " handoff=",
        " wake=",
        " job_exec=",
        " issued=",
        " completed=",
        " gap=",
        " imbalance=",
        " core0_issue=",
        " core1_issue=",
        " core2_issue=",
        " core3_issue=",
        " core0_done=",
        " core1_done=",
        " core2_done=",
        " core3_done=",
        " selftest=",
        'shellBufferSliceEquals(commandStart, commandLen, "sched7")',
    ):
        assert marker in shell

    for marker in (
        "let handoff = kernel_scheduler_secondary_handoff_selftest()",
        "version=39",
        " handoff=",
        "handoff != 0",
        "printScheduler7()",
    ):
        assert marker in shell


def test_runtime_v39_boot_certificates_warm_scheduler_before_handoff() -> None:
    shell = read_repo("Sources/Application/UARTShell.swift")

    assert_handoff_certificate_warms_scheduler_layers(
        swift_function(shell, "printSubstrateCertificate")
    )
    assert_handoff_certificate_warms_scheduler_layers(
        swift_function(shell, "printBootcert")
    )


def test_runtime_v39_netboot_gates_and_sched7_probe_exist() -> None:
    net_iterate = read_repo("net-iterate.sh")
    doctor = read_repo("netboot-doctor.sh")

    for source in (net_iterate, doctor):
        assert "runtime v39: secondary scheduler handoff protocol" in source
        assert COMMANDS_V39 in source

    for marker in (
        "probe shell: sched7",
        "^sched7 ok=1 version=39 .*handoff=1 .*wake=1 .*job_exec=1 .*issued=[1-9][0-9]* .*completed=[1-9][0-9]* .*gap=[0-9][0-9]* .*imbalance=[0-9][0-9]* .*core0_issue=0 .*core1_issue=[1-9][0-9]* .*core2_issue=[1-9][0-9]* .*core3_issue=[1-9][0-9]* .*core0_done=0 .*core1_done=[1-9][0-9]* .*core2_done=[1-9][0-9]* .*core3_done=[1-9][0-9]* .*selftest=1",
        "probe shell: req-sched7",
        "^resp id=40 ok=1 cmd=sched7 end",
        "^bootcert ok=1 version=43 .*priority=1 .*fairness=1 .*stealing=1 .*backpressure=1 .*handoff=1 .*wake=1 .*job_exec=1 .*worker_feed=1 .*secondary_workers=1 .*preemptive=1 .*smp_scheduler=1 .*atomics=1 .*locks=1 .*queues=1 .*smp=1 .*scheduler=1 .*certificate=1 .*agent=1 .*runtime=1 .*events_lost=0",
        "^certificate ok=1 version=43 substrate=1 .*bootcert=1 .*priority=1 .*fairness=1 .*stealing=1 .*backpressure=1 .*handoff=1 .*wake=1 .*job_exec=1 .*worker_feed=1 .*secondary_workers=1 .*preemptive=1 .*smp_scheduler=1 .*atomics=1 .*locks=1 .*queues=1 .*smp=1 .*scheduler=1 .*agent=1 .*runtime=1 .*memory=1 .*objects=1 .*tasks=1 .*mailboxes=1 .*supervisor=1 .*handles=1 .*events=1 .*cancellations=1 .*channels=1 .*drivers=1 .*pressure=1 .*pools=1 .*mmu=1 .*events_lost=0",
        "stale pre-V43 SD fallback",
    ):
        assert marker in net_iterate


def test_runtime_v39_docs_are_updated_after_hardware_proof() -> None:
    readme = read_repo("README.md")
    runbook = read_repo("RUNBOOK.md")
    design = read_repo("CONCURRENCY_DESIGN.md")

    for source in (readme, runbook, design):
        assert "Runtime V39 secondary scheduler handoff protocol" in source
        assert "bootcert ok=1 version=39" in source
        assert "handoff=1" in source
        assert "sched7 ok=1 version=39" in source

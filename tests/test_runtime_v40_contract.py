import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]


COMMANDS_V40 = (
    "commands=help,protocol,status,heap,queues,tasks,tasks2,kobjects,drivers,drivercheck,"
    "mailboxes,sendtest,supervisor,health,capcheck,events,runtime,agent,certificate,sched,sched2,sched3,sched4,sched5,sched6,sched7,sched8,sched9,sched10,sched11,sched12,cores,locks,runqueues,diag,irqs,timers,"
    "memcheck,faults,retained,retained-clear,memmap,mmu,pools,poolcheck,"
    "heapfrag,poolstats,frames,heapcheck,framecheck,stress,frameprobe,bootcert,"
    "canceltest,taskcheck,channeltest,bootcheck,soak,heap-invalid-free-test,"
    "heap-double-free-test,panic-test,fault-test,reboot"
)


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_runtime_v40_backpressure_contract_exists() -> None:
    support = read_repo("Sources/Support/include/Support.h")
    scheduler = read_repo("Sources/Support/kernel_scheduler.c")

    for marker in (
        "Runtime V40 scheduler backpressure protocol",
        "#define KERNEL_SCHEDULER_VERSION 44U",
        "#define KERNEL_SCHEDULER_PRESSURE_TOKEN_BASE 0x4000U",
        "unsigned long kernel_scheduler_runqueue_high_water(unsigned int core_id);",
        "unsigned long kernel_scheduler_runqueue_overflow_count(unsigned int core_id);",
        "unsigned long kernel_scheduler_runqueue_overflow_total(void);",
        "unsigned long kernel_scheduler_runqueue_high_water_max(void);",
        "int kernel_scheduler_backpressure_selftest(void);",
    ):
        assert marker in support

    for marker in (
        "Runtime V40 scheduler backpressure protocol",
        "runqueue_high_water",
        "runqueue_overflows",
        "KERNEL_SCHEDULER_PRESSURE_TOKEN_BASE",
        "kernel_scheduler_runqueue_high_water",
        "kernel_scheduler_runqueue_overflow_count",
        "kernel_scheduler_runqueue_overflow_total",
        "kernel_scheduler_runqueue_high_water_max",
        "kernel_scheduler_backpressure_selftest",
        "kernel_scheduler_runqueue_high_water_max() >= KERNEL_SCHEDULER_RUNQUEUE_CAPACITY",
        "kernel_scheduler_runqueue_overflow_total() >= KERNEL_SCHEDULER_CORE_CAPACITY",
    ):
        assert marker in scheduler


def test_runtime_v40_application_boot_marker_and_selftest_exist() -> None:
    app = read_repo("Sources/Application/Application.swift")

    for marker in (
        "Runtime V40 adds bounded scheduler backpressure proof.",
        "kernel_event_emit(KERNEL_EVENT_KIND_BOOT, 44, 0, 0)",
        "runtime v40: scheduler backpressure protocol",
        "let runtimeV40 = kernel_scheduler_backpressure_selftest()",
        "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 40, UInt(runtimeV40), UInt(kernel_scheduler_runqueue_overflow_total()))",
    ):
        assert marker in app


def test_runtime_v40_shell_sched8_and_certificate_surface_exist() -> None:
    shell = read_repo("Sources/Application/UARTShell.swift")

    for marker in (
        COMMANDS_V40,
        "func printScheduler8()",
        "sched8 ok=",
        " version=40",
        " backpressure=",
        " handoff=",
        " wake=",
        " high_water=",
        " overflows=",
        " total=",
        " capacity=",
        " core0_high=",
        " core1_high=",
        " core2_high=",
        " core3_high=",
        " core0_overflow=",
        " core1_overflow=",
        " core2_overflow=",
        " core3_overflow=",
        " selftest=",
        'shellBufferSliceEquals(commandStart, commandLen, "sched8")',
    ):
        assert marker in shell

    for marker in (
        "let backpressure = kernel_scheduler_backpressure_selftest()",
        "version=40",
        " backpressure=",
        "backpressure != 0",
        "printScheduler8()",
    ):
        assert marker in shell


def test_runtime_v40_netboot_gates_and_sched8_probe_exist() -> None:
    net_iterate = read_repo("net-iterate.sh")
    doctor = read_repo("netboot-doctor.sh")

    for source in (net_iterate, doctor):
        assert "runtime v40: scheduler backpressure protocol" in source
        assert COMMANDS_V40 in source

    for marker in (
        "probe shell: sched8",
        "^sched8 ok=1 version=40 .*backpressure=1 .*handoff=1 .*wake=1 .*high_water=[8-9][0-9]* .*overflows=[1-9][0-9]* .*total=0 .*capacity=8 .*core0_high=8 .*core1_high=8 .*core2_high=8 .*core3_high=8 .*core0_overflow=[1-9][0-9]* .*core1_overflow=[1-9][0-9]* .*core2_overflow=[1-9][0-9]* .*core3_overflow=[1-9][0-9]* .*selftest=1",
        "probe shell: req-sched8",
        "^resp id=41 ok=1 cmd=sched8 end",
        "^bootcert ok=1 version=44 .*concurrency=1 .*priority=1 .*fairness=1 .*stealing=1 .*backpressure=1 .*handoff=1 .*wake=1 .*job_exec=1 .*worker_feed=1 .*secondary_workers=1 .*preemptive=1 .*smp_scheduler=1 .*atomics=1 .*locks=1 .*queues=1 .*smp=1 .*scheduler=1 .*certificate=1 .*agent=1 .*runtime=1 .*events_lost=0",
        "^certificate ok=1 version=44 substrate=1 .*bootcert=1 .*concurrency=1 .*priority=1 .*fairness=1 .*stealing=1 .*backpressure=1 .*handoff=1 .*wake=1 .*job_exec=1 .*worker_feed=1 .*secondary_workers=1 .*preemptive=1 .*smp_scheduler=1 .*atomics=1 .*locks=1 .*queues=1 .*smp=1 .*scheduler=1 .*agent=1 .*runtime=1 .*memory=1 .*objects=1 .*tasks=1 .*mailboxes=1 .*supervisor=1 .*handles=1 .*events=1 .*cancellations=1 .*channels=1 .*drivers=1 .*pressure=1 .*pools=1 .*mmu=1 .*events_lost=0",
        "stale pre-V43 SD fallback",
    ):
        assert marker in net_iterate


def test_runtime_v40_docs_are_updated_after_hardware_proof() -> None:
    readme = read_repo("README.md")
    runbook = read_repo("RUNBOOK.md")
    design = read_repo("CONCURRENCY_DESIGN.md")

    for source in (readme, runbook, design):
        assert "Runtime V40 scheduler backpressure protocol" in source
        assert "bootcert ok=1 version=40" in source
        assert "backpressure=1" in source
        assert "sched8 ok=1 version=40" in source

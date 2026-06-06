import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]


COMMANDS_V31 = (
    "commands=help,protocol,status,heap,queues,tasks,tasks2,kobjects,drivers,drivercheck,"
    "mailboxes,sendtest,supervisor,health,capcheck,events,runtime,agent,certificate,sched,sched2,sched3,sched4,sched5,cores,locks,runqueues,diag,irqs,timers,"
    "memcheck,faults,retained,retained-clear,memmap,mmu,pools,poolcheck,"
    "heapfrag,poolstats,frames,heapcheck,framecheck,stress,frameprobe,bootcert,"
    "canceltest,taskcheck,channeltest,bootcheck,soak,heap-invalid-free-test,"
    "heap-double-free-test,panic-test,fault-test,reboot"
)


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_runtime_v31_scheduler_c_substrate_contract_exists() -> None:
    support = read_repo("Sources/Support/include/Support.h")
    source_path = ROOT / "Sources/Support/kernel_scheduler.c"

    assert source_path.exists(), "Sources/Support/kernel_scheduler.c missing"

    for marker in (
        "Runtime V31 preemptive scheduler substrate",
        "#define KERNEL_TIMER_CLIENT_SCHEDULER 2U",
        "#define KERNEL_TIMER_CLIENT_COUNT    3U",
        "#define KERNEL_SCHEDULER_VERSION 37U",
        "#define KERNEL_SCHEDULER_CORE_CAPACITY 4U",
        "#define KERNEL_SCHEDULER_RUNQUEUE_CAPACITY",
        "void kernel_scheduler_init(void);",
        "void kernel_scheduler_start(unsigned long interval_ticks);",
        "void kernel_scheduler_on_timer_irq(void);",
        "unsigned int kernel_scheduler_active(void);",
        "unsigned int kernel_scheduler_core_count(void);",
        "unsigned int kernel_scheduler_runqueue_capacity(void);",
        "unsigned int kernel_scheduler_runqueue_count(unsigned int core_id);",
        "unsigned long kernel_scheduler_tick_count(unsigned int core_id);",
        "unsigned long kernel_scheduler_irq_tick_count(unsigned int core_id);",
        "unsigned long kernel_scheduler_preempt_count(unsigned int core_id);",
        "unsigned long kernel_scheduler_enqueue_count(unsigned int core_id);",
        "unsigned long kernel_scheduler_dequeue_count(unsigned int core_id);",
        "unsigned long kernel_scheduler_interval_ticks(void);",
        "int kernel_scheduler_selftest(void);",
    ):
        assert marker in support


def test_runtime_v31_application_and_irq_wiring_exist() -> None:
    app = read_repo("Sources/Application/Application.swift")
    irq = read_repo("Sources/Application/IRQHandler.swift")

    for marker in (
        "kernel_event_emit(KERNEL_EVENT_KIND_BOOT, 37, 0, 0)",
        "kernel_scheduler_init()",
        "kernel_scheduler_start(timerFrequency() / 20)",
        "runtime v31: preemptive scheduler substrate",
        "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 31,",
    ):
        assert marker in app

    assert "kernel_scheduler_on_timer_irq()" in irq


def test_runtime_v31_shell_sched_and_bootcert_surface_exist() -> None:
    shell = read_repo("Sources/Application/UARTShell.swift")

    for marker in (
        COMMANDS_V31,
        "func printScheduler()",
        "sched ok=",
        " version=31",
        " active=",
        " cores=",
        " core=0",
        " interval_ticks=",
        " ticks=",
        " irq_ticks=",
        " preemptions=",
        " runqueue=",
        " enqueues=",
        " dequeues=",
        " selftest=",
        'shellBufferSliceEquals(commandStart, commandLen, "sched")',
    ):
        assert marker in shell

    for marker in (
        "let scheduler = kernel_scheduler_selftest()",
        "version=31",
        " scheduler=",
        "scheduler != 0",
        "printScheduler()",
    ):
        assert marker in shell


def test_runtime_v31_netboot_gates_and_scheduler_probe_exist() -> None:
    net_iterate = read_repo("net-iterate.sh")
    doctor = read_repo("netboot-doctor.sh")

    for source in (net_iterate, doctor):
        assert "runtime v31: preemptive scheduler substrate" in source
        assert COMMANDS_V31 in source

    for marker in (
        "probe shell: sched",
        "^sched ok=1 version=31 .*active=1 .*cores=1 .*core=0 .*ticks=[1-9][0-9]* .*irq_ticks=[1-9][0-9]* .*preemptions=[1-9][0-9]* .*runqueue=0/[1-9][0-9]* .*selftest=1",
        "probe shell: req-sched",
        "^resp id=31 ok=1 cmd=sched end",
        "^bootcert ok=1 version=37 .*job_exec=1 .*worker_feed=1 .*secondary_workers=1 .*preemptive=1 .*smp_scheduler=1 .*atomics=1 .*locks=1 .*queues=1 .*smp=1 .*scheduler=1 .*certificate=1 .*agent=1 .*runtime=1 .*events_lost=0",
        "stale pre-V37 SD fallback",
    ):
        assert marker in net_iterate


def test_runtime_v31_docs_are_updated_after_hardware_proof() -> None:
    readme = read_repo("README.md")
    runbook = read_repo("RUNBOOK.md")
    design = read_repo("CONCURRENCY_DESIGN.md")

    for source in (readme, runbook, design):
        assert "Runtime V31 preemptive scheduler substrate" in source
        assert "bootcert ok=1 version=31" in source
        assert "scheduler=1" in source
        assert "sched ok=1 version=31" in source

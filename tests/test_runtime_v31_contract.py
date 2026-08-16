import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]

from tests.commands_contract_helpers import (
    assert_commands_era_in_netboot_sources,
    assert_commands_era_prefix_of_live,
)
from tests.test_runtime_v45_contract import COMMANDS_V45


COMMANDS_V31 = (
    "commands=help,protocol,status,heap,queues,tasks,tasks2,kobjects,drivers,drivercheck,"
    "mailboxes,sendtest,supervisor,health,capcheck,events,runtime,agent,certificate,sched,sched2,sched3,sched4,sched5,sched6,sched7,sched8,sched9,sched10,sched11,sched12,cores,locks,runqueues,diag,irqs,timers,"
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
        "#define KERNEL_SCHEDULER_VERSION 46U",
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
        "kernel_event_emit(KERNEL_EVENT_KIND_BOOT, 64, 0, 0)",
        "kernel_scheduler_init()",
        "kernel_scheduler_start(timerFrequency() / 20)",
        "runtime v31: preemptive scheduler substrate",
        "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 31,",
    ):
        assert marker in app

    assert "kernel_scheduler_on_timer_irq()" in irq


def test_runtime_v31_shell_sched_and_bootcert_surface_exist() -> None:
    shell = read_repo("Sources/Application/UARTShell.swift")
    assert_commands_era_prefix_of_live(COMMANDS_V31, label="V31")


    for marker in (
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
        "let scheduler = kernel_scheduler_scheduler_proven()",
        "version=31",
        " scheduler=",
        "scheduler != 0",
        "printScheduler()",
    ):
        assert marker in shell


def test_runtime_v31_netboot_gates_and_scheduler_probe_exist() -> None:
    net_iterate = read_repo("scripts/netboot/net-iterate.sh")
    doctor = read_repo("scripts/netboot/netboot-doctor.sh")

    for source in (net_iterate, doctor):
        assert_commands_era_in_netboot_sources(COMMANDS_V31, COMMANDS_V45, source, label="V31")

    assert "schedselftest ok=1 version=31" in net_iterate
    assert "schedselftest ok=1 version=31" in doctor

    for marker in (
        "probe shell: sched",
        "^sched ok=1 version=31 .*active=1 .*cores=1 .*core=0 .*ticks=[1-9][0-9]* .*irq_ticks=[1-9][0-9]* .*preemptions=[1-9][0-9]* .*runqueue=0/[1-9][0-9]* .*selftest=1",
        "probe shell: req-sched",
        "^resp id=31 ok=1 cmd=sched end",
        "^bootcert ok=1 version=66 .*concurrency=1 .*priority=1 .*fairness=1 .*stealing=[01] .*backpressure=[01] .*handoff=[01] .*wake=[01] .*job_exec=[01] .*worker_feed=[01] .*secondary_workers=[01] .*preemptive=[01] .*smp_scheduler=[01] .*atomics=1 .*locks=1 .*queues=1 .*smp=1 .*scheduler=1 .*certificate=1 .*agent=1 .*runtime=1 .*syscall=1 .*uaccess=1 .*usermode=1 .*process=1 .*loader=1 .*multiprocess=1 .*sdhci=1 .*card=1 .*block=1 .*fat32=1 .*mailbox=1 .*framebuf=1 .*console=1 .*pcie=1 .*vl805=1 .*xhci=1 .*kbd=[01] .*swift=6.3.2 .*events_lost=0",
        "stale pre-V43 SD fallback",
    ):
        assert marker in net_iterate


def test_runtime_v31_docs_are_updated_after_hardware_proof() -> None:
    readme = read_repo("README.md")
    runbook = read_repo("docs/RUNBOOK.md")
    design = read_repo("docs/CONCURRENCY_DESIGN.md")

    for source in (readme, runbook, design):
        assert "Runtime V31 preemptive scheduler substrate" in source
        assert "bootcert ok=1 version=31" in source
        assert "scheduler=1" in source
        assert "sched ok=1 version=31" in source

    # S40: V31 narrative must not claim a live executor timer client.
    assert "sleep/executor timer clients" not in readme
    assert "then the armed SLEEP" in readme
    assert "client (TimerSleep)" in readme
    assert "EXECUTOR CNTP slot" in readme
    assert "never armed" in readme
    assert "sleep and executor timer" not in design
    assert "armed SLEEP client (TimerSleep)" in design
    assert "EXECUTOR CNTP slot never armed" in design

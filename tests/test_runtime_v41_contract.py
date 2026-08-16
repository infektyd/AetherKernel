import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]

from tests.commands_contract_helpers import (
    assert_commands_era_in_netboot_sources,
    assert_commands_era_prefix_of_live,
)
from tests.test_runtime_v45_contract import COMMANDS_V45


COMMANDS_V41 = (
    "commands=help,protocol,status,heap,queues,tasks,tasks2,kobjects,drivers,drivercheck,"
    "mailboxes,sendtest,supervisor,health,capcheck,events,runtime,agent,certificate,sched,sched2,sched3,sched4,sched5,sched6,sched7,sched8,sched9,sched10,sched11,sched12,cores,locks,runqueues,diag,irqs,timers,"
    "memcheck,faults,retained,retained-clear,memmap,mmu,pools,poolcheck,"
    "heapfrag,poolstats,frames,heapcheck,framecheck,stress,frameprobe,bootcert,"
    "canceltest,taskcheck,channeltest,bootcheck,soak,heap-invalid-free-test,"
    "heap-double-free-test,panic-test,fault-test,reboot"
)


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_runtime_v41_work_steal_contract_exists() -> None:
    support = read_repo("Sources/Support/include/Support.h")
    scheduler = read_repo("Sources/Support/kernel_scheduler.c")

    for marker in (
        "Runtime V41 secondary scheduler work-stealing protocol",
        "#define KERNEL_SCHEDULER_VERSION 46U",
        "#define KERNEL_SCHEDULER_STEAL_TOKEN_BASE 0x5000U",
        "unsigned long kernel_scheduler_steal_attempt_count(unsigned int core_id);",
        "unsigned long kernel_scheduler_steal_success_count(unsigned int core_id);",
        "unsigned long kernel_scheduler_steal_source_count(unsigned int core_id);",
        "unsigned long kernel_scheduler_steal_total(void);",
        "unsigned long kernel_scheduler_steal_completion_total(void);",
        "unsigned int kernel_scheduler_secondary_has_runnable_work(unsigned int core_id);",
        "int kernel_scheduler_work_steal_selftest(void);",
    ):
        assert marker in support

    for marker in (
        "Runtime V41 secondary scheduler work-stealing protocol",
        "steal_attempts",
        "steal_successes",
        "steal_source_count",
        "steal_completions",
        "KERNEL_SCHEDULER_STEAL_TOKEN_BASE",
        "kernel_scheduler_try_steal_work",
        "kernel_scheduler_steal_attempt_count",
        "kernel_scheduler_steal_success_count",
        "kernel_scheduler_steal_source_count",
        "kernel_scheduler_steal_total",
        "kernel_scheduler_steal_completion_total",
        "kernel_scheduler_secondary_has_runnable_work",
        "kernel_scheduler_work_steal_selftest",
        "kernel_scheduler_steal_total() >= 2U",
        "kernel_scheduler_steal_completion_total() >= 2U",
    ):
        assert marker in scheduler

    smp = read_repo("Sources/Support/kernel_smp.c")
    assert "kernel_scheduler_secondary_has_runnable_work(core_id)" in smp


def test_runtime_v41_application_boot_marker_and_selftest_exist() -> None:
    app = read_repo("Sources/Application/Application.swift")

    for marker in (
        "Runtime V41 adds bounded secondary scheduler work stealing.",
        "kernel_event_emit(KERNEL_EVENT_KIND_BOOT, 64, 0, 0)",
        "runtime v41: secondary scheduler work stealing",
        "let runtimeV41 = kernel_scheduler_work_steal_selftest()",
        "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 41, UInt(runtimeV41), UInt(kernel_scheduler_steal_total()))",
    ):
        assert marker in app


def test_runtime_v41_shell_sched9_and_certificate_surface_exist() -> None:
    shell = read_repo("Sources/Application/UARTShell.swift")
    assert_commands_era_prefix_of_live(COMMANDS_V41, label="V41")


    for marker in (
        "func printScheduler9()",
        "sched9 ok=",
        " version=41",
        " stealing=",
        " backpressure=",
        " handoff=",
        " wake=",
        " steals=",
        " completions=",
        " total=",
        " capacity=",
        " source_core1=",
        " source_core2=",
        " source_core3=",
        " dest_core1=",
        " dest_core2=",
        " dest_core3=",
        " attempts_core1=",
        " attempts_core2=",
        " attempts_core3=",
        " selftest=",
        'shellBufferSliceEquals(commandStart, commandLen, "sched9")',
    ):
        assert marker in shell

    for marker in (
        "let stealing = kernel_scheduler_work_steal_selftest()",
        "version=41",
        " stealing=",
        "stealing != 0",
        "printScheduler9()",
    ):
        assert marker in shell


def test_runtime_v41_netboot_gates_and_sched9_probe_exist() -> None:
    net_iterate = read_repo("scripts/netboot/net-iterate.sh")
    doctor = read_repo("scripts/netboot/netboot-doctor.sh")

    for source in (net_iterate, doctor):
        assert_commands_era_in_netboot_sources(COMMANDS_V41, COMMANDS_V45, source, label="V41")

    assert "schedselftest ok=1 version=41" in net_iterate
    assert "runtime v41: secondary scheduler work stealing" in doctor

    for marker in (
        "probe shell: sched9",
        "^sched9 ok=1 version=41 .*stealing=1 .*backpressure=1 .*handoff=1 .*wake=1 .*steals=[1-9][0-9]* .*completions=[1-9][0-9]* .*total=0 .*capacity=8 .*source_core1=[1-9][0-9]* .*source_core2=[0-9][0-9]* .*source_core3=[0-9][0-9]* .*dest_core1=0 .*dest_core2=[0-9][0-9]* .*dest_core3=[0-9][0-9]* .*attempts_core1=[0-9][0-9]* .*attempts_core2=[1-9][0-9]* .*attempts_core3=[1-9][0-9]* .*selftest=1",
        "probe shell: req-sched9",
        "^resp id=42 ok=1 cmd=sched9 end",
        "^bootcert ok=1 version=66 .*concurrency=1 .*priority=1 .*fairness=1 .*stealing=[01] .*backpressure=[01] .*handoff=[01] .*wake=[01] .*job_exec=[01] .*worker_feed=[01] .*secondary_workers=[01] .*preemptive=[01] .*smp_scheduler=[01] .*atomics=1 .*locks=1 .*queues=1 .*smp=1 .*scheduler=1 .*certificate=1 .*agent=1 .*runtime=1 .*syscall=1 .*uaccess=1 .*usermode=1 .*process=1 .*loader=1 .*multiprocess=1 .*sdhci=1 .*card=1 .*block=1 .*fat32=1 .*mailbox=1 .*framebuf=1 .*console=1 .*pcie=1 .*vl805=1 .*xhci=1 .*kbd=[01] .*swift=6.3.2 .*events_lost=0",
        "^certificate ok=1 version=63 substrate=1 .*bootcert=[01] .*concurrency=1 .*priority=1 .*fairness=1 .*stealing=[01] .*backpressure=[01] .*handoff=[01] .*wake=[01] .*job_exec=[01] .*worker_feed=[01] .*secondary_workers=[01] .*preemptive=[01] .*smp_scheduler=[01] .*atomics=1 .*locks=1 .*queues=1 .*smp=1 .*scheduler=1 .*agent=1 .*runtime=1 .*memory=1 .*objects=1 .*tasks=1 .*mailboxes=1 .*supervisor=1 .*handles=1 .*events=[0-9]+ .*cancellations=1 .*channels=1 .*drivers=1 .*pressure=1 .*pools=1 .*mmu=1 .*vmm=1 .*asplit=1 .*el0=1 .*syscall=1 .*uaccess=1 .*usermode=1 .*process=1 .*loader=1 .*multiprocess=1 .*sdhci=1 .*card=1 .*block=1 .*fat32=1 .*mailbox=1 .*framebuf=1 .*console=1 .*pcie=1 .*vl805=1 .*xhci=1 .*swift=6.3.2 .*events_lost=0",
        "stale pre-V43 SD fallback",
    ):
        assert marker in net_iterate


def test_runtime_v41_docs_are_updated_after_hardware_proof() -> None:
    readme = read_repo("README.md")
    runbook = read_repo("docs/RUNBOOK.md")
    design = read_repo("docs/CONCURRENCY_DESIGN.md")

    for source in (readme, runbook, design):
        assert "Runtime V41 secondary scheduler work-stealing protocol" in source
        assert "bootcert ok=1 version=43" in source
        assert "stealing=1" in source
        assert "sched9 ok=1 version=41" in source

import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]

from tests.commands_contract_helpers import (
    assert_commands_era_in_netboot_sources,
    assert_commands_era_prefix_of_live,
)
from tests.test_runtime_v45_contract import COMMANDS_V45


COMMANDS_V34 = (
    "commands=help,protocol,status,heap,queues,tasks,tasks2,kobjects,drivers,drivercheck,"
    "mailboxes,sendtest,supervisor,health,capcheck,events,runtime,agent,certificate,sched,sched2,sched3,sched4,sched5,sched6,sched7,sched8,sched9,sched10,sched11,sched12,cores,locks,runqueues,diag,irqs,timers,"
    "memcheck,faults,retained,retained-clear,memmap,mmu,pools,poolcheck,"
    "heapfrag,poolstats,frames,heapcheck,framecheck,stress,frameprobe,bootcert,"
    "canceltest,taskcheck,channeltest,bootcheck,soak,heap-invalid-free-test,"
    "heap-double-free-test,panic-test,fault-test,reboot"
)


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_runtime_v34_scheduler_dispatch_contract_exists() -> None:
    support = read_repo("Sources/Support/include/Support.h")
    scheduler = read_repo("Sources/Support/kernel_scheduler.c")

    for marker in (
        "Runtime V34 timer-driven SMP scheduler dispatch",
        "#define KERNEL_SCHEDULER_VERSION 46U",
        "#define KERNEL_SCHEDULER_DISPATCH_TOKEN_BASE 0x3400U",
        "void kernel_scheduler_enable_smp_dispatch(void);",
        "unsigned int kernel_scheduler_smp_dispatch_enabled(void);",
        "unsigned long kernel_scheduler_dispatch_count(unsigned int core_id);",
        "unsigned long kernel_scheduler_route_count(unsigned int core_id);",
        "unsigned long kernel_scheduler_total_dispatch_count(void);",
        "unsigned long kernel_scheduler_total_route_count(void);",
        "unsigned long kernel_scheduler_fairness_min(void);",
        "unsigned long kernel_scheduler_fairness_max(void);",
        "unsigned long kernel_scheduler_fairness_imbalance(void);",
        "unsigned int kernel_scheduler_last_dispatch_core(void);",
        "int kernel_scheduler_smp_selftest(void);",
    ):
        assert marker in support

    for marker in (
        "Runtime V34 timer-driven SMP scheduler dispatch",
        "kernel_scheduler_enable_smp_dispatch",
        "kernel_smp_online_count",
        "kernel_smp_core_online(core_id)",
        "kernel_scheduler_enqueue(core_id, token)",
        "kernel_scheduler_dequeue(core_id, &dispatched)",
        "routes",
        "dispatches",
        "kernel_scheduler_smp_selftest",
    ):
        assert marker in scheduler


def test_runtime_v34_application_boot_marker_and_selftest_exist() -> None:
    app = read_repo("Sources/Application/Application.swift")

    for marker in (
        "Runtime V34 adds timer-driven SMP scheduler dispatch accounting.",
        "kernel_event_emit(KERNEL_EVENT_KIND_BOOT, 64, 0, 0)",
        "kernel_scheduler_enable_smp_dispatch()",
        "runtime v34: timer-driven smp scheduler dispatch",
        "let runtimeV34 = kernel_scheduler_smp_selftest()",
        "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 34, UInt(runtimeV34), UInt(kernel_scheduler_total_dispatch_count()))",
    ):
        assert marker in app


def test_runtime_v34_shell_sched2_and_certificate_surface_exist() -> None:
    shell = read_repo("Sources/Application/UARTShell.swift")
    assert_commands_era_prefix_of_live(COMMANDS_V34, label="V34")


    for marker in (
        "func printScheduler2()",
        "sched2 ok=",
        " version=34",
        " preemptive=",
        " smp_scheduler=",
        " active=",
        " cores=",
        " online=",
        " dispatches=",
        " routes=",
        " min=",
        " max=",
        " imbalance=",
        " core0=",
        " core1=",
        " core2=",
        " core3=",
        " selftest=",
        'shellBufferSliceEquals(commandStart, commandLen, "sched2")',
    ):
        assert marker in shell

    for marker in (
        "let preemptive = kernel_scheduler_active()",
        "let smpScheduler = kernel_scheduler_smp_selftest()",
        "version=34",
        " preemptive=",
        " smp_scheduler=",
        "preemptive != 0 && smpScheduler != 0",
        "printScheduler2()",
    ):
        assert marker in shell


def test_runtime_v34_netboot_gates_and_sched2_probe_exist() -> None:
    net_iterate = read_repo("scripts/netboot/net-iterate.sh")
    doctor = read_repo("scripts/netboot/netboot-doctor.sh")

    for source in (net_iterate, doctor):
        assert_commands_era_in_netboot_sources(COMMANDS_V34, COMMANDS_V45, source, label="V34")

    assert "schedselftest ok=1 version=34" in net_iterate
    assert "schedselftest ok=1 version=34" in doctor

    for marker in (
        "probe shell: sched2",
        "^sched2 ok=1 version=34 .*preemptive=1 .*smp_scheduler=1 .*active=1 .*cores=4 .*online=4 .*dispatches=[1-9][0-9]* .*routes=[1-9][0-9]* .*imbalance=[0-9][0-9]* .*core0=[1-9][0-9]* .*core1=[1-9][0-9]* .*core2=[1-9][0-9]* .*core3=[1-9][0-9]* .*selftest=1",
        "probe shell: req-sched2",
        "^resp id=35 ok=1 cmd=sched2 end",
        "^bootcert ok=1 version=66 .*concurrency=1 .*priority=1 .*fairness=1 .*stealing=[01] .*backpressure=[01] .*handoff=[01] .*wake=[01] .*job_exec=[01] .*worker_feed=[01] .*secondary_workers=[01] .*preemptive=[01] .*smp_scheduler=[01] .*atomics=1 .*locks=1 .*queues=1 .*smp=1 .*scheduler=1 .*certificate=1 .*agent=1 .*runtime=1 .*syscall=1 .*uaccess=1 .*usermode=1 .*process=1 .*loader=1 .*multiprocess=1 .*sdhci=1 .*card=1 .*block=1 .*fat32=1 .*mailbox=1 .*framebuf=1 .*console=1 .*pcie=1 .*vl805=1 .*xhci=1 .*kbd=[01] .*swift=6.3.2 .*events_lost=0",
        "^certificate ok=1 version=63 substrate=1 .*bootcert=[01] .*concurrency=1 .*priority=1 .*fairness=1 .*stealing=[01] .*backpressure=[01] .*handoff=[01] .*wake=[01] .*job_exec=[01] .*worker_feed=[01] .*secondary_workers=[01] .*preemptive=[01] .*smp_scheduler=[01] .*atomics=1 .*locks=1 .*queues=1 .*smp=1 .*scheduler=1 .*agent=1 .*runtime=1 .*memory=1 .*objects=1 .*tasks=1 .*mailboxes=1 .*supervisor=1 .*handles=1 .*events=[0-9]+ .*cancellations=1 .*channels=1 .*drivers=1 .*pressure=1 .*pools=1 .*mmu=1 .*vmm=1 .*asplit=1 .*el0=1 .*syscall=1 .*uaccess=1 .*usermode=1 .*process=1 .*loader=1 .*multiprocess=1 .*sdhci=1 .*card=1 .*block=1 .*fat32=1 .*mailbox=1 .*framebuf=1 .*console=1 .*pcie=1 .*vl805=1 .*xhci=1 .*swift=6.3.2 .*events_lost=0",
        "stale pre-V43 SD fallback",
    ):
        assert marker in net_iterate


def test_runtime_v34_docs_are_updated_after_hardware_proof() -> None:
    readme = read_repo("README.md")
    runbook = read_repo("docs/RUNBOOK.md")
    design = read_repo("docs/CONCURRENCY_DESIGN.md")

    for source in (readme, runbook, design):
        assert "Runtime V34 timer-driven SMP scheduler dispatch" in source
        assert "bootcert ok=1 version=34" in source
        assert "preemptive=1" in source
        assert "smp_scheduler=1" in source
        assert "sched2 ok=1 version=34" in source

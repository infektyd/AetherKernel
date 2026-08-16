import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]

from tests.commands_contract_helpers import (
    assert_commands_era_in_netboot_sources,
    assert_commands_era_prefix_of_live,
)
from tests.test_runtime_v45_contract import COMMANDS_V45


COMMANDS_V42 = (
    "commands=help,protocol,status,heap,queues,tasks,tasks2,kobjects,drivers,drivercheck,"
    "mailboxes,sendtest,supervisor,health,capcheck,events,runtime,agent,certificate,sched,sched2,sched3,sched4,sched5,sched6,sched7,sched8,sched9,sched10,sched11,sched12,cores,locks,runqueues,diag,irqs,timers,"
    "memcheck,faults,retained,retained-clear,memmap,mmu,pools,poolcheck,"
    "heapfrag,poolstats,frames,heapcheck,framecheck,stress,frameprobe,bootcert,"
    "canceltest,taskcheck,channeltest,bootcheck,soak,heap-invalid-free-test,"
    "heap-double-free-test,panic-test,fault-test,reboot"
)


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_runtime_v42_load_balancing_contract_exists() -> None:
    support = read_repo("Sources/Support/include/Support.h")
    scheduler = read_repo("Sources/Support/kernel_scheduler.c")

    for marker in (
        "Runtime V42 secondary scheduler load-balancing protocol",
        "#define KERNEL_SCHEDULER_VERSION 46U",
        "#define KERNEL_SCHEDULER_BALANCE_TOKEN_BASE 0x4200U",
        "void kernel_scheduler_enable_load_balancing(void);",
        "unsigned int kernel_scheduler_load_balancing_enabled(void);",
        "unsigned long kernel_scheduler_balance_attempt_count(unsigned int core_id);",
        "unsigned long kernel_scheduler_balance_success_count(unsigned int core_id);",
        "unsigned long kernel_scheduler_balance_source_count(unsigned int core_id);",
        "unsigned long kernel_scheduler_balance_completion_count(unsigned int core_id);",
        "unsigned long kernel_scheduler_balance_total(void);",
        "unsigned long kernel_scheduler_balance_completion_total(void);",
        "unsigned long kernel_scheduler_secondary_queue_min(void);",
        "unsigned long kernel_scheduler_secondary_queue_max(void);",
        "unsigned long kernel_scheduler_secondary_queue_imbalance(void);",
        "int kernel_scheduler_fairness_selftest(void);",
    ):
        assert marker in support

    for marker in (
        "Runtime V42 secondary scheduler load-balancing protocol",
        "balance_attempts",
        "balance_successes",
        "balance_source_count",
        "balance_completions",
        "KERNEL_SCHEDULER_BALANCE_TOKEN_BASE",
        "kernel_scheduler_try_balance_work",
        "kernel_scheduler_balance_attempt_count",
        "kernel_scheduler_balance_success_count",
        "kernel_scheduler_balance_source_count",
        "kernel_scheduler_balance_total",
        "kernel_scheduler_balance_completion_total",
        "kernel_scheduler_secondary_queue_min",
        "kernel_scheduler_secondary_queue_max",
        "kernel_scheduler_secondary_queue_imbalance",
        "kernel_scheduler_fairness_selftest",
        "kernel_scheduler_balance_total() >= 2U",
        "kernel_scheduler_balance_completion_total() >= 2U",
    ):
        assert marker in scheduler

    smp = read_repo("Sources/Support/kernel_smp.c")
    assert "kernel_scheduler_try_balance_work(core_id)" in smp


def test_runtime_v42_application_boot_marker_and_selftest_exist() -> None:
    app = read_repo("Sources/Application/Application.swift")

    for marker in (
        "Runtime V42 adds bounded secondary scheduler load balancing.",
        "kernel_event_emit(KERNEL_EVENT_KIND_BOOT, 64, 0, 0)",
        "runtime v42: secondary scheduler load balancing",
        "kernel_scheduler_enable_load_balancing()",
        "let runtimeV42 = kernel_scheduler_fairness_selftest()",
        "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 42, UInt(runtimeV42), UInt(kernel_scheduler_balance_total()))",
    ):
        assert marker in app


def test_runtime_v42_shell_sched10_and_certificate_surface_exist() -> None:
    shell = read_repo("Sources/Application/UARTShell.swift")
    assert_commands_era_prefix_of_live(COMMANDS_V42, label="V42")


    for marker in (
        "func printScheduler10()",
        "sched10 ok=",
        " version=42",
        " fairness=",
        " stealing=",
        " backpressure=",
        " handoff=",
        " wake=",
        " balances=",
        " completions=",
        " min=",
        " max=",
        " imbalance=",
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
        " queue_min=",
        " queue_max=",
        " queue_imbalance=",
        " selftest=",
        'shellBufferSliceEquals(commandStart, commandLen, "sched10")',
    ):
        assert marker in shell

    for marker in (
        "let fairness = kernel_scheduler_fairness_selftest()",
        "version=42",
        " fairness=",
        "fairness != 0",
        "printScheduler10()",
    ):
        assert marker in shell


def test_runtime_v42_netboot_gates_and_sched10_probe_exist() -> None:
    net_iterate = read_repo("scripts/netboot/net-iterate.sh")
    doctor = read_repo("scripts/netboot/netboot-doctor.sh")

    for source in (net_iterate, doctor):
        assert_commands_era_in_netboot_sources(COMMANDS_V42, COMMANDS_V45, source, label="V42")

    assert "schedselftest ok=1 version=42" in net_iterate
    assert "runtime v42: secondary scheduler load balancing" in doctor

    for marker in (
        "probe shell: sched10",
        "^sched10 ok=1 version=42 .*fairness=1 .*stealing=1 .*backpressure=1 .*handoff=1 .*wake=1 .*balances=[1-9][0-9]* .*completions=[1-9][0-9]* .*total=0 .*capacity=8 .*source_core1=[1-9][0-9]* .*source_core2=[0-9][0-9]* .*source_core3=[0-9][0-9]* .*dest_core1=0 .*dest_core2=[0-9][0-9]* .*dest_core3=[0-9][0-9]* .*attempts_core1=[0-9][0-9]* .*attempts_core2=[1-9][0-9]* .*attempts_core3=[1-9][0-9]* .*queue_imbalance=[0-9][0-9]* .*selftest=1",
        "probe shell: req-sched10",
        "^resp id=43 ok=1 cmd=sched10 end",
        "^bootcert ok=1 version=66 .*concurrency=1 .*priority=1 .*fairness=1 .*stealing=[01] .*backpressure=[01] .*handoff=[01] .*wake=[01] .*job_exec=[01] .*worker_feed=[01] .*secondary_workers=[01] .*preemptive=[01] .*smp_scheduler=[01] .*atomics=1 .*locks=1 .*queues=1 .*smp=1 .*scheduler=1 .*certificate=1 .*agent=1 .*runtime=1 .*syscall=1 .*uaccess=1 .*usermode=1 .*process=1 .*loader=1 .*multiprocess=1 .*sdhci=1 .*card=1 .*block=1 .*fat32=1 .*mailbox=1 .*framebuf=1 .*console=1 .*pcie=1 .*vl805=1 .*xhci=1 .*kbd=[01] .*swift=6.3.2 .*events_lost=0",
        "^certificate ok=1 version=63 substrate=1 .*bootcert=[01] .*concurrency=1 .*priority=1 .*fairness=1 .*stealing=[01] .*backpressure=[01] .*handoff=[01] .*wake=[01] .*job_exec=[01] .*worker_feed=[01] .*secondary_workers=[01] .*preemptive=[01] .*smp_scheduler=[01] .*atomics=1 .*locks=1 .*queues=1 .*smp=1 .*scheduler=1 .*agent=1 .*runtime=1 .*memory=1 .*objects=1 .*tasks=1 .*mailboxes=1 .*supervisor=1 .*handles=1 .*events=[0-9]+ .*cancellations=1 .*channels=1 .*drivers=1 .*pressure=1 .*pools=1 .*mmu=1 .*vmm=1 .*asplit=1 .*el0=1 .*syscall=1 .*uaccess=1 .*usermode=1 .*process=1 .*loader=1 .*multiprocess=1 .*sdhci=1 .*card=1 .*block=1 .*fat32=1 .*mailbox=1 .*framebuf=1 .*console=1 .*pcie=1 .*vl805=1 .*xhci=1 .*swift=6.3.2 .*events_lost=0",
        "stale pre-V44 SD fallback",
    ):
        assert marker in net_iterate


def test_runtime_v42_docs_are_updated_after_hardware_proof() -> None:
    readme = read_repo("README.md")
    runbook = read_repo("docs/RUNBOOK.md")
    design = read_repo("docs/CONCURRENCY_DESIGN.md")

    for source in (readme, runbook, design):
        assert "Runtime V42 secondary scheduler load-balancing protocol" in source
        assert "bootcert ok=1 version=42" in source
        assert "fairness=1" in source
        assert "sched10 ok=1 version=42" in source
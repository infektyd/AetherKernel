import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]


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
        "#define KERNEL_SCHEDULER_VERSION 44U",
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
        "kernel_event_emit(KERNEL_EVENT_KIND_BOOT, 44, 0, 0)",
        "runtime v42: secondary scheduler load balancing",
        "kernel_scheduler_enable_load_balancing()",
        "let runtimeV42 = kernel_scheduler_fairness_selftest()",
        "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 42, UInt(runtimeV42), UInt(kernel_scheduler_balance_total()))",
    ):
        assert marker in app


def test_runtime_v42_shell_sched10_and_certificate_surface_exist() -> None:
    shell = read_repo("Sources/Application/UARTShell.swift")

    for marker in (
        COMMANDS_V42,
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
    net_iterate = read_repo("net-iterate.sh")
    doctor = read_repo("netboot-doctor.sh")

    for source in (net_iterate, doctor):
        assert "runtime v42: secondary scheduler load balancing" in source
        assert COMMANDS_V42 in source

    for marker in (
        "probe shell: sched10",
        "^sched10 ok=1 version=42 .*fairness=1 .*stealing=1 .*backpressure=1 .*handoff=1 .*wake=1 .*balances=[1-9][0-9]* .*completions=[1-9][0-9]* .*total=0 .*capacity=8 .*source_core1=[1-9][0-9]* .*source_core2=[0-9][0-9]* .*source_core3=[0-9][0-9]* .*dest_core1=0 .*dest_core2=[1-9][0-9]* .*dest_core3=[1-9][0-9]* .*attempts_core1=[0-9][0-9]* .*attempts_core2=[1-9][0-9]* .*attempts_core3=[1-9][0-9]* .*queue_imbalance=[0-9][0-9]* .*selftest=1",
        "probe shell: req-sched10",
        "^resp id=43 ok=1 cmd=sched10 end",
        "^bootcert ok=1 version=44 .*concurrency=1 .*priority=1 .*fairness=1 .*stealing=1 .*backpressure=1 .*handoff=1 .*wake=1 .*job_exec=1 .*worker_feed=1 .*secondary_workers=1 .*preemptive=1 .*smp_scheduler=1 .*atomics=1 .*locks=1 .*queues=1 .*smp=1 .*scheduler=1 .*certificate=1 .*agent=1 .*runtime=1 .*events_lost=0",
        "^certificate ok=1 version=44 substrate=1 .*bootcert=1 .*concurrency=1 .*priority=1 .*fairness=1 .*stealing=1 .*backpressure=1 .*handoff=1 .*wake=1 .*job_exec=1 .*worker_feed=1 .*secondary_workers=1 .*preemptive=1 .*smp_scheduler=1 .*atomics=1 .*locks=1 .*queues=1 .*smp=1 .*scheduler=1 .*agent=1 .*runtime=1 .*memory=1 .*objects=1 .*tasks=1 .*mailboxes=1 .*supervisor=1 .*handles=1 .*events=1 .*cancellations=1 .*channels=1 .*drivers=1 .*pressure=1 .*pools=1 .*mmu=1 .*events_lost=0",
        "stale pre-V44 SD fallback",
    ):
        assert marker in net_iterate


def test_runtime_v42_docs_are_updated_after_hardware_proof() -> None:
    readme = read_repo("README.md")
    runbook = read_repo("RUNBOOK.md")
    design = read_repo("CONCURRENCY_DESIGN.md")

    for source in (readme, runbook, design):
        assert "Runtime V42 secondary scheduler load-balancing protocol" in source
        assert "bootcert ok=1 version=42" in source
        assert "fairness=1" in source
        assert "sched10 ok=1 version=42" in source
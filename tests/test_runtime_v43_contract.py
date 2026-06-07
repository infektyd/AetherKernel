import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]


COMMANDS_V43 = (
    "commands=help,protocol,status,heap,queues,tasks,tasks2,kobjects,drivers,drivercheck,"
    "mailboxes,sendtest,supervisor,health,capcheck,events,runtime,agent,certificate,sched,sched2,sched3,sched4,sched5,sched6,sched7,sched8,sched9,sched10,sched11,cores,locks,runqueues,diag,irqs,timers,"
    "memcheck,faults,retained,retained-clear,memmap,mmu,pools,poolcheck,"
    "heapfrag,poolstats,frames,heapcheck,framecheck,stress,frameprobe,bootcert,"
    "canceltest,taskcheck,channeltest,bootcheck,soak,heap-invalid-free-test,"
    "heap-double-free-test,panic-test,fault-test,reboot"
)


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_runtime_v43_priority_preemption_contract_exists() -> None:
    support = read_repo("Sources/Support/include/Support.h")
    scheduler = read_repo("Sources/Support/kernel_scheduler.c")

    for marker in (
        "Runtime V43 secondary scheduler priority/preemption protocol",
        "#define KERNEL_SCHEDULER_VERSION 43U",
        "#define KERNEL_SCHEDULER_PRIORITY_TOKEN_BASE 0x4300U",
        "void kernel_scheduler_enable_priority_lanes(void);",
        "unsigned int kernel_scheduler_priority_lanes_enabled(void);",
        "unsigned long kernel_scheduler_priority_low_count(unsigned int core_id);",
        "unsigned long kernel_scheduler_priority_high_count(unsigned int core_id);",
        "unsigned long kernel_scheduler_priority_preempt_count(unsigned int core_id);",
        "unsigned long kernel_scheduler_priority_yield_count(unsigned int core_id);",
        "unsigned long kernel_scheduler_priority_completion_count(unsigned int core_id);",
        "unsigned long kernel_scheduler_priority_preempt_total(void);",
        "unsigned long kernel_scheduler_priority_yield_total(void);",
        "unsigned long kernel_scheduler_priority_completion_total(void);",
        "unsigned long kernel_scheduler_priority_lane_imbalance(void);",
        "int kernel_scheduler_priority_selftest(void);",
    ):
        assert marker in support

    for marker in (
        "Runtime V43 secondary scheduler priority/preemption protocol",
        "priority_low_count",
        "priority_high_count",
        "priority_preempt_count",
        "priority_yield_count",
        "priority_completions",
        "KERNEL_SCHEDULER_PRIORITY_TOKEN_BASE",
        "kernel_scheduler_try_preempt_priority_work",
        "kernel_scheduler_priority_low_count",
        "kernel_scheduler_priority_high_count",
        "kernel_scheduler_priority_preempt_count",
        "kernel_scheduler_priority_yield_count",
        "kernel_scheduler_priority_preempt_total",
        "kernel_scheduler_priority_yield_total",
        "kernel_scheduler_priority_completion_total",
        "kernel_scheduler_priority_lane_imbalance",
        "kernel_scheduler_priority_selftest",
        "kernel_scheduler_priority_preempt_total() >= 2U",
        "kernel_scheduler_priority_yield_total() >= 2U",
        "kernel_scheduler_priority_completion_total() >= 4U",
    ):
        assert marker in scheduler

    smp = read_repo("Sources/Support/kernel_smp.c")
    assert "kernel_scheduler_try_preempt_priority_work(core_id)" in smp

    for marker in (
        "priority_lanes_enabled && is_scheduler_priority_token",
        "KERNEL_SCHEDULER_PRIORITY_TOKEN_BASE | 0x1000U",
        "is_scheduler_steal_token(head_token)",
        "is_scheduler_balance_token(head_token)",
        "kernel_scheduler_try_preempt_priority_work(1)",
        "set_smp_dispatch_enabled(0)",
        "saved_dispatch = kernel_scheduler_smp_dispatch_enabled()",
    ):
        assert marker in scheduler


def test_runtime_v43_application_boot_marker_and_selftest_exist() -> None:
    app = read_repo("Sources/Application/Application.swift")

    for marker in (
        "Runtime V43 adds bounded secondary scheduler priority lanes.",
        "kernel_event_emit(KERNEL_EVENT_KIND_BOOT, 43, 0, 0)",
        "runtime v43: secondary scheduler priority preemption",
        "kernel_scheduler_enable_priority_lanes()",
        "let runtimeV43 = kernel_scheduler_priority_selftest()",
        "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 43, UInt(runtimeV43), UInt(kernel_scheduler_priority_preempt_total()))",
    ):
        assert marker in app


def test_runtime_v43_shell_sched11_and_certificate_surface_exist() -> None:
    shell = read_repo("Sources/Application/UARTShell.swift")

    for marker in (
        COMMANDS_V43,
        "func printScheduler11()",
        "sched11 ok=",
        " version=43",
        " priority=",
        " fairness=",
        " stealing=",
        " backpressure=",
        " handoff=",
        " wake=",
        " preemptions=",
        " yields=",
        " completions=",
        " low=",
        " high=",
        " imbalance=",
        " total=",
        " capacity=",
        " low_core1=",
        " low_core2=",
        " low_core3=",
        " high_core1=",
        " high_core2=",
        " high_core3=",
        " preempt_core1=",
        " preempt_core2=",
        " preempt_core3=",
        " yield_core1=",
        " yield_core2=",
        " yield_core3=",
        " selftest=",
        'shellBufferSliceEquals(commandStart, commandLen, "sched11")',
    ):
        assert marker in shell

    for marker in (
        "let priority = kernel_scheduler_priority_selftest()",
        "version=43",
        " priority=",
        "priority != 0",
        "printScheduler11()",
    ):
        assert marker in shell


def test_runtime_v43_netboot_gates_and_sched11_probe_exist() -> None:
    net_iterate = read_repo("net-iterate.sh")
    doctor = read_repo("netboot-doctor.sh")

    for source in (net_iterate, doctor):
        assert "runtime v43: secondary scheduler priority preemption" in source
        assert COMMANDS_V43 in source

    for marker in (
        "probe shell: sched11",
        "^sched11 ok=1 version=43 .*priority=1 .*fairness=1 .*stealing=1 .*backpressure=1 .*handoff=1 .*wake=1 .*preemptions=[1-9][0-9]* .*yields=[1-9][0-9]* .*completions=[1-9][0-9]* .*total=0 .*capacity=8 .*low_core1=[1-9][0-9]* .*low_core2=[0-9][0-9]* .*low_core3=[0-9][0-9]* .*high_core1=[1-9][0-9]* .*high_core2=[0-9][0-9]* .*high_core3=[0-9][0-9]* .*preempt_core1=[1-9][0-9]* .*preempt_core2=[0-9][0-9]* .*preempt_core3=[0-9][0-9]* .*yield_core1=[1-9][0-9]* .*yield_core2=[0-9][0-9]* .*yield_core3=[0-9][0-9]* .*imbalance=[0-9][0-9]* .*selftest=1",
        "probe shell: req-sched11",
        "^resp id=44 ok=1 cmd=sched11 end",
        "^bootcert ok=1 version=43 .*priority=1 .*fairness=1 .*stealing=1 .*backpressure=1 .*handoff=1 .*wake=1 .*job_exec=1 .*worker_feed=1 .*secondary_workers=1 .*preemptive=1 .*smp_scheduler=1 .*atomics=1 .*locks=1 .*queues=1 .*smp=1 .*scheduler=1 .*certificate=1 .*agent=1 .*runtime=1 .*events_lost=0",
        "^certificate ok=1 version=43 substrate=1 .*bootcert=1 .*priority=1 .*fairness=1 .*stealing=1 .*backpressure=1 .*handoff=1 .*wake=1 .*job_exec=1 .*worker_feed=1 .*secondary_workers=1 .*preemptive=1 .*smp_scheduler=1 .*atomics=1 .*locks=1 .*queues=1 .*smp=1 .*scheduler=1 .*agent=1 .*runtime=1 .*memory=1 .*objects=1 .*tasks=1 .*mailboxes=1 .*supervisor=1 .*handles=1 .*events=1 .*cancellations=1 .*channels=1 .*drivers=1 .*pressure=1 .*pools=1 .*mmu=1 .*events_lost=0",
        "stale pre-V43 SD fallback",
    ):
        assert marker in net_iterate


def test_runtime_v43_shell_lane_provenance_helpers_exist() -> None:
    support = read_repo("Sources/Support/include/Support.h")
    scheduler = read_repo("Sources/Support/kernel_scheduler.c")
    smp = read_repo("Sources/Support/kernel_smp.c")
    shell = read_repo("Sources/Application/UARTShell.swift")

    for marker in (
        "int kernel_scheduler_timer_worker_feed_proven(void);",
        "int kernel_scheduler_secondary_worker_proven(void);",
        "int kernel_scheduler_secondary_job_proven(void);",
        "int kernel_scheduler_secondary_wake_proven(void);",
        "int kernel_scheduler_secondary_handoff_proven(void);",
        "int kernel_scheduler_backpressure_proven(void);",
        "int kernel_scheduler_work_steal_proven(void);",
        "int kernel_scheduler_fairness_proven(void);",
        "int kernel_scheduler_priority_proven(void);",
        "int kernel_scheduler_smp_scheduler_proven(void);",
        "int kernel_scheduler_scheduler_proven(void);",
        "int kernel_scheduler_runqueue_proven(void);",
        "int kernel_smp_proven(void);",
    ):
        assert marker in support

    for marker in (
        "kernel_scheduler_timer_worker_feed_proven",
        "kernel_scheduler_secondary_worker_proven",
        "kernel_scheduler_secondary_job_proven",
        "kernel_scheduler_secondary_wake_proven",
        "kernel_scheduler_secondary_handoff_proven",
        "kernel_scheduler_backpressure_proven",
        "kernel_scheduler_work_steal_proven",
        "kernel_scheduler_fairness_proven",
        "kernel_scheduler_priority_proven",
        "kernel_scheduler_smp_scheduler_proven",
        "kernel_scheduler_scheduler_proven",
        "kernel_scheduler_runqueue_proven",
        "return kernel_scheduler_priority_preempt_total() >= 2U",
        "kernel_scheduler_runqueue_high_water_max() >= KERNEL_SCHEDULER_RUNQUEUE_CAPACITY",
    ):
        assert marker in scheduler

    assert "kernel_smp_proven" in smp

    for marker in (
        "let wake = kernel_scheduler_secondary_wake_proven()",
        "let handoff = kernel_scheduler_secondary_handoff_proven()",
        "let backpressure = kernel_scheduler_backpressure_proven()",
        "let stealing = kernel_scheduler_work_steal_proven()",
        "let fairness = kernel_scheduler_fairness_proven()",
        "let workerFeed = kernel_scheduler_timer_worker_feed_proven()",
        "let secondaryWorkers = kernel_scheduler_secondary_worker_proven()",
        "let jobExec = kernel_scheduler_secondary_job_proven()",
        "let smpScheduler = kernel_scheduler_smp_scheduler_proven()",
        "let queues = kernel_scheduler_runqueue_proven()",
        "let smp = kernel_smp_proven()",
        "let events = kernel_event_log_selftest()",
        "let priority = kernel_scheduler_priority_proven()",
    ):
        assert marker in shell

    bootcert = shell.split("func printBootcert()")[1].split("func ")[0]
    events_idx = bootcert.index("let events = kernel_event_log_selftest()")
    priority_idx = bootcert.index("let priority = kernel_scheduler_priority_proven()")
    assert events_idx < priority_idx

    for forbidden in (
        "let queues = kernel_scheduler_runqueue_selftest()",
        "let smp = kernel_smp_selftest()",
    ):
        bootcert = shell.split("func printBootcert()")[1].split("func ")[0]
        certificate = shell.split("func printSubstrateCertificate()")[1].split("func ")[0]
        assert forbidden not in bootcert, forbidden
        assert forbidden not in certificate, forbidden


def test_runtime_v43_docs_are_updated_after_hardware_proof() -> None:
    readme = read_repo("README.md")
    runbook = read_repo("RUNBOOK.md")
    design = read_repo("CONCURRENCY_DESIGN.md")

    for source in (readme, runbook, design):
        assert "Runtime V43 secondary scheduler priority/preemption protocol" in source
        assert "bootcert ok=1 version=43" in source
        assert "priority=1" in source
        assert "sched11 ok=1 version=43" in source
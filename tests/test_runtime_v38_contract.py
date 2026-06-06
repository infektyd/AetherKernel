import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]


COMMANDS_V38 = (
    "commands=help,protocol,status,heap,queues,tasks,tasks2,kobjects,drivers,drivercheck,"
    "mailboxes,sendtest,supervisor,health,capcheck,events,runtime,agent,certificate,sched,sched2,sched3,sched4,sched5,sched6,cores,locks,runqueues,diag,irqs,timers,"
    "memcheck,faults,retained,retained-clear,memmap,mmu,pools,poolcheck,"
    "heapfrag,poolstats,frames,heapcheck,framecheck,stress,frameprobe,bootcert,"
    "canceltest,taskcheck,channeltest,bootcheck,soak,heap-invalid-free-test,"
    "heap-double-free-test,panic-test,fault-test,reboot"
)


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_runtime_v38_secondary_wake_contract_exists() -> None:
    support = read_repo("Sources/Support/include/Support.h")
    scheduler = read_repo("Sources/Support/kernel_scheduler.c")
    smp = read_repo("Sources/Support/kernel_smp.c")

    for marker in (
        "Runtime V38 secondary scheduler wake protocol",
        "#define KERNEL_SCHEDULER_VERSION 38U",
        "void kernel_scheduler_enable_secondary_wake_signals(void);",
        "unsigned int kernel_scheduler_secondary_wake_signals_enabled(void);",
        "unsigned long kernel_scheduler_secondary_wake_signal_total(void);",
        "unsigned int kernel_scheduler_secondary_wake_signal_mask(void);",
        "unsigned long kernel_scheduler_secondary_wake_target_total(void);",
        "unsigned long kernel_scheduler_secondary_wake_wait_count(unsigned int core_id);",
        "unsigned long kernel_scheduler_secondary_wake_ack_count(unsigned int core_id);",
        "unsigned long kernel_scheduler_secondary_wake_wait_total(void);",
        "unsigned long kernel_scheduler_secondary_wake_ack_total(void);",
        "unsigned long kernel_scheduler_secondary_wake_gap(void);",
        "unsigned long kernel_scheduler_secondary_wake_imbalance(void);",
        "int kernel_scheduler_secondary_wake_selftest(void);",
        "void kernel_smp_signal_scheduler_work(unsigned int target_mask);",
        "void kernel_smp_secondary_wait_for_work(unsigned int core_id);",
        "unsigned long kernel_smp_scheduler_signal_count(void);",
        "unsigned int kernel_smp_scheduler_signal_mask(void);",
        "unsigned long kernel_smp_scheduler_signal_target_total(void);",
        "unsigned long kernel_smp_core_scheduler_wait_count(unsigned int core_id);",
        "unsigned long kernel_smp_core_scheduler_wake_count(unsigned int core_id);",
        "int kernel_smp_scheduler_wake_selftest(void);",
    ):
        assert marker in support

    for marker in (
        "Runtime V38 secondary scheduler wake protocol",
        "secondary_wake_signals_enabled",
        "signal_secondary_work_for_core",
        "kernel_smp_signal_scheduler_work",
        "wake imbalance is telemetry, not a gate",
        "kernel_scheduler_secondary_wake_selftest",
    ):
        assert marker in scheduler

    for marker in (
        "Runtime V38 scheduler wake protocol",
        "scheduler_waits",
        "scheduler_wakes",
        "scheduler_signal_count",
        "scheduler_signal_target_total",
        "kernel_smp_signal_scheduler_work",
        "__asm__ volatile(\"sev\"",
        "kernel_smp_secondary_wait_for_work",
        "__asm__ volatile(\"sevl\"",
        "kernel_scheduler_runqueue_count(core_id) != 0",
        "__asm__ volatile(\"wfe\"",
        "kernel_smp_secondary_wait_for_work(core_id)",
    ):
        assert marker in smp


def test_runtime_v38_application_boot_marker_and_selftest_exist() -> None:
    app = read_repo("Sources/Application/Application.swift")

    for marker in (
        "Runtime V38 adds SEV/WFE secondary scheduler wakeups.",
        "kernel_event_emit(KERNEL_EVENT_KIND_BOOT, 38, 0, 0)",
        "kernel_scheduler_enable_secondary_wake_signals()",
        "runtime v38: secondary scheduler wake protocol",
        "let runtimeV38 = kernel_scheduler_secondary_wake_selftest()",
        "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 38, UInt(runtimeV38), UInt(kernel_scheduler_secondary_wake_signal_total()))",
    ):
        assert marker in app


def test_runtime_v38_shell_sched6_and_certificate_surface_exist() -> None:
    shell = read_repo("Sources/Application/UARTShell.swift")

    for marker in (
        COMMANDS_V38,
        "func printScheduler6()",
        "sched6 ok=",
        " version=38",
        " wake=",
        " job_exec=",
        " worker_feed=",
        " signals=",
        " mask=",
        " targets=",
        " waits=",
        " wakes=",
        " gap=",
        " imbalance=",
        " core0_wait=",
        " core1_wait=",
        " core2_wait=",
        " core3_wait=",
        " core0_wake=",
        " core1_wake=",
        " core2_wake=",
        " core3_wake=",
        " selftest=",
        'shellBufferSliceEquals(commandStart, commandLen, "sched6")',
    ):
        assert marker in shell

    for marker in (
        "let wake = kernel_scheduler_secondary_wake_selftest()",
        "version=38",
        " wake=",
        "wake != 0",
        "printScheduler6()",
    ):
        assert marker in shell


def test_runtime_v38_netboot_gates_and_sched6_probe_exist() -> None:
    net_iterate = read_repo("net-iterate.sh")
    doctor = read_repo("netboot-doctor.sh")

    for source in (net_iterate, doctor):
        assert "runtime v38: secondary scheduler wake protocol" in source
        assert COMMANDS_V38 in source

    for marker in (
        "probe shell: sched6",
        "^sched6 ok=1 version=38 .*wake=1 .*job_exec=1 .*worker_feed=1 .*signals=[1-9][0-9]* .*mask=0xe .*targets=[1-9][0-9]* .*waits=[1-9][0-9]* .*wakes=[1-9][0-9]* .*gap=[0-9][0-9]* .*imbalance=[0-9][0-9]* .*core0_wait=0 .*core1_wait=[1-9][0-9]* .*core2_wait=[1-9][0-9]* .*core3_wait=[1-9][0-9]* .*core0_wake=0 .*core1_wake=[1-9][0-9]* .*core2_wake=[1-9][0-9]* .*core3_wake=[1-9][0-9]* .*selftest=1",
        "probe shell: req-sched6",
        "^resp id=39 ok=1 cmd=sched6 end",
        "^bootcert ok=1 version=38 .*wake=1 .*job_exec=1 .*worker_feed=1 .*secondary_workers=1 .*preemptive=1 .*smp_scheduler=1 .*atomics=1 .*locks=1 .*queues=1 .*smp=1 .*scheduler=1 .*certificate=1 .*agent=1 .*runtime=1 .*events_lost=0",
        "^certificate ok=1 version=38 substrate=1 .*bootcert=1 .*wake=1 .*job_exec=1 .*worker_feed=1 .*secondary_workers=1 .*preemptive=1 .*smp_scheduler=1 .*atomics=1 .*locks=1 .*queues=1 .*smp=1 .*scheduler=1 .*agent=1 .*runtime=1 .*memory=1 .*objects=1 .*tasks=1 .*mailboxes=1 .*supervisor=1 .*handles=1 .*events=1 .*cancellations=1 .*channels=1 .*drivers=1 .*pressure=1 .*pools=1 .*mmu=1 .*events_lost=0",
        "stale pre-V38 SD fallback",
    ):
        assert marker in net_iterate


def test_runtime_v38_docs_are_updated_after_hardware_proof() -> None:
    readme = read_repo("README.md")
    runbook = read_repo("RUNBOOK.md")
    design = read_repo("CONCURRENCY_DESIGN.md")

    for source in (readme, runbook, design):
        assert "Runtime V38 secondary scheduler wake protocol" in source
        assert "bootcert ok=1 version=38" in source
        assert "wake=1" in source
        assert "sched6 ok=1 version=38" in source

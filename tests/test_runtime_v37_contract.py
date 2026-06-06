import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]


COMMANDS_V37 = (
    "commands=help,protocol,status,heap,queues,tasks,tasks2,kobjects,drivers,drivercheck,"
    "mailboxes,sendtest,supervisor,health,capcheck,events,runtime,agent,certificate,sched,sched2,sched3,sched4,sched5,sched6,cores,locks,runqueues,diag,irqs,timers,"
    "memcheck,faults,retained,retained-clear,memmap,mmu,pools,poolcheck,"
    "heapfrag,poolstats,frames,heapcheck,framecheck,stress,frameprobe,bootcert,"
    "canceltest,taskcheck,channeltest,bootcheck,soak,heap-invalid-free-test,"
    "heap-double-free-test,panic-test,fault-test,reboot"
)


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_runtime_v37_secondary_job_execution_contract_exists() -> None:
    support = read_repo("Sources/Support/include/Support.h")
    scheduler = read_repo("Sources/Support/kernel_scheduler.c")

    for marker in (
        "Runtime V37 timer-fed secondary C scheduler jobs",
        "#define KERNEL_SCHEDULER_VERSION 38U",
        "#define KERNEL_SCHEDULER_JOB_TOKEN_BASE 0x3700U",
        "#define KERNEL_SCHEDULER_JOB_OP_CHECKSUM 1U",
        "void kernel_scheduler_enable_secondary_job_execution(void);",
        "unsigned int kernel_scheduler_secondary_job_execution_enabled(void);",
        "unsigned long kernel_scheduler_secondary_job_execution_count(unsigned int core_id);",
        "unsigned long kernel_scheduler_secondary_job_completion_count(unsigned int core_id);",
        "unsigned long kernel_scheduler_secondary_job_noop_count(unsigned int core_id);",
        "unsigned long kernel_scheduler_secondary_job_checksum(unsigned int core_id);",
        "unsigned long kernel_scheduler_secondary_job_total(void);",
        "unsigned long kernel_scheduler_secondary_job_completion_total(void);",
        "unsigned long kernel_scheduler_secondary_job_noop_total(void);",
        "unsigned long kernel_scheduler_secondary_job_checksum_total(void);",
        "unsigned long kernel_scheduler_secondary_job_min(void);",
        "unsigned long kernel_scheduler_secondary_job_max(void);",
        "unsigned long kernel_scheduler_secondary_job_imbalance(void);",
        "unsigned long kernel_scheduler_secondary_job_completion_gap(void);",
        "int kernel_scheduler_secondary_job_selftest(void);",
    ):
        assert marker in support

    for marker in (
        "Runtime V37 timer-fed secondary C scheduler jobs",
        "secondary_job_execution_enabled",
        "job_executions",
        "job_completions",
        "job_noops",
        "job_checksum",
        "scheduler_job_token_for_core",
        "is_scheduler_job_token",
        "execute_scheduler_job_for_core",
        "kernel_scheduler_secondary_job_selftest",
    ):
        assert marker in scheduler


def test_runtime_v37_application_boot_marker_and_selftest_exist() -> None:
    app = read_repo("Sources/Application/Application.swift")

    for marker in (
        "Runtime V37 adds timer-fed secondary C scheduler jobs.",
        "kernel_event_emit(KERNEL_EVENT_KIND_BOOT, 38, 0, 0)",
        "kernel_scheduler_enable_secondary_job_execution()",
        "runtime v37: timer-fed secondary C scheduler jobs",
        "let runtimeV37 = kernel_scheduler_secondary_job_selftest()",
        "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 37, UInt(runtimeV37), UInt(kernel_scheduler_secondary_job_total()))",
    ):
        assert marker in app


def test_runtime_v37_shell_sched5_and_certificate_surface_exist() -> None:
    shell = read_repo("Sources/Application/UARTShell.swift")

    for marker in (
        COMMANDS_V37,
        "func printScheduler5()",
        "sched5 ok=",
        " version=37",
        " job_exec=",
        " worker_feed=",
        " secondary_workers=",
        " executions=",
        " completions=",
        " noops=",
        " checksum=",
        " gap=",
        " imbalance=",
        " core0_exec=",
        " core1_exec=",
        " core2_exec=",
        " core3_exec=",
        " core0_done=",
        " core1_done=",
        " core2_done=",
        " core3_done=",
        " selftest=",
        'shellBufferSliceEquals(commandStart, commandLen, "sched5")',
    ):
        assert marker in shell

    for marker in (
        "let jobExec = kernel_scheduler_secondary_job_selftest()",
        "version=37",
        " job_exec=",
        "jobExec != 0",
        "printScheduler5()",
    ):
        assert marker in shell


def test_runtime_v37_netboot_gates_and_sched5_probe_exist() -> None:
    net_iterate = read_repo("net-iterate.sh")
    doctor = read_repo("netboot-doctor.sh")

    for source in (net_iterate, doctor):
        assert "runtime v37: timer-fed secondary C scheduler jobs" in source
        assert COMMANDS_V37 in source

    for marker in (
        "probe shell: sched5",
        "^sched5 ok=1 version=37 .*job_exec=1 .*worker_feed=1 .*secondary_workers=1 .*executions=[1-9][0-9]* .*completions=[1-9][0-9]* .*noops=[0-9][0-9]* .*checksum=[1-9][0-9]* .*gap=[0-9][0-9]* .*imbalance=[0-9][0-9]* .*core0_exec=0 .*core1_exec=[1-9][0-9]* .*core2_exec=[1-9][0-9]* .*core3_exec=[1-9][0-9]* .*core0_done=0 .*core1_done=[1-9][0-9]* .*core2_done=[1-9][0-9]* .*core3_done=[1-9][0-9]* .*selftest=1",
        "probe shell: req-sched5",
        "^resp id=38 ok=1 cmd=sched5 end",
        "^bootcert ok=1 version=38 .*wake=1 .*job_exec=1 .*worker_feed=1 .*secondary_workers=1 .*preemptive=1 .*smp_scheduler=1 .*atomics=1 .*locks=1 .*queues=1 .*smp=1 .*scheduler=1 .*certificate=1 .*agent=1 .*runtime=1 .*events_lost=0",
        "^certificate ok=1 version=38 substrate=1 .*bootcert=1 .*wake=1 .*job_exec=1 .*worker_feed=1 .*secondary_workers=1 .*preemptive=1 .*smp_scheduler=1 .*atomics=1 .*locks=1 .*queues=1 .*smp=1 .*scheduler=1 .*agent=1 .*runtime=1 .*memory=1 .*objects=1 .*tasks=1 .*mailboxes=1 .*supervisor=1 .*handles=1 .*events=1 .*cancellations=1 .*channels=1 .*drivers=1 .*pressure=1 .*pools=1 .*mmu=1 .*events_lost=0",
        "stale pre-V38 SD fallback",
    ):
        assert marker in net_iterate


def test_runtime_v37_docs_are_updated_after_hardware_proof() -> None:
    readme = read_repo("README.md")
    runbook = read_repo("RUNBOOK.md")
    design = read_repo("CONCURRENCY_DESIGN.md")

    for source in (readme, runbook, design):
        assert "Runtime V37 timer-fed secondary C scheduler jobs" in source
        assert "bootcert ok=1 version=37" in source
        assert "job_exec=1" in source
        assert "sched5 ok=1 version=37" in source

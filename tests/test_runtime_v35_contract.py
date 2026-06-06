import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]


COMMANDS_V35 = (
    "commands=help,protocol,status,heap,queues,tasks,tasks2,kobjects,drivers,drivercheck,"
    "mailboxes,sendtest,supervisor,health,capcheck,events,runtime,agent,certificate,sched,sched2,sched3,sched4,sched5,cores,locks,runqueues,diag,irqs,timers,"
    "memcheck,faults,retained,retained-clear,memmap,mmu,pools,poolcheck,"
    "heapfrag,poolstats,frames,heapcheck,framecheck,stress,frameprobe,bootcert,"
    "canceltest,taskcheck,channeltest,bootcheck,soak,heap-invalid-free-test,"
    "heap-double-free-test,panic-test,fault-test,reboot"
)


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_runtime_v35_secondary_worker_scheduler_contract_exists() -> None:
    support = read_repo("Sources/Support/include/Support.h")
    scheduler = read_repo("Sources/Support/kernel_scheduler.c")
    smp = read_repo("Sources/Support/kernel_smp.c")

    for marker in (
        "Runtime V35 secondary-owned scheduler workers",
        "#define KERNEL_SCHEDULER_VERSION 37U",
        "#define KERNEL_SCHEDULER_WORKER_TOKEN_BASE 0x3500U",
        "void kernel_scheduler_enable_secondary_workers(void);",
        "unsigned int kernel_scheduler_secondary_workers_enabled(void);",
        "void kernel_scheduler_secondary_worker_tick(unsigned int core_id);",
        "unsigned long kernel_scheduler_worker_drain_count(unsigned int core_id);",
        "unsigned long kernel_scheduler_worker_idle_count(unsigned int core_id);",
        "unsigned long kernel_scheduler_total_worker_drain_count(void);",
        "unsigned long kernel_scheduler_total_worker_idle_count(void);",
        "unsigned long kernel_scheduler_secondary_worker_total(void);",
        "unsigned long kernel_scheduler_secondary_worker_min(void);",
        "unsigned long kernel_scheduler_secondary_worker_max(void);",
        "unsigned long kernel_scheduler_secondary_worker_imbalance(void);",
        "int kernel_scheduler_secondary_worker_selftest(void);",
    ):
        assert marker in support

    for marker in (
        "Runtime V35 secondary-owned scheduler workers",
        "secondary_workers_enabled",
        "worker_drains",
        "worker_idles",
        "kernel_scheduler_secondary_worker_tick",
        "kernel_scheduler_dequeue(core_id, &token)",
        "kernel_scheduler_secondary_worker_selftest",
    ):
        assert marker in scheduler

    for marker in (
        "Runtime V35 secondary scheduler worker loop",
        "kernel_scheduler_secondary_worker_tick(core_id)",
    ):
        assert marker in smp


def test_runtime_v35_application_boot_marker_and_selftest_exist() -> None:
    app = read_repo("Sources/Application/Application.swift")

    for marker in (
        "Runtime V35 adds C-only secondary scheduler workers.",
        "kernel_event_emit(KERNEL_EVENT_KIND_BOOT, 37, 0, 0)",
        "kernel_scheduler_enable_secondary_workers()",
        "runtime v35: secondary-owned scheduler workers",
        "let runtimeV35 = kernel_scheduler_secondary_worker_selftest()",
        "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 35, UInt(runtimeV35), UInt(kernel_scheduler_secondary_worker_total()))",
    ):
        assert marker in app


def test_runtime_v35_shell_sched3_and_certificate_surface_exist() -> None:
    shell = read_repo("Sources/Application/UARTShell.swift")

    for marker in (
        COMMANDS_V35,
        "func printScheduler3()",
        "sched3 ok=",
        " version=35",
        " secondary_workers=",
        " active=",
        " cores=",
        " online=",
        " worker_drains=",
        " worker_idles=",
        " min=",
        " max=",
        " imbalance=",
        " core0=",
        " core1=",
        " core2=",
        " core3=",
        " selftest=",
        'shellBufferSliceEquals(commandStart, commandLen, "sched3")',
    ):
        assert marker in shell

    for marker in (
        "let secondaryWorkers = kernel_scheduler_secondary_worker_selftest()",
        "version=35",
        " secondary_workers=",
        "secondaryWorkers != 0",
        "printScheduler3()",
    ):
        assert marker in shell


def test_runtime_v35_netboot_gates_and_sched3_probe_exist() -> None:
    net_iterate = read_repo("net-iterate.sh")
    doctor = read_repo("netboot-doctor.sh")

    for source in (net_iterate, doctor):
        assert "runtime v35: secondary-owned scheduler workers" in source
        assert COMMANDS_V35 in source

    for marker in (
        "probe shell: sched3",
        "^sched3 ok=1 version=35 .*secondary_workers=1 .*active=1 .*cores=4 .*online=4 .*worker_drains=[1-9][0-9]* .*worker_idles=[0-9][0-9]* .*imbalance=[0-9][0-9]* .*core0=[0-9][0-9]* .*core1=[1-9][0-9]* .*core2=[1-9][0-9]* .*core3=[1-9][0-9]* .*selftest=1",
        "probe shell: req-sched3",
        "^resp id=36 ok=1 cmd=sched3 end",
        "^bootcert ok=1 version=37 .*job_exec=1 .*worker_feed=1 .*secondary_workers=1 .*preemptive=1 .*smp_scheduler=1 .*atomics=1 .*locks=1 .*queues=1 .*smp=1 .*scheduler=1 .*certificate=1 .*agent=1 .*runtime=1 .*events_lost=0",
        "^certificate ok=1 version=37 substrate=1 .*bootcert=1 .*job_exec=1 .*worker_feed=1 .*secondary_workers=1 .*preemptive=1 .*smp_scheduler=1 .*atomics=1 .*locks=1 .*queues=1 .*smp=1 .*scheduler=1 .*agent=1 .*runtime=1 .*memory=1 .*objects=1 .*tasks=1 .*mailboxes=1 .*supervisor=1 .*handles=1 .*events=1 .*cancellations=1 .*channels=1 .*drivers=1 .*pressure=1 .*pools=1 .*mmu=1 .*events_lost=0",
        "stale pre-V37 SD fallback",
    ):
        assert marker in net_iterate


def test_runtime_v35_docs_are_updated_after_hardware_proof() -> None:
    readme = read_repo("README.md")
    runbook = read_repo("RUNBOOK.md")
    design = read_repo("CONCURRENCY_DESIGN.md")

    for source in (readme, runbook, design):
        assert "Runtime V35 secondary-owned scheduler workers" in source
        assert "bootcert ok=1 version=35" in source
        assert "secondary_workers=1" in source
        assert "sched3 ok=1 version=35" in source

import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]

COMMANDS_V22 = (
    "commands=help,protocol,status,heap,queues,tasks,tasks2,kobjects,drivers,drivercheck,mailboxes,sendtest,"
    "supervisor,health,capcheck,events,runtime,agent,certificate,sched,sched2,sched3,sched4,sched5,sched6,sched7,cores,locks,runqueues,diag,irqs,timers,memcheck,faults,retained,"
    "retained-clear,memmap,mmu,pools,poolcheck,heapfrag,poolstats,frames,heapcheck,framecheck,stress,"
    "frameprobe,bootcert,canceltest,taskcheck,channeltest,bootcheck,soak,"
    "heap-invalid-free-test,heap-double-free-test,panic-test,fault-test,reboot"
)


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_runtime_v22_guarded_pool_support_api_exists() -> None:
    support = read_repo("Sources/Support/include/Support.h")
    pool = read_repo("Sources/Support/kernel_pool.c")

    for marker in (
        "KERNEL_POOL_SELFTEST_ID",
        "KERNEL_POOL_ERROR_NONE",
        "KERNEL_POOL_ERROR_FULL",
        "KERNEL_POOL_ERROR_BAD_FREE",
        "KERNEL_POOL_ERROR_DOUBLE_FREE",
        "KERNEL_POOL_ERROR_GUARD",
        "kernel_pool_init",
        "kernel_pool_count",
        "kernel_pool_capacity",
        "kernel_pool_name_len",
        "kernel_pool_name_byte",
        "kernel_pool_slot_size",
        "kernel_pool_slot_capacity",
        "kernel_pool_used",
        "kernel_pool_high_water",
        "kernel_pool_alloc_count",
        "kernel_pool_free_count",
        "kernel_pool_failed_alloc_count",
        "kernel_pool_bad_free_count",
        "kernel_pool_double_free_count",
        "kernel_pool_last_error",
        "kernel_pool_generation",
        "kernel_pool_alloc",
        "kernel_pool_free",
        "kernel_pool_selftest",
    ):
        assert marker in support

    for marker in (
        "Runtime V22 fixed guarded typed pools",
        "#define KERNEL_POOL_CAPACITY_VALUE 4U",
        "#define KERNEL_POOL_SLOT_CAPACITY_VALUE 8U",
        "#define KERNEL_POOL_GUARD_HEAD",
        "#define KERNEL_POOL_GUARD_TAIL",
        "typedef struct kernel_pool_slot",
        "typedef struct kernel_pool_record",
        "kernel_pool_alloc",
        "kernel_pool_free",
        "kernel_pool_selftest",
        "irq_save()",
    ):
        assert marker in pool


def test_runtime_v22_application_shell_and_bootcert_surface_exist() -> None:
    app = read_repo("Sources/Application/Application.swift")
    shell = read_repo("Sources/Application/UARTShell.swift")

    for marker in (
        "kernel_pool_init()",
        "kernel_event_emit(KERNEL_EVENT_KIND_BOOT, 39, 0, 0)",
        "runtime v22: guarded typed pools",
        "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 22, UInt(kernel_pool_selftest()), UInt(kernel_pool_count()))",
    ):
        assert marker in app

    for marker in (
        "V22 adds fixed guarded typed pools",
        COMMANDS_V22,
        "func printPools()",
        "func printPoolcheck()",
        "pools count=",
        "pool index=",
        " name=",
        " slot_size=",
        " used=",
        " high_water=",
        " allocs=",
        " failed=",
        " bad_frees=",
        " double_frees=",
        " generation=",
        "poolcheck ok=",
        "kernel_pool_selftest()",
        'shellBufferSliceEquals(commandStart, commandLen, "pools")',
        'shellBufferSliceEquals(commandStart, commandLen, "poolcheck")',
        " version=29",
        " pools=",
    ):
        assert marker in shell


def test_runtime_v22_netboot_gates_and_probe_exist() -> None:
    net_iterate = read_repo("net-iterate.sh")
    doctor = read_repo("netboot-doctor.sh")

    for source in (net_iterate, doctor):
        assert "runtime v22: guarded typed pools" in source
        assert COMMANDS_V22 in source

    for marker in (
        "probe shell: poolcheck",
        "^poolcheck ok=1 .*bad_frees=1 .*double_frees=1",
        "probe shell: pools",
        "^pools count=.* capacity=.* selftest=1",
        "^bootcert ok=1 version=39 .*handoff=1 .*wake=1 .*job_exec=1 .*worker_feed=1 .*secondary_workers=1 .*preemptive=1 .*smp_scheduler=1 .*atomics=1 .*locks=1 .*queues=1 .*smp=1 .*scheduler=1 .*certificate=1 .*agent=1 .*runtime=1 .*events_lost=0",
        "stale pre-V39 SD fallback",
    ):
        assert marker in net_iterate


def test_runtime_v22_docs_are_updated_after_hardware_proof() -> None:
    readme = read_repo("README.md")
    runbook = read_repo("RUNBOOK.md")
    design = read_repo("CONCURRENCY_DESIGN.md")

    for source in (readme, runbook, design):
        assert "Runtime V22 guarded typed pools" in source
        assert "poolcheck ok=1" in source
        assert "bootcert ok=1 version=22" in source

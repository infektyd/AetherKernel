import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]

COMMANDS_V23 = (
    "commands=help,protocol,status,heap,queues,tasks,tasks2,kobjects,drivers,drivercheck,mailboxes,sendtest,"
    "supervisor,health,capcheck,events,runtime,agent,certificate,sched,sched2,sched3,sched4,sched5,cores,locks,runqueues,diag,irqs,timers,memcheck,faults,retained,"
    "retained-clear,memmap,mmu,pools,poolcheck,heapfrag,poolstats,frames,"
    "heapcheck,framecheck,stress,frameprobe,bootcert,canceltest,taskcheck,"
    "channeltest,bootcheck,soak,heap-invalid-free-test,heap-double-free-test,"
    "panic-test,fault-test,reboot"
)


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_runtime_v23_allocator_fragmentation_api_exists() -> None:
    support = read_repo("Sources/Support/include/Support.h")
    alloc = read_repo("Sources/Support/alloc.c")

    for marker in (
        "heap_free_block_count",
        "heap_allocated_block_count",
        "heap_smallest_free_bytes",
        "heap_fragmentation_permil",
        "heap_pressure_last_free_block_count",
        "heap_pressure_last_largest_free_bytes",
        "heap_fragmentation_selftest",
    ):
        assert marker in support

    for marker in (
        "Runtime V23 allocator fragmentation telemetry",
        "static unsigned long heap_free_block_count_unsafe",
        "static unsigned long heap_allocated_block_count_unsafe",
        "static unsigned long heap_smallest_free_bytes_unsafe",
        "heap_fragmentation_permil",
        "heap_pressure_last_free_blocks",
        "heap_pressure_last_largest_free",
        "heap_fragmentation_selftest",
    ):
        assert marker in alloc


def test_runtime_v23_pool_pressure_aggregate_api_exists() -> None:
    support = read_repo("Sources/Support/include/Support.h")
    pool = read_repo("Sources/Support/kernel_pool.c")

    for marker in (
        "kernel_pool_total_slot_count",
        "kernel_pool_used_slot_count",
        "kernel_pool_high_water_slot_count",
        "kernel_pool_failed_alloc_total",
        "kernel_pool_bad_free_total",
        "kernel_pool_double_free_total",
        "kernel_pool_pressure_selftest",
    ):
        assert marker in support

    for marker in (
        "Runtime V23 pool pressure aggregate telemetry",
        "kernel_pool_total_slot_count",
        "kernel_pool_used_slot_count",
        "kernel_pool_high_water_slot_count",
        "kernel_pool_failed_alloc_total",
        "kernel_pool_bad_free_total",
        "kernel_pool_double_free_total",
        "kernel_pool_pressure_selftest",
    ):
        assert marker in pool


def test_runtime_v23_application_shell_and_bootcert_surface_exist() -> None:
    app = read_repo("Sources/Application/Application.swift")
    shell = read_repo("Sources/Application/UARTShell.swift")

    for marker in (
        "Runtime V23 adds allocator/pool pressure telemetry.",
        "kernel_event_emit(KERNEL_EVENT_KIND_BOOT, 37, 0, 0)",
        "runtime v23: allocator and pool pressure telemetry",
        "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 23, UInt(heap_fragmentation_selftest()), UInt(kernel_pool_pressure_selftest()))",
    ):
        assert marker in app

    for marker in (
        "V23 adds allocator/pool pressure telemetry",
        COMMANDS_V23,
        "func printHeapfrag()",
        "func printPoolstats()",
        "heapfrag ok=",
        " free_blocks=",
        " allocated_blocks=",
        " smallest_free=",
        " fragmentation_permil=",
        " pressure_free_blocks=",
        " pressure_largest_free=",
        "poolstats ok=",
        " total_slots=",
        " used_slots=",
        " high_water_slots=",
        " failed_allocs=",
        'shellBufferSliceEquals(commandStart, commandLen, "heapfrag")',
        'shellBufferSliceEquals(commandStart, commandLen, "poolstats")',
        " version=29",
        " pressure=",
    ):
        assert marker in shell


def test_runtime_v23_netboot_gates_and_probes_exist() -> None:
    net_iterate = read_repo("net-iterate.sh")
    doctor = read_repo("netboot-doctor.sh")

    for source in (net_iterate, doctor):
        assert "runtime v23: allocator and pool pressure telemetry" in source
        assert COMMANDS_V23 in source

    for marker in (
        "probe shell: heapfrag",
        "^heapfrag ok=1 .*fragmentation_permil=.*pressure_largest_free=",
        "probe shell: poolstats",
        "^poolstats ok=1 .*total_slots=.*failed_allocs=",
        "^bootcert ok=1 version=37 .*job_exec=1 .*worker_feed=1 .*secondary_workers=1 .*preemptive=1 .*smp_scheduler=1 .*atomics=1 .*locks=1 .*queues=1 .*smp=1 .*scheduler=1 .*certificate=1 .*agent=1 .*runtime=1 .*events_lost=0",
        "stale pre-V37 SD fallback",
    ):
        assert marker in net_iterate


def test_runtime_v23_docs_are_updated_after_hardware_proof() -> None:
    readme = read_repo("README.md")
    runbook = read_repo("RUNBOOK.md")
    design = read_repo("CONCURRENCY_DESIGN.md")

    for source in (readme, runbook, design):
        assert "Runtime V23 allocator/pool pressure telemetry" in source
        assert "heapfrag ok=1" in source
        assert "poolstats ok=1" in source
        assert "bootcert ok=1 version=29" in source

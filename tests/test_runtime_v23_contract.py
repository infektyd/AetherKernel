import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]

from tests.commands_contract_helpers import (
    assert_commands_era_in_netboot_sources,
    assert_commands_era_prefix_of_live,
)
from tests.test_runtime_v45_contract import COMMANDS_V45

COMMANDS_V23 = (
    "commands=help,protocol,status,heap,queues,tasks,tasks2,kobjects,drivers,drivercheck,mailboxes,sendtest,"
    "supervisor,health,capcheck,events,runtime,agent,certificate,sched,sched2,sched3,sched4,sched5,sched6,sched7,sched8,sched9,sched10,sched11,sched12,cores,locks,runqueues,diag,irqs,timers,memcheck,faults,retained,"
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
    assert_commands_era_prefix_of_live(COMMANDS_V23, label="V23")


    for marker in (
        "Runtime V23 adds allocator/pool pressure telemetry.",
        "kernel_event_emit(KERNEL_EVENT_KIND_BOOT, 44, 0, 0)",
        "runtime v23: allocator and pool pressure telemetry",
        "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 23, UInt(heap_fragmentation_selftest()), UInt(kernel_pool_pressure_selftest()))",
    ):
        assert marker in app

    for marker in (
        "V23 adds allocator/pool pressure telemetry",
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
        " version=40",
        " pressure=",
    ):
        assert marker in shell


def test_runtime_v23_netboot_gates_and_probes_exist() -> None:
    net_iterate = read_repo("scripts/netboot/net-iterate.sh")
    doctor = read_repo("scripts/netboot/netboot-doctor.sh")

    for source in (net_iterate, doctor):
        assert "runtime v23: allocator and pool pressure telemetry" in source
        assert_commands_era_in_netboot_sources(COMMANDS_V23, COMMANDS_V45, source, label="V23")

    for marker in (
        "probe shell: heapfrag",
        "^heapfrag ok=1 .*fragmentation_permil=.*pressure_largest_free=",
        "probe shell: poolstats",
        "^poolstats ok=1 .*total_slots=.*failed_allocs=",
        "^bootcert ok=1 version=44 .*concurrency=1 .*priority=1 .*fairness=1 .*stealing=1 .*backpressure=1 .*handoff=1 .*wake=1 .*job_exec=1 .*worker_feed=1 .*secondary_workers=1 .*preemptive=1 .*smp_scheduler=1 .*atomics=1 .*locks=1 .*queues=1 .*smp=1 .*scheduler=1 .*certificate=1 .*agent=1 .*runtime=1 .*events_lost=0",
        "stale pre-V43 SD fallback",
    ):
        assert marker in net_iterate


def test_runtime_v23_docs_are_updated_after_hardware_proof() -> None:
    readme = read_repo("README.md")
    runbook = read_repo("docs/RUNBOOK.md")
    design = read_repo("docs/CONCURRENCY_DESIGN.md")

    for source in (readme, runbook, design):
        assert "Runtime V23 allocator/pool pressure telemetry" in source
        assert "heapfrag ok=1" in source
        assert "poolstats ok=1" in source
        assert "bootcert ok=1 version=29" in source

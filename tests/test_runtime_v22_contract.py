import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]

from tests.commands_contract_helpers import (
    assert_commands_era_in_netboot_sources,
    assert_commands_era_prefix_of_live,
)
from tests.test_runtime_v45_contract import COMMANDS_V45

COMMANDS_V22 = (
    "commands=help,protocol,status,heap,queues,tasks,tasks2,kobjects,drivers,drivercheck,mailboxes,sendtest,"
    "supervisor,health,capcheck,events,runtime,agent,certificate,sched,sched2,sched3,sched4,sched5,sched6,sched7,sched8,sched9,sched10,sched11,sched12,cores,locks,runqueues,diag,irqs,timers,memcheck,faults,retained,"
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
    assert_commands_era_prefix_of_live(COMMANDS_V22, label="V22")


    for marker in (
        "kernel_pool_init()",
        "kernel_event_emit(KERNEL_EVENT_KIND_BOOT, 64, 0, 0)",
        "runtime v22: guarded typed pools",
        "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 22, UInt(kernel_pool_selftest()), UInt(kernel_pool_count()))",
    ):
        assert marker in app

    for marker in (
        "V22 adds fixed guarded typed pools",
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
        " version=40",
        " pools=",
    ):
        assert marker in shell


def test_runtime_v22_netboot_gates_and_probe_exist() -> None:
    net_iterate = read_repo("scripts/netboot/net-iterate.sh")
    doctor = read_repo("scripts/netboot/netboot-doctor.sh")

    for source in (net_iterate, doctor):
        assert "runtime v22: guarded typed pools" in source
        assert_commands_era_in_netboot_sources(COMMANDS_V22, COMMANDS_V45, source, label="V22")

    for marker in (
        "probe shell: poolcheck",
        "^poolcheck ok=1 .*bad_frees=1 .*double_frees=1",
        "probe shell: pools",
        "^pools count=.* capacity=.* selftest=1",
        "^bootcert ok=1 version=66 .*concurrency=1 .*priority=1 .*fairness=1 .*stealing=[01] .*backpressure=[01] .*handoff=[01] .*wake=[01] .*job_exec=[01] .*worker_feed=[01] .*secondary_workers=[01] .*preemptive=[01] .*smp_scheduler=[01] .*atomics=1 .*locks=1 .*queues=1 .*smp=1 .*scheduler=1 .*certificate=1 .*agent=1 .*runtime=1 .*syscall=1 .*uaccess=1 .*usermode=1 .*process=1 .*loader=1 .*multiprocess=1 .*sdhci=1 .*card=1 .*block=1 .*fat32=1 .*mailbox=1 .*framebuf=1 .*console=1 .*pcie=1 .*vl805=1 .*xhci=1 .*kbd=[01] .*swift=6.3.2 .*events_lost=0",
        "stale pre-V43 SD fallback",
    ):
        assert marker in net_iterate


def test_runtime_v22_docs_are_updated_after_hardware_proof() -> None:
    readme = read_repo("README.md")
    runbook = read_repo("docs/RUNBOOK.md")
    design = read_repo("docs/CONCURRENCY_DESIGN.md")

    for source in (readme, runbook, design):
        assert "Runtime V22 guarded typed pools" in source
        assert "poolcheck ok=1" in source
        assert "bootcert ok=1 version=22" in source

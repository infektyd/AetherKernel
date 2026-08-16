import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]

from tests.commands_contract_helpers import (
    assert_commands_era_in_netboot_sources,
    assert_commands_era_prefix_of_live,
)
from tests.test_runtime_v45_contract import COMMANDS_V45

COMMANDS_V18 = (
    "commands=help,protocol,status,heap,queues,tasks,tasks2,kobjects,drivers,drivercheck,mailboxes,sendtest,"
    "supervisor,health,capcheck,events,runtime,agent,certificate,sched,sched2,sched3,sched4,sched5,sched6,sched7,sched8,sched9,sched10,sched11,sched12,cores,locks,runqueues,diag,irqs,timers,memcheck,faults,retained,"
    "retained-clear,memmap,mmu,pools,poolcheck,heapfrag,poolstats,frames,heapcheck,framecheck,stress,frameprobe,"
    "bootcert,canceltest,taskcheck,channeltest,bootcheck,soak,heap-invalid-free-test,"
    "heap-double-free-test,panic-test,fault-test,reboot"
)


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_support_declares_runtime_v18_cancellation_api() -> None:
    support = read_repo("Sources/Support/include/Support.h")

    for symbol in (
        "KERNEL_CANCEL_TOKEN_CAPACITY",
        "KERNEL_CANCEL_STATE_FREE",
        "KERNEL_CANCEL_STATE_ACTIVE",
        "KERNEL_CANCEL_STATE_CANCELLED",
        "KERNEL_CANCEL_STATE_COMPLETED",
        "KERNEL_CANCEL_ERROR_NONE",
        "KERNEL_CANCEL_ERROR_CAPACITY",
        "KERNEL_CANCEL_ERROR_BAD_TOKEN",
        "kernel_cancel_init",
        "kernel_cancel_token_capacity",
        "kernel_cancel_token_count",
        "kernel_cancel_requested_count",
        "kernel_cancel_completed_count",
        "kernel_cancel_last_error",
        "kernel_cancel_create",
        "kernel_cancel_request",
        "kernel_cancel_is_requested",
        "kernel_cancel_complete",
        "kernel_cancel_state",
        "kernel_cancel_owner_task",
        "kernel_cancel_selftest",
    ):
        assert symbol in support


def test_runtime_v18_cancellation_source_is_fixed_capacity_without_heap() -> None:
    source = read_repo("Sources/Support/kernel_cancel.c")

    for marker in (
        "KERNEL_CANCEL_TOKEN_CAPACITY_VALUE",
        "cancel_token_record",
        "static cancel_token_record tokens",
        "generation",
        "owner_task_id",
        "requested_count",
        "completed_count",
        "kernel_cancel_selftest",
        "irq_save",
    ):
        assert marker in source

    forbidden_hot_path = (
        "malloc(",
        "calloc(",
        "realloc(",
        "free(",
        "posix_memalign(",
    )
    for marker in forbidden_hot_path:
        assert marker not in source


def test_runtime_v18_boot_marker_and_cancel_init_are_wired() -> None:
    app = read_repo("Sources/Application/Application.swift")

    assert "kernel_cancel_init()" in app
    assert "runtime v18: cooperative cancellation tokens" in app
    assert "kernel_event_emit(KERNEL_EVENT_KIND_BOOT, 44, 0, 0)" in app
    assert "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 18" in app


def test_uart_shell_v18_canceltest_command_and_response_exist() -> None:
    shell = read_repo("Sources/Application/UARTShell.swift")
    assert_commands_era_prefix_of_live(COMMANDS_V18, label="V18")


    for marker in (
        "func printCanceltest()",
        "canceltest ok=",
        " capacity=",
        " active=",
        " requested=",
        " completed=",
        " last_error=",
        " fast=",
        " slow=",
        " long=",
        'shellBufferSliceEquals(commandStart, commandLen, "canceltest")',
        "kernel_cancel_selftest()",
    ):
        assert marker in shell


def test_runtime_v18_bootcert_includes_cancellation_health() -> None:
    shell = read_repo("Sources/Application/UARTShell.swift")
    assert_commands_era_prefix_of_live(COMMANDS_V18, label="V18")
    net_iterate = read_repo("scripts/netboot/net-iterate.sh")

    assert "func printBootcert()" in shell
    assert " version=40" in shell
    assert " cancellations=" in shell
    assert "^bootcert ok=1 version=44 .*concurrency=1 .*priority=1 .*fairness=1 .*stealing=1 .*backpressure=1 .*handoff=1 .*wake=1 .*job_exec=1 .*worker_feed=1 .*secondary_workers=1 .*preemptive=1 .*smp_scheduler=1 .*atomics=1 .*locks=1 .*queues=1 .*smp=1 .*scheduler=1 .*certificate=1 .*agent=1 .*runtime=1 .*events_lost=0" in net_iterate


def test_runtime_v18_netboot_gates_and_shell_probe_exist() -> None:
    net_iterate = read_repo("scripts/netboot/net-iterate.sh")
    doctor = read_repo("scripts/netboot/netboot-doctor.sh")

    for source in (net_iterate, doctor):
        assert "runtime v18: cooperative cancellation tokens" in source
        assert_commands_era_in_netboot_sources(COMMANDS_V18, COMMANDS_V45, source, label="V18")

    for marker in (
        "probe shell: canceltest",
        "^canceltest ok=1 .*completed=1",
        "stale pre-V43 SD fallback",
    ):
        assert marker in net_iterate

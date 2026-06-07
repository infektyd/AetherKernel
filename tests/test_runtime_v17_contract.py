import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]

COMMANDS_V17 = (
    "commands=help,protocol,status,heap,queues,tasks,tasks2,kobjects,drivers,drivercheck,mailboxes,sendtest,"
    "supervisor,health,capcheck,events,runtime,agent,certificate,sched,sched2,sched3,sched4,sched5,sched6,sched7,sched8,sched9,sched10,cores,locks,runqueues,diag,irqs,timers,memcheck,faults,retained,"
    "retained-clear,memmap,mmu,pools,poolcheck,heapfrag,poolstats,frames,heapcheck,framecheck,stress,frameprobe,"
    "bootcert,canceltest,taskcheck,channeltest,bootcheck,soak,heap-invalid-free-test,heap-double-free-test,"
    "panic-test,fault-test,reboot"
)


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_runtime_v17_boot_marker_exists() -> None:
    app = read_repo("Sources/Application/Application.swift")

    assert "runtime v17: deterministic boot certificate" in app
    assert "kernel_event_emit(KERNEL_EVENT_KIND_BOOT, 42, 0, 0)" in app


def test_uart_shell_v17_bootcert_command_and_fields_exist() -> None:
    shell = read_repo("Sources/Application/UARTShell.swift")

    for marker in (
        COMMANDS_V17,
        "func printBootcert()",
        "bootcert ok=",
        " version=40",
        " memmap=",
        " heap=",
        " frames=",
        " retained_valid=",
        " kobjects=",
        " tasks=",
        " mailboxes=",
        " supervisor=",
        " events=",
        " events_lost=",
        " heap_free=",
        " frame_free=",
        " uptime_ms=",
        'shellBufferSliceEquals(commandStart, commandLen, "bootcert")',
    ):
        assert marker in shell


def test_uart_shell_v17_bootcert_aggregates_subsystem_selftests() -> None:
    shell = read_repo("Sources/Application/UARTShell.swift")

    assert "func printBootcert()" in shell

    for marker in (
        "kernel_memory_map_valid()",
        "heap_guard_selftest()",
        "kernel_frame_allocator_selftest()",
        "kernel_retained_valid()",
        "kernel_object_registry_selftest()",
        "kernel_task_registry_selftest()",
        "kernel_mailbox_selftest()",
        "kernel_supervisor_check()",
        "kernel_supervisor_selftest()",
        "kernel_event_log_selftest()",
        "kernel_event_lost_count()",
    ):
        assert marker in shell


def test_runtime_v17_netboot_gates_and_shell_probe_exist() -> None:
    net_iterate = read_repo("net-iterate.sh")
    doctor = read_repo("netboot-doctor.sh")

    for source in (net_iterate, doctor):
        assert "runtime v17: deterministic boot certificate" in source
        assert COMMANDS_V17 in source

    for marker in (
        "probe shell: bootcert",
        "^bootcert ok=1 version=42 .*fairness=1 .*stealing=1 .*backpressure=1 .*handoff=1 .*wake=1 .*job_exec=1 .*worker_feed=1 .*secondary_workers=1 .*preemptive=1 .*smp_scheduler=1 .*atomics=1 .*locks=1 .*queues=1 .*smp=1 .*scheduler=1 .*certificate=1 .*agent=1 .*runtime=1 .*events_lost=0",
        "stale pre-V41 SD fallback",
    ):
        assert marker in net_iterate

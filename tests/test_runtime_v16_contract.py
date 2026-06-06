import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]

COMMANDS_V16 = (
    "commands=help,protocol,status,heap,queues,tasks,tasks2,kobjects,drivers,drivercheck,mailboxes,sendtest,"
    "supervisor,health,capcheck,events,runtime,agent,certificate,sched,sched2,sched3,sched4,sched5,cores,locks,runqueues,diag,irqs,timers,memcheck,faults,retained,"
    "retained-clear,memmap,mmu,pools,poolcheck,heapfrag,poolstats,frames,heapcheck,framecheck,stress,frameprobe,"
    "bootcert,canceltest,taskcheck,channeltest,bootcheck,soak,heap-invalid-free-test,heap-double-free-test,panic-test,"
    "fault-test,reboot"
)


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_support_declares_runtime_v16_event_log_api() -> None:
    support = read_repo("Sources/Support/include/Support.h")

    for symbol in (
        "KERNEL_EVENT_KIND_BOOT",
        "KERNEL_EVENT_KIND_TASK",
        "KERNEL_EVENT_KIND_TIMER",
        "KERNEL_EVENT_KIND_MAILBOX",
        "KERNEL_EVENT_KIND_SUPERVISOR",
        "KERNEL_EVENT_KIND_SHELL",
        "KERNEL_EVENT_KIND_HANDLE",
        "KERNEL_EVENT_KIND_SELFTEST",
        "kernel_event_log_init",
        "kernel_event_emit",
        "kernel_event_capacity",
        "kernel_event_count",
        "kernel_event_lost_count",
        "kernel_event_sequence",
        "kernel_event_kind",
        "kernel_event_ticks",
        "kernel_event_seq",
        "kernel_event_arg0",
        "kernel_event_arg1",
        "kernel_event_arg2",
        "kernel_event_log_selftest",
    ):
        assert symbol in support


def test_runtime_v16_event_log_ring_source_exists() -> None:
    source = read_repo("Sources/Support/kernel_event_log.c")

    for marker in (
        "KERNEL_EVENT_CAPACITY_VALUE",
        "event_record",
        "write_index",
        "lost_count",
        "kernel_event_emit",
        "kernel_event_log_selftest",
        "irq_save",
    ):
        assert marker in source


def test_runtime_v16_boot_marker_and_event_emits_are_wired() -> None:
    app = read_repo("Sources/Application/Application.swift")

    assert "kernel_event_log_init()" in app
    assert "runtime v16: kernel event log ring" in app

    for marker in (
        "kernel_event_emit(KERNEL_EVENT_KIND_BOOT",
        "kernel_event_emit(KERNEL_EVENT_KIND_TIMER",
        "kernel_event_emit(KERNEL_EVENT_KIND_MAILBOX",
        "kernel_event_emit(KERNEL_EVENT_KIND_SUPERVISOR",
        "kernel_event_emit(KERNEL_EVENT_KIND_HANDLE",
    ):
        assert marker in app


def test_uart_shell_v16_events_command_and_responses_exist() -> None:
    shell = read_repo("Sources/Application/UARTShell.swift")

    for marker in (
        COMMANDS_V16,
        "events count=",
        " capacity=",
        " lost=",
        " sequence=",
        " selftest=",
        " event index=",
        " seq=",
        " kind=",
        " ticks=",
        " a0=",
        " a1=",
        " a2=",
        'shellBufferSliceEquals(commandStart, commandLen, "events")',
    ):
        assert marker in shell


def test_runtime_v16_netboot_gates_and_shell_probe_exist() -> None:
    net_iterate = read_repo("net-iterate.sh")
    doctor = read_repo("netboot-doctor.sh")

    for source in (net_iterate, doctor):
        assert "runtime v16: kernel event log ring" in source
        assert COMMANDS_V16 in source

    for marker in (
        "probe shell: events",
        "^events count=.* lost=0 .*selftest=1",
        "stale pre-V37 SD fallback",
    ):
        assert marker in net_iterate

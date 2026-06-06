import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]

COMMANDS_V20 = (
    "commands=help,status,heap,queues,tasks,tasks2,kobjects,mailboxes,sendtest,"
    "supervisor,health,capcheck,events,diag,irqs,timers,memcheck,faults,retained,"
    "retained-clear,memmap,frames,heapcheck,framecheck,stress,frameprobe,"
    "bootcert,canceltest,taskcheck,channeltest,bootcheck,soak,heap-invalid-free-test,"
    "heap-double-free-test,panic-test,fault-test,reboot"
)


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_aether_channel_swift_wrapper_exists_over_mailboxes() -> None:
    channel = read_repo("Sources/Application/AetherChannel.swift")

    for marker in (
        "struct AetherChannelU64",
        "let mailboxID: UInt32",
        "func send(_ value: UInt64) -> Bool",
        "func receive() async -> UInt64",
        "func tryReceive() -> (Bool, UInt64)",
        "func depth() -> UInt32",
        "kernel_mailbox_send_u64(mailboxID",
        "kernel_mailbox_recv_u64(mailboxID",
        "timerSleepMillis(25)",
        "aetherChannelSelftest()",
    ):
        assert marker in channel


def test_runtime_v20_demo_mailbox_tasks_use_channel_wrapper() -> None:
    app = read_repo("Sources/Application/Application.swift")

    for marker in (
        "let channel = AetherChannelU64(mailboxID: MAILBOX_DEMO_ID)",
        "channel.send(n)",
        "await channel.receive()",
        "runtime v20: bounded async channels",
        "kernel_event_emit(KERNEL_EVENT_KIND_BOOT, 20, 0, 0)",
        "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 20",
    ):
        assert marker in app

    assert "kernel_mailbox_send_u64(MAILBOX_DEMO_ID" not in app
    assert "await mailboxReceiveU64(MAILBOX_DEMO_ID)" not in app


def test_uart_shell_v20_channeltest_command_and_response_exist() -> None:
    shell = read_repo("Sources/Application/UARTShell.swift")

    for marker in (
        COMMANDS_V20,
        "func printChanneltest()",
        "channeltest ok=",
        " mailbox=",
        " sent=",
        " received=",
        " value=",
        " depth=",
        " selftest=",
        "aetherChannelSelftest()",
        'shellBufferEquals("channeltest")',
    ):
        assert marker in shell


def test_runtime_v20_bootcert_includes_channel_health() -> None:
    shell = read_repo("Sources/Application/UARTShell.swift")
    net_iterate = read_repo("net-iterate.sh")

    assert " version=20" in shell
    assert " channels=" in shell
    assert "^bootcert ok=1 version=20 .*channels=1 .*taskspawns=1 .*cancellations=1 .*events_lost=0" in net_iterate


def test_runtime_v20_netboot_gates_and_shell_probe_exist() -> None:
    net_iterate = read_repo("net-iterate.sh")
    doctor = read_repo("netboot-doctor.sh")

    for source in (net_iterate, doctor):
        assert "runtime v20: bounded async channels" in source
        assert COMMANDS_V20 in source

    for marker in (
        "probe shell: channeltest",
        "^channeltest ok=1 .*received=1",
        "stale pre-V20 SD fallback",
    ):
        assert marker in net_iterate

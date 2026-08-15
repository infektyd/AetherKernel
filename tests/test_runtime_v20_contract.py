import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]

from tests.commands_contract_helpers import (
    assert_commands_era_in_netboot_sources,
    assert_commands_era_prefix_of_live,
)
from tests.test_runtime_v45_contract import COMMANDS_V45

COMMANDS_V20 = (
    "commands=help,protocol,status,heap,queues,tasks,tasks2,kobjects,drivers,drivercheck,mailboxes,sendtest,"
    "supervisor,health,capcheck,events,runtime,agent,certificate,sched,sched2,sched3,sched4,sched5,sched6,sched7,sched8,sched9,sched10,sched11,sched12,cores,locks,runqueues,diag,irqs,timers,memcheck,faults,retained,"
    "retained-clear,memmap,mmu,pools,poolcheck,heapfrag,poolstats,frames,heapcheck,framecheck,stress,frameprobe,"
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
        "kernel_event_emit(KERNEL_EVENT_KIND_BOOT, 64, 0, 0)",
        "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 20",
    ):
        assert marker in app

    assert "kernel_mailbox_send_u64(MAILBOX_DEMO_ID" not in app
    assert "await mailboxReceiveU64(MAILBOX_DEMO_ID)" not in app


def test_uart_shell_v20_channeltest_command_and_response_exist() -> None:
    shell = read_repo("Sources/Application/UARTShell.swift")
    assert_commands_era_prefix_of_live(COMMANDS_V20, label="V20")


    for marker in (
        "func printChanneltest()",
        "channeltest ok=",
        " mailbox=",
        " sent=",
        " received=",
        " value=",
        " depth=",
        " selftest=",
        "aetherChannelSelftest()",
        'shellBufferSliceEquals(commandStart, commandLen, "channeltest")',
    ):
        assert marker in shell


def test_runtime_v20_bootcert_includes_channel_health() -> None:
    shell = read_repo("Sources/Application/UARTShell.swift")
    assert_commands_era_prefix_of_live(COMMANDS_V20, label="V20")
    net_iterate = read_repo("scripts/netboot/net-iterate.sh")

    assert " version=40" in shell
    assert " channels=" in shell
    assert "^bootcert ok=1 version=66 .*concurrency=1 .*priority=1 .*fairness=1 .*stealing=[01] .*backpressure=[01] .*handoff=[01] .*wake=[01] .*job_exec=[01] .*worker_feed=[01] .*secondary_workers=[01] .*preemptive=[01] .*smp_scheduler=[01] .*atomics=1 .*locks=1 .*queues=1 .*smp=1 .*scheduler=1 .*certificate=1 .*agent=1 .*runtime=1 .*syscall=1 .*uaccess=1 .*usermode=1 .*process=1 .*loader=1 .*multiprocess=1 .*sdhci=1 .*card=1 .*block=1 .*fat32=1 .*mailbox=1 .*framebuf=1 .*console=1 .*pcie=1 .*vl805=1 .*xhci=1 .*kbd=[01] .*events_lost=0" in net_iterate


def test_runtime_v20_netboot_gates_and_shell_probe_exist() -> None:
    net_iterate = read_repo("scripts/netboot/net-iterate.sh")
    doctor = read_repo("scripts/netboot/netboot-doctor.sh")

    for source in (net_iterate, doctor):
        assert "runtime v20: bounded async channels" in source
        assert_commands_era_in_netboot_sources(COMMANDS_V20, COMMANDS_V45, source, label="V20")

    for marker in (
        "probe shell: channeltest",
        "^channeltest ok=1 .*received=1",
        "stale pre-V43 SD fallback",
    ):
        assert marker in net_iterate

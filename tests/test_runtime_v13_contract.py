import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]

COMMANDS_V13 = (
    "commands=help,protocol,status,heap,queues,tasks,tasks2,kobjects,drivers,drivercheck,mailboxes,sendtest,"
    "supervisor,health,capcheck,events,runtime,agent,certificate,sched,sched2,sched3,sched4,sched5,sched6,sched7,sched8,sched9,sched10,sched11,sched12,cores,locks,runqueues,diag,irqs,timers,memcheck,faults,retained,"
    "retained-clear,memmap,mmu,pools,poolcheck,heapfrag,poolstats,frames,heapcheck,framecheck,stress,frameprobe,"
    "bootcert,canceltest,taskcheck,channeltest,bootcheck,soak,heap-invalid-free-test,heap-double-free-test,panic-test,"
    "fault-test,reboot"
)


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_support_declares_runtime_v13_mailbox_api() -> None:
    support = read_repo("Sources/Support/include/Support.h")

    for symbol in (
        "KERNEL_OBJECT_KIND_MAILBOX",
        "KERNEL_MAILBOX_ERROR_NONE",
        "KERNEL_MAILBOX_ERROR_FULL",
        "KERNEL_MAILBOX_ERROR_EMPTY",
        "KERNEL_MAILBOX_ERROR_BAD_ID",
        "kernel_mailbox_registry_init",
        "kernel_mailbox_register",
        "kernel_mailbox_count",
        "kernel_mailbox_capacity",
        "kernel_mailbox_queue_capacity",
        "kernel_mailbox_object_id",
        "kernel_mailbox_depth",
        "kernel_mailbox_sent_count",
        "kernel_mailbox_received_count",
        "kernel_mailbox_drop_count",
        "kernel_mailbox_last_error",
        "kernel_mailbox_send_u64",
        "kernel_mailbox_recv_u64",
        "kernel_mailbox_clear",
        "kernel_mailbox_name_len",
        "kernel_mailbox_name_byte",
        "kernel_mailbox_selftest",
    ):
        assert symbol in support


def test_runtime_v13_boot_marker_and_mailbox_init_exist() -> None:
    app = read_repo("Sources/Application/Application.swift")

    assert "runtime v13: bounded mailbox message queues" in app
    assert "kernel_mailbox_registry_init()" in app
    assert "registerRuntimeMailboxes()" in app
    assert "registerRuntimeTasks()" in app
    main_body = app[app.index("static func main()"):]
    assert main_body.index("kernel_task_registry_init()") < main_body.index("kernel_mailbox_registry_init()")
    assert main_body.index("kernel_mailbox_registry_init()") < main_body.index("registerRuntimeMailboxes()")
    assert main_body.index("registerRuntimeMailboxes()") < main_body.index("registerRuntimeTasks()")


def test_runtime_v13_demo_mailbox_tasks_exist() -> None:
    app = read_repo("Sources/Application/Application.swift")

    for marker in (
        "MAILBOX_DEMO_ID",
        "MAILBOX_SELFTEST_ID",
        "TASK_MAIL_TX_ID",
        "TASK_MAIL_RX_ID",
        "runtimeMailboxSent",
        "runtimeMailboxReceived",
        "mailboxProducer",
        "mailboxConsumer",
        "AetherChannelU64(mailboxID: MAILBOX_DEMO_ID)",
        "channel.send(n)",
        "await channel.receive()",
        "rtv13 mail tx ",
        "rtv13 mail rx ",
        "spawnAetherTask(TASK_MAIL_TX_ID",
        "spawnAetherTask(TASK_MAIL_RX_ID",
    ):
        assert marker in app


def test_uart_shell_v13_mailbox_commands_and_responses_exist() -> None:
    shell = read_repo("Sources/Application/UARTShell.swift")

    for marker in (
        COMMANDS_V13,
        "mailboxes count=",
        " queue_capacity=",
        " mailbox index=",
        " object=",
        " depth=",
        " sent=",
        " received=",
        " drops=",
        " last_error=",
        "sendtest ok=",
        " mailbox=",
        " value=",
        'shellBufferSliceEquals(commandStart, commandLen, "mailboxes")',
        'shellBufferSliceEquals(commandStart, commandLen, "sendtest")',
    ):
        assert marker in shell


def test_runtime_v13_netboot_gates_and_shell_probes_exist() -> None:
    net_iterate = read_repo("scripts/netboot/net-iterate.sh")
    doctor = read_repo("scripts/netboot/netboot-doctor.sh")

    for source in (net_iterate, doctor):
        assert "runtime v13: bounded mailbox message queues" in source
        assert COMMANDS_V13 in source

    for marker in (
        "rtv13 mail tx 0x0000000000000000",
        "rtv13 mail rx 0x0000000000000000",
        "probe shell: mailboxes",
        "probe shell: sendtest",
        "^mailboxes count=.* queue_capacity=",
        "^sendtest ok=1 .*received=1",
    ):
        assert marker in net_iterate

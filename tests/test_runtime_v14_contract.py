import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]

COMMANDS_V14 = (
    "commands=help,status,heap,queues,tasks,tasks2,kobjects,mailboxes,sendtest,"
    "supervisor,health,capcheck,events,diag,irqs,timers,memcheck,faults,retained,"
    "retained-clear,memmap,frames,heapcheck,framecheck,stress,frameprobe,"
    "bootcert,canceltest,taskcheck,bootcheck,soak,heap-invalid-free-test,heap-double-free-test,panic-test,"
    "fault-test,reboot"
)


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_support_declares_runtime_v14_supervisor_api() -> None:
    support = read_repo("Sources/Support/include/Support.h")

    for symbol in (
        "KERNEL_SUPERVISOR_POLICY_OBSERVE",
        "KERNEL_SUPERVISOR_POLICY_PANIC",
        "KERNEL_SUPERVISOR_STATE_HEALTHY",
        "KERNEL_SUPERVISOR_STATE_MISSED",
        "kernel_supervisor_init",
        "kernel_supervisor_register_task",
        "kernel_supervisor_heartbeat",
        "kernel_supervisor_check",
        "kernel_supervisor_count",
        "kernel_supervisor_capacity",
        "kernel_supervisor_unhealthy_count",
        "kernel_supervisor_total_missed_count",
        "kernel_supervisor_now_ms",
        "kernel_supervisor_task_id",
        "kernel_supervisor_deadline_ms",
        "kernel_supervisor_last_heartbeat_ms",
        "kernel_supervisor_missed_count",
        "kernel_supervisor_state",
        "kernel_supervisor_policy",
        "kernel_supervisor_selftest",
    ):
        assert symbol in support


def test_runtime_v14_boot_marker_and_supervisor_init_exist() -> None:
    app = read_repo("Sources/Application/Application.swift")

    assert "runtime v14: deterministic task supervisor" in app
    assert "kernel_supervisor_init()" in app
    assert "registerRuntimeSupervisor()" in app

    main_body = app[app.index("static func main()"):]
    assert main_body.index("kernel_task_registry_init()") < main_body.index("kernel_supervisor_init()")
    assert main_body.index("registerRuntimeTasks()") < main_body.index("registerRuntimeSupervisor()")
    assert main_body.index("registerRuntimeSupervisor()") < main_body.index("uart_rx_irq_init()")


def test_runtime_v14_demo_tasks_send_supervisor_heartbeats() -> None:
    app = read_repo("Sources/Application/Application.swift")

    for marker in (
        "registerAetherTask(TASK_FAST_ID",
        "registerAetherTask(TASK_SLOW_ID",
        "registerAetherTask(TASK_LONG_ID",
        "registerAetherTask(TASK_MAIL_TX_ID",
        "registerAetherTask(TASK_MAIL_RX_ID",
        "kernel_supervisor_heartbeat(TASK_FAST_ID)",
        "kernel_supervisor_heartbeat(TASK_SLOW_ID)",
        "kernel_supervisor_heartbeat(TASK_LONG_ID)",
        "kernel_supervisor_heartbeat(TASK_MAIL_TX_ID)",
        "kernel_supervisor_heartbeat(TASK_MAIL_RX_ID)",
        "kernel_supervisor_check()",
    ):
        assert marker in app


def test_uart_shell_v14_supervisor_commands_and_responses_exist() -> None:
    shell = read_repo("Sources/Application/UARTShell.swift")

    for marker in (
        COMMANDS_V14,
        "supervisor count=",
        " unhealthy=",
        " total_missed=",
        " supervise index=",
        " task=",
        " policy=",
        " deadline_ms=",
        " last_ms=",
        " missed=",
        " state=",
        "health ok=",
        " supervised=",
        " uptime_ms=",
        'shellBufferEquals("supervisor")',
        'shellBufferEquals("health")',
    ):
        assert marker in shell


def test_runtime_v14_netboot_gates_and_shell_probes_exist() -> None:
    net_iterate = read_repo("net-iterate.sh")
    doctor = read_repo("netboot-doctor.sh")

    for source in (net_iterate, doctor):
        assert "runtime v14: deterministic task supervisor" in source
        assert COMMANDS_V14 in source

    for marker in (
        "probe shell: supervisor",
        "probe shell: health",
        "^supervisor count=.* unhealthy=0",
        "^health ok=1 .*supervised=",
    ):
        assert marker in net_iterate

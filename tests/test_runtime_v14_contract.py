import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]

from tests.commands_contract_helpers import (
    assert_commands_era_in_netboot_sources,
    assert_commands_era_prefix_of_live,
)
from tests.test_runtime_v45_contract import COMMANDS_V45

COMMANDS_V14 = (
    "commands=help,protocol,status,heap,queues,tasks,tasks2,kobjects,drivers,drivercheck,mailboxes,sendtest,"
    "supervisor,health,capcheck,events,runtime,agent,certificate,sched,sched2,sched3,sched4,sched5,sched6,sched7,sched8,sched9,sched10,sched11,sched12,cores,locks,runqueues,diag,irqs,timers,memcheck,faults,retained,"
    "retained-clear,memmap,mmu,pools,poolcheck,heapfrag,poolstats,frames,heapcheck,framecheck,stress,frameprobe,"
    "bootcert,canceltest,taskcheck,channeltest,bootcheck,soak,heap-invalid-free-test,heap-double-free-test,panic-test,"
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
    assert_commands_era_prefix_of_live(COMMANDS_V14, label="V14")


    for marker in (
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
        'shellBufferSliceEquals(commandStart, commandLen, "supervisor")',
        'shellBufferSliceEquals(commandStart, commandLen, "health")',
    ):
        assert marker in shell


def test_runtime_v14_netboot_gates_and_shell_probes_exist() -> None:
    net_iterate = read_repo("scripts/netboot/net-iterate.sh")
    doctor = read_repo("scripts/netboot/netboot-doctor.sh")

    for source in (net_iterate, doctor):
        assert "runtime v14: deterministic task supervisor" in source
        assert_commands_era_in_netboot_sources(COMMANDS_V14, COMMANDS_V45, source, label="V14")

    for marker in (
        "probe shell: supervisor",
        "probe shell: health",
        "^supervisor count=.* unhealthy=0",
        "^health ok=1 .*supervised=",
    ):
        assert marker in net_iterate

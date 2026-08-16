import os
import pathlib
import subprocess


ROOT = pathlib.Path(__file__).resolve().parents[1]

from tests.commands_contract_helpers import assert_commands_era_in_netboot_sources
from tests.test_runtime_v45_contract import COMMANDS_V45


COMMANDS_V3 = (
    "commands=help,protocol,status,heap,queues,tasks,tasks2,kobjects,drivers,drivercheck,"
    "mailboxes,sendtest,supervisor,health,handlecheck,capcheck,events,runtime,agent,certificate,sched,sched2,sched3,sched4,sched5,sched6,sched7,sched8,sched9,sched10,sched11,sched12,cores,locks,runqueues,diag,irqs,timers,"
    "memcheck,faults,retained,retained-clear,memmap,mmu,pools,poolcheck,heapfrag,poolstats,frames,heapcheck,framecheck,stress,frameprobe,bootcert,canceltest,taskcheck,channeltest,bootcheck,soak,heap-invalid-free-test,heap-double-free-test,panic-test,fault-test,reboot"
)


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def run_script(name: str, *args: str, env: dict[str, str] | None = None) -> subprocess.CompletedProcess[str]:
    script_env = os.environ.copy()
    if env:
        script_env.update(env)
    return subprocess.run(
        [str(ROOT / name), *args],
        cwd=ROOT,
        env=script_env,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=True,
    )


def test_support_declares_runtime_v3_stats_api() -> None:
    support = read_repo("Sources/Support/include/Support.h")

    for symbol in (
        "heap_total_bytes",
        "heap_free_bytes",
        "heap_largest_free_bytes",
        "heap_malloc_count",
        "heap_free_count",
        "heap_realloc_count",
        "heap_calloc_count",
        "executor_ready_count",
        "executor_ready_capacity",
        "executor_delayed_count",
        "executor_delayed_capacity",
        "kernel_timer_active_mask",
    ):
        assert symbol in support


def test_uart_shell_declares_commands_and_machine_checkable_responses() -> None:
    shell = read_repo("Sources/Application/UARTShell.swift")

    for marker in (
        "let SHELL_COMMAND_LIST =",
        'uartPuts("shell ready \\(SHELL_COMMAND_LIST)\\n")',
        'uartPuts("shell help \\(SHELL_COMMAND_LIST)\\n")',
        ",fat32,mailbox,framebuf,console,pcie,vl805,xhci",
        "status uptime_ms=",
        "heap total=",
        "queues ready=",
        "task fast count=",
        "task slow count=",
        "task long count=",
        "shell reboot reason=command",
        "shell error reason=unknown command=",
        "shell error reason=line_too_long",
    ):
        assert marker in shell

    assert "let UART_SHELL_BUFFER_CAPACITY: Int = 80" in shell
    assert "func startUartShellTask()" in shell
    assert "func processUartShellByte(_ b: UInt8)" in shell


def test_runtime_v3_keeps_single_byte_reset_compatibility() -> None:
    shell = read_repo("Sources/Application/UARTShell.swift")
    reset = read_repo("scripts/serial/serial-reset.sh")

    assert "isResetAlias" in shell
    assert "b == 0x72 || b == 0x52" in shell
    assert 'PAYLOAD="${AETHER_SERIAL_RESET_PAYLOAD:-r}"' in reset


def test_timer_sleep_exposes_pending_stats() -> None:
    timer_sleep = read_repo("Sources/Application/TimerSleep.swift")

    assert "func timerSleepPendingCount() -> UInt" in timer_sleep
    assert "func timerSleepCapacity() -> UInt" in timer_sleep


def test_application_starts_shell_and_exposes_cadence_counters() -> None:
    app = read_repo("Sources/Application/Application.swift")

    for symbol in (
        "runtimeFastCount",
        "runtimeSlowCount",
        "runtimeLongCount",
        "spawnAetherTask(TASK_SHELL_ID",
    ):
        assert symbol in app


def test_net_iterate_requires_runtime_v3_shell_ready_after_hardware_proof() -> None:
    net_iterate = read_repo("scripts/netboot/net-iterate.sh")

    assert_commands_era_in_netboot_sources(COMMANDS_V3, COMMANDS_V45, net_iterate, label="V3")


def test_serial_command_dry_run_appends_newline_and_targets_default_port() -> None:
    result = run_script("scripts/serial/serial-command.sh", "status", env={"AETHER_SERIAL_COMMAND_DRY_RUN": "1"})

    assert "/dev/cu.usbserial-B0044J1V" in result.stdout
    assert "command: status" in result.stdout
    assert "payload: status\\n" in result.stdout

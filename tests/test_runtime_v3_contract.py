import os
import pathlib
import subprocess


ROOT = pathlib.Path(__file__).resolve().parents[1]


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
        "shell ready commands=help,status,heap,queues,tasks,diag,irqs,timers,memcheck,faults,panic-test,fault-test,reboot",
        "shell help commands=help,status,heap,queues,tasks,diag,irqs,timers,memcheck,faults,panic-test,fault-test,reboot",
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
    reset = read_repo("serial-reset.sh")

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
        "startUartShellTask()",
    ):
        assert symbol in app


def test_net_iterate_requires_runtime_v3_shell_ready_after_hardware_proof() -> None:
    net_iterate = read_repo("net-iterate.sh")

    assert "shell ready commands=help,status,heap,queues,tasks,diag,irqs,timers,memcheck,faults,panic-test,fault-test,reboot" in net_iterate


def test_serial_command_dry_run_appends_newline_and_targets_default_port() -> None:
    result = run_script("serial-command.sh", "status", env={"AETHER_SERIAL_COMMAND_DRY_RUN": "1"})

    assert "/dev/cu.usbserial-B0044J1V" in result.stdout
    assert "command: status" in result.stdout
    assert "payload: status\\n" in result.stdout

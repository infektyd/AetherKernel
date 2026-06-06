import os
import pathlib
import subprocess


ROOT = pathlib.Path(__file__).resolve().parents[1]


COMMANDS_V11 = (
    "commands=help,status,heap,queues,tasks,tasks2,kobjects,mailboxes,sendtest,diag,irqs,timers,memcheck,"
    "faults,retained,retained-clear,memmap,frames,heapcheck,framecheck,"
    "stress,frameprobe,bootcheck,soak,heap-invalid-free-test,"
    "heap-double-free-test,panic-test,fault-test,reboot"
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


def test_uart_shell_v11_bootcheck_and_soak_commands_exist() -> None:
    shell = read_repo("Sources/Application/UARTShell.swift")

    for marker in (
        COMMANDS_V11,
        "bootcheck ok=",
        " memmap=",
        " heap=",
        " frames=",
        " retained_valid=",
        "soak ok=",
        " rounds=",
        " failures=",
        " heap_leak=",
        " frame_leak=",
        "let SOAK_ROUNDS: UInt32 = 3",
        'shellBufferEquals("bootcheck")',
        'shellBufferEquals("soak")',
    ):
        assert marker in shell


def test_runtime_v11_boot_marker_and_startup_bootcheck_exist() -> None:
    app = read_repo("Sources/Application/Application.swift")

    assert "runtime v11: boot and soak invariants" in app
    assert "printBootcheck()" in app
    assert app.index("runtime v11: boot and soak invariants") < app.index("printBootcheck()")
    assert app.index("printBootcheck()") < app.index("Task { await fastHeartbeat() }")


def test_serial_probe_script_sends_command_and_waits_for_expected_line() -> None:
    probe = ROOT / "serial-probe.sh"
    assert probe.exists()
    source = probe.read_text()

    for marker in (
        "AETHER_SERIAL_PROBE_DRY_RUN",
        "AETHER_SERIAL_PROBE_TIMEOUT",
        "./serial-command.sh",
        "grep -a -E",
        "matched:",
        "expected regex:",
    ):
        assert marker in source

    result = run_script(
        "serial-probe.sh",
        "bootcheck",
        "^bootcheck ok=1",
        "/dev/cu.test",
        env={
            "AETHER_SERIAL_PROBE_DRY_RUN": "1",
            "AETHER_SERIAL_LOG": "/tmp/aether-test.log",
        },
    )

    assert "serial port: /dev/cu.test" in result.stdout
    assert "command: bootcheck" in result.stdout
    assert "expected regex: ^bootcheck ok=1" in result.stdout
    assert "serial log: /tmp/aether-test.log" in result.stdout


def test_net_iterate_v11_probes_shell_responses_after_boot() -> None:
    net_iterate = read_repo("net-iterate.sh")

    for marker in (
        "AETHER_NETITERATE_SKIP_SHELL_PROBES",
        "./serial-probe.sh",
        "probe shell: status",
        "probe shell: bootcheck",
        "probe shell: stress",
        "probe shell: soak",
        "^status uptime_ms=.*timer_mask=",
        "^bootcheck ok=1 .*frame_free=",
        "^stress ok=1",
        "^soak ok=1",
    ):
        assert marker in net_iterate


def test_runtime_v11_netboot_gates_exist() -> None:
    net_iterate = read_repo("net-iterate.sh")
    doctor = read_repo("netboot-doctor.sh")

    for source in (net_iterate, doctor):
        assert "runtime v11: boot and soak invariants" in source
        assert COMMANDS_V11 in source

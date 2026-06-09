import os
import pathlib
import subprocess


ROOT = pathlib.Path(__file__).resolve().parents[1]

COMMANDS_V25 = (
    "commands=help,protocol,status,heap,queues,tasks,tasks2,kobjects,drivers,drivercheck,"
    "mailboxes,sendtest,supervisor,health,capcheck,events,runtime,agent,certificate,sched,sched2,sched3,sched4,sched5,sched6,sched7,sched8,sched9,sched10,sched11,sched12,cores,locks,runqueues,diag,irqs,timers,"
    "memcheck,faults,retained,retained-clear,memmap,mmu,pools,poolcheck,"
    "heapfrag,poolstats,frames,heapcheck,framecheck,stress,frameprobe,bootcert,"
    "canceltest,taskcheck,channeltest,bootcheck,soak,heap-invalid-free-test,"
    "heap-double-free-test,panic-test,fault-test,reboot"
)


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def run_script(script: str, *args: str, env: dict[str, str] | None = None) -> subprocess.CompletedProcess[str]:
    merged = os.environ.copy()
    if env:
        merged.update(env)
    return subprocess.run(
        [str(ROOT / script), *args],
        cwd=ROOT,
        env=merged,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=True,
    )


def test_runtime_v25_application_marker_and_bootcert_protocol_field_exist() -> None:
    app = read_repo("Sources/Application/Application.swift")
    shell = read_repo("Sources/Application/UARTShell.swift")

    for marker in (
        "Runtime V25 adds a scriptable ASCII command protocol v2.",
        "kernel_event_emit(KERNEL_EVENT_KIND_BOOT, 44, 0, 0)",
        "runtime v25: scriptable command protocol v2",
        "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 25, 2, 1)",
    ):
        assert marker in app

    for marker in (
        " version=40",
        " protocol=1",
        "let protocolV2 = UInt32(1)",
        "protocolV2 != 0",
    ):
        assert marker in shell


def test_runtime_v25_shell_protocol_request_surface_exists() -> None:
    shell = read_repo("Sources/Application/UARTShell.swift")

    for marker in (
        "V25 adds a scriptable request/response envelope",
        COMMANDS_V25,
        "func printProtocol()",
        "protocol version=2 request=req id_field=id cmd_field=cmd begin_end=1 errors=1 max_line=80",
        "func shellBufferSliceEquals",
        "func shellBufferHasPrefix",
        "func parseProtocolRequest",
        "func processProtocolRequest",
        "req id=",
        " cmd=",
        "resp id=",
        " begin",
        " ok=1",
        " end",
        " error=bad_request",
        " error=unknown",
        "shell error reason=unknown command=",
        'shellBufferSliceEquals(commandStart, commandLen, "protocol")',
    ):
        assert marker in shell


def test_runtime_v25_host_tool_can_wrap_request_id_in_dry_run() -> None:
    result = run_script(
        "scripts/serial/serial-command.sh",
        "--request-id",
        "42",
        "status",
        "/dev/cu.test",
        env={"AETHER_SERIAL_COMMAND_DRY_RUN": "1"},
    )

    assert "serial port: /dev/cu.test" in result.stdout
    assert "command: status" in result.stdout
    assert "request id: 42" in result.stdout
    assert "payload: req id=42 cmd=status\\n" in result.stdout


def test_runtime_v25_netboot_gates_and_probes_exist() -> None:
    net_iterate = read_repo("scripts/netboot/net-iterate.sh")
    doctor = read_repo("scripts/netboot/netboot-doctor.sh")

    for source in (net_iterate, doctor):
        assert "runtime v25: scriptable command protocol v2" in source
        assert COMMANDS_V25 in source

    for marker in (
        "probe shell: protocol",
        "^protocol version=2 .*begin_end=1 .*errors=1",
        "probe shell: req-status",
        "^resp id=25 ok=1 cmd=status end",
        "^bootcert ok=1 version=44 .*concurrency=1 .*priority=1 .*fairness=1 .*stealing=1 .*backpressure=1 .*handoff=1 .*wake=1 .*job_exec=1 .*worker_feed=1 .*secondary_workers=1 .*preemptive=1 .*smp_scheduler=1 .*atomics=1 .*locks=1 .*queues=1 .*smp=1 .*scheduler=1 .*certificate=1 .*agent=1 .*runtime=1 .*events_lost=0",
        "stale pre-V43 SD fallback",
    ):
        assert marker in net_iterate


def test_runtime_v25_docs_are_updated_after_hardware_proof() -> None:
    readme = read_repo("README.md")
    runbook = read_repo("docs/RUNBOOK.md")
    design = read_repo("docs/CONCURRENCY_DESIGN.md")

    for source in (readme, runbook, design):
        assert "Runtime V25 scriptable command protocol v2" in source
        assert "protocol version=2" in source
        assert "resp id=25 ok=1 cmd=status end" in source
        assert "bootcert ok=1 version=29" in source

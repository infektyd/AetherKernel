import os
import pathlib
import subprocess


ROOT = pathlib.Path(__file__).resolve().parents[1]


COMMANDS_V29 = (
    "commands=help,protocol,status,heap,queues,tasks,tasks2,kobjects,drivers,drivercheck,"
    "mailboxes,sendtest,supervisor,health,capcheck,events,runtime,agent,certificate,sched,sched2,sched3,sched4,sched5,sched6,sched7,sched8,sched9,sched10,sched11,cores,locks,runqueues,diag,irqs,timers,"
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


def test_runtime_v29_application_shell_and_bootcert_surface_exist() -> None:
    app = read_repo("Sources/Application/Application.swift")
    shell = read_repo("Sources/Application/UARTShell.swift")

    for marker in (
        "kernel_event_emit(KERNEL_EVENT_KIND_BOOT, 43, 0, 0)",
        "runtime v29: agent-oriented control session",
        "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 29,",
    ):
        assert marker in app

    for marker in (
        COMMANDS_V29,
        "func printAgentSession()",
        "agent ok=",
        " version=29",
        " health=green",
        " bootcert=",
        " runtime=",
        " protocol=2",
        " events_lost=",
        " heap_free=",
        " ready=",
        " delayed=",
        " sleepers=",
        " agent=1",
        'shellBufferSliceEquals(commandStart, commandLen, "agent")',
    ):
        assert marker in shell


def test_runtime_v29_host_agent_session_harness_contract_exists() -> None:
    path = ROOT / "agent-session.sh"
    assert path.exists(), "agent-session.sh missing"
    script = path.read_text()
    result = run_script(
        "agent-session.sh",
        env={
            "AETHER_AGENT_SESSION_DRY_RUN": "1",
            "AETHER_AGENT_SESSION_ID_BASE": "2900",
            "AETHER_SERIAL_PORT": "/dev/cu.test",
        },
    )

    for marker in (
        "Runtime V29 agent control session",
        "AETHER_AGENT_SESSION_DRY_RUN",
        "AETHER_AGENT_SESSION_ID_BASE",
        "cmd=agent",
        "cmd=bootcert",
        "cmd=runtime",
        "cmd=events",
        "cmd=stress",
        "cmd=soak",
        "agent-session ok=",
        "health=green",
        "bootcert=1",
        "runtime=1",
        "events_lost=0",
    ):
        assert marker in script

    assert "Runtime V29 agent control session dry run" in result.stdout
    assert "serial port: /dev/cu.test" in result.stdout
    assert "probe: req id=2901 cmd=agent" in result.stdout
    assert "probe: req id=2902 cmd=bootcert" in result.stdout
    assert "probe: req id=2903 cmd=runtime" in result.stdout


def test_runtime_v29_netboot_gates_and_agent_probe_exist() -> None:
    net_iterate = read_repo("net-iterate.sh")
    doctor = read_repo("netboot-doctor.sh")

    for source in (net_iterate, doctor):
        assert "runtime v29: agent-oriented control session" in source
        assert COMMANDS_V29 in source

    for marker in (
        "probe shell: agent",
        "^agent ok=1 version=29 health=green .*bootcert=1 .*runtime=1 .*protocol=2 .*events_lost=0",
        "probe shell: req-agent",
        "^resp id=29 ok=1 cmd=agent end",
        "^bootcert ok=1 version=43 .*priority=1 .*fairness=1 .*stealing=1 .*backpressure=1 .*handoff=1 .*wake=1 .*job_exec=1 .*worker_feed=1 .*secondary_workers=1 .*preemptive=1 .*smp_scheduler=1 .*atomics=1 .*locks=1 .*queues=1 .*smp=1 .*scheduler=1 .*certificate=1 .*agent=1 .*runtime=1 .*events_lost=0",
        "stale pre-V43 SD fallback",
    ):
        assert marker in net_iterate


def test_runtime_v29_docs_are_updated_after_hardware_proof() -> None:
    readme = read_repo("README.md")
    runbook = read_repo("RUNBOOK.md")
    design = read_repo("CONCURRENCY_DESIGN.md")

    for source in (readme, runbook, design):
        assert "Runtime V29 agent-oriented control session" in source
        assert "bootcert ok=1 version=29" in source
        assert "agent ok=1 version=29 health=green" in source
        assert "agent-session ok=1 version=29 health=green" in source

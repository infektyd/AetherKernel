import os
import pathlib
import subprocess


ROOT = pathlib.Path(__file__).resolve().parents[1]


COMMANDS_V30 = (
    "commands=help,protocol,status,heap,queues,tasks,tasks2,kobjects,drivers,drivercheck,"
    "mailboxes,sendtest,supervisor,health,capcheck,events,runtime,agent,certificate,sched,sched2,sched3,cores,locks,runqueues,diag,irqs,timers,"
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


def test_runtime_v30_application_shell_and_certificate_surface_exist() -> None:
    app = read_repo("Sources/Application/Application.swift")
    shell = read_repo("Sources/Application/UARTShell.swift")

    for marker in (
        "kernel_event_emit(KERNEL_EVENT_KIND_BOOT, 35, 0, 0)",
        "runtime v30: swift-native kernel substrate certificate",
        "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 30,",
    ):
        assert marker in app

    for marker in (
        COMMANDS_V30,
        "func printSubstrateCertificate()",
        "certificate ok=",
        " version=31",
        " substrate=1",
        " bootcert=",
        " agent=1",
        " runtime=",
        " protocol=2",
        " memory=",
        " objects=",
        " tasks=",
        " mailboxes=",
        " supervisor=",
        " handles=",
        " events=",
        " cancellations=",
        " channels=",
        " drivers=",
        " pressure=",
        " pools=",
        " mmu=",
        " swift=6.3.2",
        " events_lost=",
        " heap_free=",
        " frame_free=",
        'shellBufferSliceEquals(commandStart, commandLen, "certificate")',
    ):
        assert marker in shell


def test_runtime_v30_bootcert_reports_certificate_field() -> None:
    shell = read_repo("Sources/Application/UARTShell.swift")

    for marker in (
        "let substrateCertificate = UInt32(1)",
        "version=35",
        " certificate=",
        "substrateCertificate != 0",
        "printSubstrateCertificate()",
    ):
        assert marker in shell


def test_runtime_v30_host_certificate_loop_contract_exists() -> None:
    path = ROOT / "certificate-loop.sh"
    agent_session = read_repo("agent-session.sh")
    assert path.exists(), "certificate-loop.sh missing"
    script = path.read_text()
    result = run_script(
        "certificate-loop.sh",
        env={
            "AETHER_CERTIFICATE_LOOP_DRY_RUN": "1",
            "AETHER_CERTIFICATE_LOOP_CYCLES": "2",
            "AETHER_CERTIFICATE_LOOP_ID_BASE": "3000",
            "AETHER_SERIAL_PORT": "/dev/cu.test",
        },
    )

    for marker in (
        "Runtime V30 substrate certificate loop",
        "AETHER_CERTIFICATE_LOOP_DRY_RUN",
        "AETHER_CERTIFICATE_LOOP_CYCLES",
        "net-iterate.sh",
        "agent-session.sh",
        "runtime-audit.sh",
        "cmd=certificate",
        "certificate-loop ok=",
        "version=35",
        "substrate=1",
        "bootcert=1",
        "agent=1",
        "runtime=1",
        "events_lost=0",
    ):
        assert marker in script

    assert "Runtime V30 substrate certificate loop dry run" in result.stdout
    assert "cycles: 2" in result.stdout
    assert "probe: req id=3001 cmd=certificate" in result.stdout
    assert "probe: runtime-audit .build/release/Application" in result.stdout
    assert "^bootcert ok=1 version=35 .*secondary_workers=1 .*preemptive=1 .*smp_scheduler=1 .*atomics=1 .*locks=1 .*queues=1 .*smp=1 .*scheduler=1 .*certificate=1 .*agent=1 .*runtime=1 .*events_lost=0" in agent_session


def test_runtime_v30_netboot_gates_and_certificate_probe_exist() -> None:
    net_iterate = read_repo("net-iterate.sh")
    doctor = read_repo("netboot-doctor.sh")

    for source in (net_iterate, doctor):
        assert "runtime v30: swift-native kernel substrate certificate" in source
        assert COMMANDS_V30 in source

    for marker in (
        "probe shell: certificate",
        "^certificate ok=1 version=35 substrate=1 .*bootcert=1 .*secondary_workers=1 .*preemptive=1 .*smp_scheduler=1 .*atomics=1 .*locks=1 .*queues=1 .*smp=1 .*scheduler=1 .*agent=1 .*runtime=1 .*memory=1 .*objects=1 .*tasks=1 .*mailboxes=1 .*supervisor=1 .*handles=1 .*events=1 .*cancellations=1 .*channels=1 .*drivers=1 .*pressure=1 .*pools=1 .*mmu=1 .*events_lost=0",
        "probe shell: req-certificate",
        "^resp id=30 ok=1 cmd=certificate end",
        "^bootcert ok=1 version=35 .*secondary_workers=1 .*preemptive=1 .*smp_scheduler=1 .*atomics=1 .*locks=1 .*queues=1 .*smp=1 .*scheduler=1 .*certificate=1 .*agent=1 .*runtime=1 .*events_lost=0",
        "stale pre-V35 SD fallback",
    ):
        assert marker in net_iterate


def test_runtime_v30_docs_are_updated_after_hardware_proof() -> None:
    readme = read_repo("README.md")
    runbook = read_repo("RUNBOOK.md")
    design = read_repo("CONCURRENCY_DESIGN.md")

    for source in (readme, runbook, design):
        assert "Runtime V30 Swift-native kernel substrate certificate" in source
        assert "bootcert ok=1 version=30" in source
        assert "certificate ok=1 version=30 substrate=1" in source
        assert "certificate-loop ok=1 version=30" in source

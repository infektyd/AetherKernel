import os
import pathlib
import subprocess


ROOT = pathlib.Path(__file__).resolve().parents[1]


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


def test_runtime_v26_soak_loop_script_contract_exists() -> None:
    script = read_repo("soak-loop.sh")

    for marker in (
        "Runtime V26 host soak harness",
        "AETHER_SOAK_CYCLES",
        "AETHER_SOAK_LOG",
        "AETHER_SOAK_DRY_RUN",
        "AETHER_NETITERATE_SKIP_SHELL_PROBES",
        "net-iterate.sh",
        "probe_request()",
        "req id=",
        "cmd=bootcert",
        "cmd=stress",
        "cmd=soak",
        "cmd=events",
        "cmd=status",
        "^bootcert ok=1 version=44 .*concurrency=1 .*priority=1 .*fairness=1 .*stealing=1 .*backpressure=1 .*handoff=1 .*wake=1 .*job_exec=1 .*worker_feed=1 .*secondary_workers=1 .*preemptive=1 .*smp_scheduler=1 .*atomics=1 .*locks=1 .*queues=1 .*smp=1 .*scheduler=1 .*certificate=1 .*agent=1 .*events_lost=0",
        "^stress ok=1 .*heap_leak=0 frame_leak=0",
        "^soak ok=1 .*failures=0 .*heap_leak=0 frame_leak=0",
        "^events count=.* lost=0 .*selftest=1",
        "soak summary cycle=",
        "soak result ok=1",
    ):
        assert marker in script


def test_runtime_v26_soak_loop_dry_run_is_scriptable() -> None:
    result = run_script(
        "soak-loop.sh",
        "/tmp/aether-root",
        env={
            "AETHER_SOAK_DRY_RUN": "1",
            "AETHER_SOAK_CYCLES": "2",
            "AETHER_SOAK_LOG": "/tmp/aether-soak-test.log",
            "AETHER_SERIAL_PORT": "/dev/cu.test",
            "AETHER_SERIAL_LOG": "/tmp/aether-serial-test.log",
            "AETHER_TFTP_LOG": "/tmp/aether-tftp-test.log",
        },
    )

    for marker in (
        "cycles: 2",
        "tftp root: /tmp/aether-root",
        "soak log: /tmp/aether-soak-test.log",
        "serial port: /dev/cu.test",
        "serial log: /tmp/aether-serial-test.log",
        "./net-iterate.sh /tmp/aether-root",
        "probe: req id=2601 cmd=status",
        "probe: req id=2602 cmd=bootcert",
        "probe: req id=2603 cmd=stress",
        "probe: req id=2604 cmd=soak",
        "probe: req id=2605 cmd=events",
    ):
        assert marker in result.stdout


def test_runtime_v26_docs_are_updated_after_hardware_proof() -> None:
    readme = read_repo("README.md")
    runbook = read_repo("RUNBOOK.md")
    design = read_repo("CONCURRENCY_DESIGN.md")

    for source in (readme, runbook, design):
        assert "Runtime V26 host soak harness" in source
        assert "soak result ok=1 cycles=" in source
        assert "bootcert ok=1 version=29" in source

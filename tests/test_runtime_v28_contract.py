import os
import pathlib
import subprocess


ROOT = pathlib.Path(__file__).resolve().parents[1]

from tests.commands_contract_helpers import (
    assert_commands_era_in_netboot_sources,
    assert_commands_era_prefix_of_live,
)
from tests.test_runtime_v45_contract import COMMANDS_V45


COMMANDS_V28 = (
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


def test_runtime_v28_support_declares_runtime_audit_surface() -> None:
    support = read_repo("Sources/Support/include/Support.h")
    source = read_repo("Sources/Support/kernel_runtime_audit.c")

    for marker in (
        "KERNEL_RUNTIME_AUDIT_VERSION 28U",
        "KERNEL_RUNTIME_OWNED_HOOK_COUNT 10U",
        "KERNEL_RUNTIME_HEAP_SHIM_COUNT 5U",
        "KERNEL_RUNTIME_AUDIT_REQUIRED_SYMBOL_COUNT",
        "kernel_runtime_audit_version",
        "kernel_runtime_source_hook_count",
        "kernel_runtime_linked_hook_count",
        "kernel_runtime_heap_shim_count",
        "kernel_runtime_linked_heap_shim_count",
        "kernel_runtime_required_symbol_count",
        "kernel_runtime_audit_selftest",
    ):
        assert marker in support
        assert marker in source

    for marker in (
        "swift_task_enqueueGlobalImpl",
        "swift_task_enqueueMainExecutorImpl",
        "swift_task_enqueueGlobalWithDelayImpl",
        "swift_task_enqueueGlobalWithDeadlineImpl",
        "swift_task_asyncMainDrainQueueImpl",
        "swift_task_getMainExecutorImpl",
        "swift_task_isMainExecutorImpl",
        "swift_task_checkIsolatedImpl",
        "swift_task_isIsolatingCurrentContextImpl",
        "swift_task_donateThreadToGlobalExecutorUntilImpl",
        "malloc",
        "free",
        "calloc",
        "realloc",
        "posix_memalign",
    ):
        assert marker in source


def test_runtime_v28_application_shell_and_bootcert_surface_exist() -> None:
    app = read_repo("Sources/Application/Application.swift")
    shell = read_repo("Sources/Application/UARTShell.swift")
    assert_commands_era_prefix_of_live(COMMANDS_V28, label="V28")


    for marker in (
        "kernel_event_emit(KERNEL_EVENT_KIND_BOOT, 64, 0, 0)",
        "runtime v28: swift runtime dependency audit",
        "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 28,",
    ):
        assert marker in app

    for marker in (
        "func printRuntimeAudit()",
        "runtime ok=",
        " version=28",
        " swift=6.3.2",
        " source_hooks=",
        " linked_hooks=",
        " heap_shims=",
        " linked_heap_shims=",
        " required_symbols=",
        " audit=1",
        'shellBufferSliceEquals(commandStart, commandLen, "runtime")',
        "runtimeAudit",
    ):
        assert marker in shell

    assert " runtime=" in shell
    assert "let runtimeAudit = kernel_runtime_audit_selftest()" in shell


def test_runtime_v28_host_nm_audit_tool_contract_exists() -> None:
    script = read_repo("scripts/runtime-audit.sh")
    result = run_script(
        "scripts/runtime-audit.sh",
        "/tmp/kernel.macho",
        env={"AETHER_RUNTIME_AUDIT_DRY_RUN": "1", "AETHER_LLVM_NM": "llvm-nm"},
    )

    for marker in (
        "Runtime V28 Swift runtime dependency audit",
        "AETHER_RUNTIME_AUDIT_DRY_RUN",
        "AETHER_KERNEL_MACHO",
        "llvm-nm -n",
        "runtime-audit ok=",
        "source_hooks=",
        "linked_hooks=",
        "heap_shims=",
        "linked_heap_shims=",
        "required_symbols=",
        "swift_task_enqueueGlobalImpl",
        "swift_task_enqueueGlobalWithDelayImpl",
        "swift_task_enqueueGlobalWithDeadlineImpl",
        "swift_task_asyncMainDrainQueueImpl",
        "malloc",
        "posix_memalign",
    ):
        assert marker in script

    assert "Runtime V28 Swift runtime dependency audit dry run" in result.stdout
    assert "mach-o: /tmp/kernel.macho" in result.stdout
    assert "nm: llvm-nm" in result.stdout


def test_runtime_v28_netboot_gates_exist() -> None:
    net_iterate = read_repo("scripts/netboot/net-iterate.sh")
    doctor = read_repo("scripts/netboot/netboot-doctor.sh")

    for source in (net_iterate, doctor):
        assert "runtime v28: swift runtime dependency audit" in source
        assert_commands_era_in_netboot_sources(COMMANDS_V28, COMMANDS_V45, source, label="V28")

    for marker in (
        "probe shell: runtime",
        "^runtime ok=1 version=28 .*source_hooks=10 .*linked_hooks=2 .*heap_shims=5 .*linked_heap_shims=3 .*required_symbols=5",
        "^bootcert ok=1 version=66 .*concurrency=1 .*priority=1 .*fairness=1 .*stealing=[01] .*backpressure=[01] .*handoff=[01] .*wake=[01] .*job_exec=[01] .*worker_feed=[01] .*secondary_workers=[01] .*preemptive=[01] .*smp_scheduler=[01] .*atomics=1 .*locks=1 .*queues=1 .*smp=1 .*scheduler=1 .*certificate=1 .*agent=1 .*runtime=1 .*syscall=1 .*uaccess=1 .*usermode=1 .*process=1 .*loader=1 .*multiprocess=1 .*sdhci=1 .*card=1 .*block=1 .*fat32=1 .*mailbox=1 .*framebuf=1 .*console=1 .*pcie=1 .*vl805=1 .*xhci=1 .*kbd=[01] .*events_lost=0",
        "stale pre-V43 SD fallback",
    ):
        assert marker in net_iterate


def test_runtime_v28_docs_are_updated_after_hardware_proof() -> None:
    readme = read_repo("README.md")
    runbook = read_repo("docs/RUNBOOK.md")
    design = read_repo("docs/CONCURRENCY_DESIGN.md")

    for source in (readme, runbook, design):
        assert "Runtime V28 Swift runtime dependency audit" in source
        assert "bootcert ok=1 version=28" in source
        assert "runtime ok=1 version=28" in source
        assert "runtime-audit ok=1" in source

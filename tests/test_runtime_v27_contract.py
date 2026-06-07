import os
import pathlib
import subprocess


ROOT = pathlib.Path(__file__).resolve().parents[1]


COMMANDS_V27 = (
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


def test_runtime_v27_retained_taxonomy_api_contract_exists() -> None:
    support = read_repo("Sources/Support/include/Support.h")
    diagnostics = read_repo("Sources/Support/diagnostics.c")

    for marker in (
        "#define KERNEL_RETAINED_CATEGORY_NONE",
        "#define KERNEL_RETAINED_CATEGORY_COMMAND",
        "#define KERNEL_RETAINED_CATEGORY_FAULT",
        "#define KERNEL_RETAINED_CATEGORY_HEAP",
        "#define KERNEL_RETAINED_REASON_UNKNOWN",
        "#define KERNEL_RETAINED_REASON_PANIC_TEST",
        "#define KERNEL_RETAINED_REASON_SYNC_FAULT",
        "#define KERNEL_RETAINED_REASON_HEAP_INVALID_FREE",
        "#define KERNEL_RETAINED_REASON_HEAP_DOUBLE_FREE",
        "unsigned int kernel_retained_category(void)",
        "unsigned int kernel_retained_reason_id(void)",
        "unsigned int kernel_panic_reason_id_for(const char *reason)",
        "unsigned int kernel_panic_category_for_reason_id(unsigned int reason_id)",
        "void kernel_panic_with_taxonomy(const char *reason, unsigned int category, unsigned int reason_id, unsigned long esr, unsigned long elr, unsigned long far)",
    ):
        assert marker in support

    for marker in (
        "unsigned long category;",
        "unsigned long reason_id;",
        "c ^= r->category;",
        "c ^= r->reason_id;",
        "retained_write(KERNEL_RETAINED_KIND_FAULT, KERNEL_RETAINED_CATEGORY_FAULT, KERNEL_RETAINED_REASON_SYNC_FAULT",
        "kernel_panic_with_taxonomy",
        "kernel_panic_reason_id_for",
        "kernel_panic_category_for_reason_id",
    ):
        assert marker in diagnostics


def test_runtime_v27_application_shell_and_bootcert_surface_exist() -> None:
    app = read_repo("Sources/Application/Application.swift")
    shell = read_repo("Sources/Application/UARTShell.swift")
    exceptions = read_repo("Sources/Application/Exceptions.swift")

    for marker in (
        "Runtime V27 adds panic/fault taxonomy and symbolic retained records.",
        "kernel_event_emit(KERNEL_EVENT_KIND_BOOT, 43, 0, 0)",
        "runtime v27: panic taxonomy and symbolic retained records",
        "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 27,",
    ):
        assert marker in app

    for marker in (
        " version=40",
        " taxonomy=1",
        "kind_id=",
        " category=",
        " reason_id=",
        "kernel_retained_category()",
        "kernel_retained_reason_id()",
    ):
        assert marker in shell

    for marker in (
        "fault kind=sync category=",
        " reason_id=",
        "KERNEL_RETAINED_CATEGORY_FAULT",
        "KERNEL_RETAINED_REASON_SYNC_FAULT",
    ):
        assert marker in exceptions


def test_runtime_v27_symbolicate_retained_host_tool_contract_exists() -> None:
    script = read_repo("symbolicate-retained.sh")
    result = run_script(
        "symbolicate-retained.sh",
        "0x80000",
        "/tmp/kernel.macho",
        env={"AETHER_SYMBOLICATE_RETAINED_DRY_RUN": "1", "AETHER_LLVM_NM": "llvm-nm"},
    )

    for marker in (
        "Runtime V27 retained symbol lookup",
        "AETHER_SYMBOLICATE_RETAINED_DRY_RUN",
        "AETHER_KERNEL_MACHO",
        "llvm-nm -n",
        "symbol address=",
        "symbol_name=",
        "symbol_offset=",
    ):
        assert marker in script

    assert "address: 0x80000" in result.stdout
    assert "mach-o: /tmp/kernel.macho" in result.stdout
    assert "nm: llvm-nm" in result.stdout


def test_runtime_v27_netboot_gates_exist() -> None:
    net_iterate = read_repo("net-iterate.sh")
    doctor = read_repo("netboot-doctor.sh")

    for source in (net_iterate, doctor):
        assert "runtime v27: panic taxonomy and symbolic retained records" in source
        assert COMMANDS_V27 in source

    for marker in (
        "^bootcert ok=1 version=43 .*priority=1 .*fairness=1 .*stealing=1 .*backpressure=1 .*handoff=1 .*wake=1 .*job_exec=1 .*worker_feed=1 .*secondary_workers=1 .*preemptive=1 .*smp_scheduler=1 .*atomics=1 .*locks=1 .*queues=1 .*smp=1 .*scheduler=1 .*certificate=1 .*agent=1 .*runtime=1 .*events_lost=0",
        "stale pre-V43 SD fallback",
    ):
        assert marker in net_iterate


def test_runtime_v27_docs_are_updated_after_hardware_proof() -> None:
    readme = read_repo("README.md")
    runbook = read_repo("RUNBOOK.md")
    design = read_repo("CONCURRENCY_DESIGN.md")

    for source in (readme, runbook, design):
        assert "Runtime V27 panic taxonomy and symbolic retained records" in source
        assert "bootcert ok=1 version=29" in source
        assert "retained valid=1 kind=panic kind_id=1 category=1 reason_id=1" in source
        assert "symbol address=" in source

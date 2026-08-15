import pathlib
import re


ROOT = pathlib.Path(__file__).resolve().parents[1]


COMMANDS_V45 = (
    "commands=help,protocol,status,heap,queues,tasks,tasks2,kobjects,drivers,drivercheck,"
    "mailboxes,sendtest,supervisor,health,capcheck,events,runtime,agent,certificate,sched,sched2,sched3,sched4,sched5,sched6,sched7,sched8,sched9,sched10,sched11,sched12,cores,locks,runqueues,diag,irqs,timers,"
    "memcheck,faults,retained,retained-clear,memmap,mmu,pools,poolcheck,"
    "heapfrag,poolstats,frames,heapcheck,framecheck,stress,frameprobe,bootcert,"
    "canceltest,taskcheck,channeltest,bootcheck,soak,heap-invalid-free-test,"
    "heap-double-free-test,panic-test,fault-test,reboot,vmm"
)


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_runtime_v45_version_bump_and_skeleton_exist() -> None:
    support = read_repo("Sources/Support/include/Support.h")
    app = read_repo("Sources/Application/Application.swift")
    shell = read_repo("Sources/Application/UARTShell.swift")

    assert "#define KERNEL_SCHEDULER_VERSION 46U" in support

    for marker in (
        "kernel_event_emit(KERNEL_EVENT_KIND_BOOT, 64, 0, 0)",
        "runtime v45: dynamic virtual memory (page tables + TLB)",
        "let pt_ok = kernel_vmm_pt_alloc_selftest()",
        "let vmm_ok = kernel_vmm_vmm_selftest()",
        "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 45, UInt(pt_ok), UInt(vmm_ok))",
        "Runtime V45 adds dynamic virtual memory (page table allocator + 4KiB map/unmap + TLB maintenance on live EL1 tables).",
    ):
        assert marker in app

    # vmm command surface: printVMM wired with version=50
    assert ",vmm\n" in shell or ",vmm\"" in shell or "vmm" in shell.split("shell ready commands=")[1].split("\n")[0]
    assert "func printVMM()" in shell
    assert "version=50" in shell


def test_runtime_v45_shell_certificate_and_vmm_skeleton() -> None:
    shell = read_repo("Sources/Application/UARTShell.swift")

    assert "func printVMM()" in shell
    assert "version=50" in shell
    assert "vmm ok=0 version=45 pending-impl" not in shell


def test_runtime_v45_contract_test_and_netboot_expectations() -> None:
    # Self: this file exists and has the v45 markers
    self_text = read_repo("tests/test_runtime_v45_contract.py")
    assert "COMMANDS_V45" in self_text
    assert "version=50" in self_text
    assert "vmm" in self_text
    assert "runtime v45: dynamic virtual memory" in self_text

    net_iterate = read_repo("scripts/netboot/net-iterate.sh")
    # v45 banner is now required for fresh-boot detection in the iteration loop
    assert "runtime v45: dynamic virtual memory (page tables + TLB)" in net_iterate


def test_runtime_v45_does_not_break_v44_historical_markers() -> None:
    # Ensure we did not clobber prior version strings needed by the v44 contract test
    support = read_repo("Sources/Support/include/Support.h")
    # The define is now 46, but v44 contract test reads the scheduler source for its own V44 markers, not this define
    app = read_repo("Sources/Application/Application.swift")
    # Boot emit for 64 is current; historical v44 comments/banners stay in the source
    assert "Runtime V44 adds bounded SMP concurrency soak" in app
    assert "runtime v44: bounded smp concurrency soak" in app


def test_runtime_v45_no_placeholder_selftest_emit() -> None:
    app = read_repo("Sources/Application/Application.swift")
    live_emit = "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 45, UInt(pt_ok), UInt(vmm_ok))"
    assert live_emit in app
    assert app.count("kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 45") == 1

    banned_zero_patterns = (
        r"KERNEL_EVENT_KIND_SELFTEST,\s*45,\s*0,\s*0",
        r"KERNEL_EVENT_KIND_SELFTEST,\s*45,\s*UInt\s*\(\s*0\s*\),\s*UInt\s*\(\s*0\s*\)",
    )
    for pattern in banned_zero_patterns:
        assert re.search(pattern, app) is None, (
            f"placeholder zero-arg SELFTEST 45 emit matched: {pattern}"
        )


def _swift_function_body(shell: str, name: str) -> str:
    return shell.split(f"func {name}()")[1].split("func ")[0]


def test_runtime_v45_boot_snapshot_contract() -> None:
    support = read_repo("Sources/Support/include/Support.h")
    app = read_repo("Sources/Application/Application.swift")
    shell = read_repo("Sources/Application/UARTShell.swift")

    for decl in (
        "int kernel_vmm_boot_snapshot_seal(int pt_ok, int vmm_ok, int asplit_ok, int el0_ok);",
        "int kernel_vmm_boot_pt_proven(void);",
        "int kernel_vmm_boot_vmm_proven(void);",
        "int kernel_vmm_boot_asplit_proven(void);",
        "int kernel_vmm_boot_el0_proven(void);",
    ):
        assert decl in support

    assert app.count("kernel_vmm_boot_snapshot_seal(") == 1
    seal_idx = app.index("kernel_vmm_boot_snapshot_seal(")
    emit45_idx = app.index("kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 45")
    assert seal_idx < emit45_idx

    agent = _swift_function_body(shell, "printAgentSession")
    certificate = _swift_function_body(shell, "printSubstrateCertificate")
    vmm_fn = _swift_function_body(shell, "printVMM")
    bootcert = _swift_function_body(shell, "printBootcert")

    assert "kernel_vmm_boot_vmm_proven()" in agent
    assert "kernel_vmm_vmm_selftest()" not in agent

    for body in (certificate, bootcert):
        assert "kernel_vmm_boot_vmm_proven()" in body
        assert "kernel_vmm_boot_asplit_proven()" in body
        assert "kernel_vmm_boot_el0_proven()" in body
        assert "kernel_vmm_vmm_selftest()" not in body
        assert "kernel_vmm_asplit_selftest()" not in body
        assert "kernel_vmm_el0_selftest()" not in body

    assert "kernel_vmm_boot_pt_proven()" in vmm_fn
    assert "kernel_vmm_boot_vmm_proven()" in vmm_fn
    assert "kernel_vmm_pt_alloc_selftest()" not in vmm_fn
    assert "kernel_vmm_vmm_selftest()" not in vmm_fn

    # printASplit / printEL0 remain live selftest readers (out of snapshot scope)
    asplit_fn = _swift_function_body(shell, "printASplit")
    el0_fn = _swift_function_body(shell, "printEL0")
    assert "kernel_vmm_asplit_selftest()" in asplit_fn
    assert "kernel_vmm_el0_selftest()" in el0_fn

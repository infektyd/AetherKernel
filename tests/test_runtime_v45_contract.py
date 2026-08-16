import pathlib
import re


ROOT = pathlib.Path(__file__).resolve().parents[1]


COMMANDS_V45 = (
    "commands=help,protocol,status,heap,queues,tasks,tasks2,kobjects,drivers,drivercheck,"
    "mailboxes,sendtest,supervisor,health,capcheck,events,runtime,agent,certificate,sched,sched2,sched3,sched4,sched5,sched6,sched7,sched8,sched9,sched10,sched11,sched12,cores,locks,runqueues,diag,irqs,timers,"
    "memcheck,faults,retained,retained-clear,memmap,mmu,pools,poolcheck,"
    "heapfrag,poolstats,frames,heapcheck,framecheck,stress,frameprobe,bootcert,"
    "canceltest,taskcheck,channeltest,bootcheck,soak,heap-invalid-free-test,"
    "heap-double-free-test,panic-test,fault-test,reboot,vmm,asplit,el0,syscall,"
    "uaccess,usermode,process,loader,multiprocess,sdhci,card,block,fat32,mailbox,"
    "framebuf,console,pcie,vl805,xhci"
)

SHELL_READY_V45 = f"shell ready {COMMANDS_V45}"


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


def test_runtime_v45_shell_ready_doctor_netiterate_ceiling() -> None:
    """COMMANDS_V45 must lock live ready list through xhci on shell, doctor, net-iterate."""
    shell = read_repo("Sources/Application/UARTShell.swift")
    doctor = read_repo("scripts/netboot/netboot-doctor.sh")
    net_iterate = read_repo("scripts/netboot/net-iterate.sh")

    for source in (shell, doctor, net_iterate):
        assert COMMANDS_V45 in source
        assert ",xhci" in source
        assert ",vmm,asplit,el0" in source

    assert SHELL_READY_V45 in shell
    assert SHELL_READY_V45 in doctor
    assert SHELL_READY_V45 in net_iterate


def test_runtime_v45_netboot_vmm_boot_greps_and_shell_probes() -> None:
    net_iterate = read_repo("scripts/netboot/net-iterate.sh")

    for marker in (
        'grep -qa "vmmcheck ok=1"',
        'grep -qa "asplit ok=1 version=46"',
        'grep -qa "el0 ok=1 version=47"',
        "probe shell: vmm",
        'probe_shell "vmm" "^vmm ok=1 version=50 pt=.* selftest=.*"',
        "probe shell: asplit",
        'probe_shell "asplit" "^asplit ok=1 version=46"',
        "probe shell: el0",
        'probe_shell "el0" "^el0 ok=1 version=47"',
    ):
        assert marker in net_iterate


def test_runtime_v45_netboot_doctor_boot_ceiling_through_v66() -> None:
    """netboot-doctor bring-up gate must match net-iterate metal floor through v66 + xhci ready."""
    doctor = read_repo("scripts/netboot/netboot-doctor.sh")

    for marker in (
        'grep -q "runtime v45: dynamic virtual memory (page tables + TLB)"',
        'grep -q "runtime v63: xHCI capability register probe"',
        'grep -q "runtime v64: xHCI controller init"',
        'grep -q "xhci_run ok=1 version=64"',
        'grep -q "runtime v66: HID boot-protocol keyboard"',
        'grep -q "kbd ok=[01] version=66"',
        'grep -q "usb_enum ok=[01] version=65"',
        'grep -q "vmmcheck ok=1"',
        'grep -q "xhci ok=1 version=63"',
        SHELL_READY_V45,
    ):
        assert marker in doctor


def test_runtime_v45_netboot_grep_ceiling_through_v63() -> None:
    """v45 partial lock: metal boot greps must still reach v63 xhci before v64+ (S44)."""
    net_iterate = read_repo("scripts/netboot/net-iterate.sh")

    for marker in (
        'grep -qa "runtime v46: kernel/user address-space split (isolated page tables)"',
        'grep -qa "runtime v63: xHCI capability register probe"',
        'grep -qa "xhci ok=1 version=63"',
    ):
        assert marker in net_iterate


def test_runtime_v45_docs_boot_ceiling_through_v66() -> None:
    """Operator docs must name v64–v66 boot greps (DOC-s44-docs)."""
    readme = read_repo("README.md")
    runbook = read_repo("docs/RUNBOOK.md")
    roadmap = read_repo("docs/ROADMAP.md")

    for marker in (
        "runtime v64: xHCI controller init",
        "xhci_run ok=1 version=64",
        "runtime v65: USB device enumeration",
        "usb_enum ok=[01] version=65",
        "runtime v66: HID boot-protocol keyboard",
        "kbd ok=[01] version=66",
    ):
        assert marker in readme
        assert marker in runbook

    assert "Where we are now (V66)" in roadmap


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

    assert 'uartPuts(" swift=6.3.2")' in bootcert

    assert "kernel_vmm_boot_pt_proven()" in vmm_fn
    assert "kernel_vmm_boot_vmm_proven()" in vmm_fn
    assert "kernel_vmm_pt_alloc_selftest()" not in vmm_fn
    assert "kernel_vmm_vmm_selftest()" not in vmm_fn

    # printASplit and printEL0 read boot snapshot (no live selftest in shell readers)
    asplit_fn = _swift_function_body(shell, "printASplit")
    el0_fn = _swift_function_body(shell, "printEL0")
    assert "kernel_vmm_boot_asplit_proven()" in asplit_fn
    assert "kernel_vmm_asplit_selftest()" not in asplit_fn
    assert "kernel_vmm_boot_el0_proven()" in el0_fn
    assert "kernel_vmm_el0_selftest()" not in el0_fn

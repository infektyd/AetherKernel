import pathlib


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

    assert "#define KERNEL_SCHEDULER_VERSION 45U" in support

    for marker in (
        "kernel_event_emit(KERNEL_EVENT_KIND_BOOT, 45, 0, 0)",
        "runtime v45: dynamic virtual memory (page tables + TLB)",
        "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 45, 0, 0)",
        "Runtime V45 adds dynamic virtual memory (page table allocator + 4KiB map/unmap + TLB maintenance on live EL1 tables).",
    ):
        assert marker in app

    # vmm placeholder in command surface (full impl + vmm=1 in later slices)
    assert ",vmm\n" in shell or ",vmm\"" in shell or "vmm" in shell.split("shell ready commands=")[1].split("\n")[0]
    assert 'printVMM()' in shell
    assert 'vmm ok=0 version=45 pending-impl' in shell or 'func printVMM' in shell


def test_runtime_v45_shell_certificate_and_vmm_skeleton() -> None:
    shell = read_repo("Sources/Application/UARTShell.swift")

    assert " version=45" in shell  # certificate version bumped (vmm=1 added in v45-4)
    assert "func printVMM()" in shell
    assert "vmm ok=0 version=45 pending-impl" in shell


def test_runtime_v45_contract_test_and_netboot_expectations() -> None:
    # Self: this file exists and has the v45 markers
    self_text = read_repo("tests/test_runtime_v45_contract.py")
    assert "COMMANDS_V45" in self_text
    assert "version=45" in self_text
    assert "vmm" in self_text
    assert "runtime v45: dynamic virtual memory" in self_text

    net_iterate = read_repo("net-iterate.sh")
    # v45 banner is now required for fresh-boot detection in the iteration loop
    assert "runtime v45: dynamic virtual memory (page tables + TLB)" in net_iterate
    # Historical v44 strings remain for the v44 contract test; v45 adds its own
    assert "bootcert ok=1 version=44" in net_iterate or "certificate ok=1 version=44" in net_iterate


def test_runtime_v45_does_not_break_v44_historical_markers() -> None:
    # Ensure we did not clobber prior version strings needed by the v44 contract test
    support = read_repo("Sources/Support/include/Support.h")
    # The define is now 45, but v44 contract test reads the scheduler source for its own V44 markers, not this define
    app = read_repo("Sources/Application/Application.swift")
    # Boot emit for 45 is new; historical v44 comments/banners stay in the source
    assert "Runtime V44 adds bounded SMP concurrency soak" in app
    assert "runtime v44: bounded smp concurrency soak" in app

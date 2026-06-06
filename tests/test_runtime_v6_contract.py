import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_support_declares_runtime_v6_retained_record_api() -> None:
    support = read_repo("Sources/Support/include/Support.h")

    for symbol in (
        "KERNEL_RETAINED_RECORD_ADDR",
        "kernel_retained_valid",
        "kernel_retained_kind",
        "kernel_retained_sequence",
        "kernel_retained_esr",
        "kernel_retained_elr",
        "kernel_retained_far",
        "kernel_retained_reason_len",
        "kernel_retained_reason_byte",
        "kernel_retained_clear",
        "kernel_retained_write_panic",
        "kernel_panic_with_far",
        "kernel_panic_with_detail",
        "kernel_retained_write_fault",
    ):
        assert symbol in support


def test_diagnostics_uses_fixed_retained_page_below_heap_and_checksum() -> None:
    support = read_repo("Sources/Support/include/Support.h")
    diagnostics = read_repo("Sources/Support/diagnostics.c")

    assert "KERNEL_RETAINED_RECORD_ADDR 0x003ff000UL" in support
    assert "(volatile retained_record *)KERNEL_RETAINED_RECORD_ADDR" in diagnostics
    assert "RETAINED_MAGIC" in diagnostics
    assert "retained_checksum" in diagnostics
    assert "retained_reason" in diagnostics
    assert "retained_flush" in diagnostics
    assert "dc cvac" in diagnostics
    assert diagnostics.index("r->checksum = retained_checksum(r)") < diagnostics.index("retained_flush(r)")
    assert "malloc(" not in diagnostics
    assert "free(" not in diagnostics


def test_panic_and_fault_paths_write_retained_record_before_watchdog_reset() -> None:
    diagnostics = read_repo("Sources/Support/diagnostics.c")
    exceptions = read_repo("Sources/Application/Exceptions.swift")

    assert "void kernel_panic_with_detail(const char *reason, unsigned long esr, unsigned long elr, unsigned long far)" in diagnostics
    assert "kernel_panic_with_taxonomy(reason, category, reason_id, esr, elr, far)" in diagnostics
    assert "void kernel_panic_with_far(const char *reason, unsigned long far)" in diagnostics
    assert "kernel_panic_with_detail(reason, 0, 0, far)" in diagnostics
    panic_body = diagnostics[
        diagnostics.index("void kernel_panic_with_taxonomy"):
        diagnostics.index("void kernel_panic_with_detail")
    ]
    assert panic_body.index("retained_write(KERNEL_RETAINED_KIND_PANIC, category, reason_id, esr, elr, far, reason)") < panic_body.index("watchdog_reset_now()")
    assert "kernel_panic_with_far(reason, 0)" in diagnostics
    assert "retained_write(KERNEL_RETAINED_KIND_FAULT, KERNEL_RETAINED_CATEGORY_FAULT, KERNEL_RETAINED_REASON_SYNC_FAULT" in diagnostics
    assert "kernel_retained_write_fault(UInt(esr), UInt(elr), UInt(far))" in exceptions
    assert exceptions.index("kernel_retained_write_fault") < exceptions.index("watchdog_reset_now()")


def test_retained_clear_masks_irqs_while_mutating_record() -> None:
    diagnostics = read_repo("Sources/Support/diagnostics.c")

    body = diagnostics[
        diagnostics.index("void kernel_retained_clear(void)"):
        diagnostics.index("void kernel_retained_write_panic")
    ]

    assert "unsigned long flags = irq_save();" in body
    assert body.index("unsigned long flags = irq_save();") < body.index("r->magic = 0;")
    assert body.index("retained_flush(r);") < body.index("irq_restore(flags);")


def test_uart_shell_v6_retained_commands_and_response_prefixes_exist() -> None:
    shell = read_repo("Sources/Application/UARTShell.swift")

    for marker in (
        "commands=help,protocol,status,heap,queues,tasks,tasks2,kobjects,drivers,drivercheck,mailboxes,sendtest,supervisor,health,capcheck,events,runtime,agent,certificate,sched,sched2,sched3,sched4,sched5,sched6,sched7,cores,locks,runqueues,diag,irqs,timers,memcheck,faults,retained,retained-clear,memmap,mmu,pools,poolcheck,heapfrag,poolstats,frames,heapcheck,framecheck,stress,frameprobe,bootcert,canceltest,taskcheck,channeltest,bootcheck,soak,heap-invalid-free-test,heap-double-free-test,panic-test,fault-test,reboot",
        "retained valid=",
        " kind=",
        " seq=",
        " reason=",
        "retained clear ok=1",
    ):
        assert marker in shell


def test_runtime_v6_boot_marker_and_net_iterate_gate_exist() -> None:
    app = read_repo("Sources/Application/Application.swift")
    net_iterate = read_repo("net-iterate.sh")
    doctor = read_repo("netboot-doctor.sh")

    assert "runtime v6: retained panic/fault records" in app
    assert "runtime v6: retained panic/fault records" in net_iterate
    assert "runtime v6: retained panic/fault records" in doctor

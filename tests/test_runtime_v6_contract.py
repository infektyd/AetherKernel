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

    assert "kernel_retained_write_panic(reason)" in diagnostics
    assert diagnostics.index("kernel_retained_write_panic(reason)") < diagnostics.index("watchdog_reset_now()")
    assert "retained_write(KERNEL_RETAINED_KIND_FAULT, esr, elr, far" in diagnostics
    assert "kernel_retained_write_fault(UInt(esr), UInt(elr), UInt(far))" in exceptions
    assert exceptions.index("kernel_retained_write_fault") < exceptions.index("watchdog_reset_now()")


def test_uart_shell_v6_retained_commands_and_response_prefixes_exist() -> None:
    shell = read_repo("Sources/Application/UARTShell.swift")

    for marker in (
        "commands=help,status,heap,queues,tasks,diag,irqs,timers,memcheck,faults,retained,retained-clear,panic-test,fault-test,reboot",
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

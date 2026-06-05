import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_support_declares_runtime_v5_diagnostics_api() -> None:
    support = read_repo("Sources/Support/include/Support.h")

    for symbol in (
        "kernel_irq_total_count",
        "kernel_irq_cntp_count",
        "kernel_irq_uart0_count",
        "kernel_irq_spurious_count",
        "kernel_irq_unknown_count",
        "kernel_irq_record",
        "kernel_timer_deadline_ticks",
        "kernel_timer_active_count",
        "heap_allocated_bytes",
        "heap_high_water_bytes",
        "heap_failed_alloc_count",
        "heap_integrity_check",
        "kernel_panic",
        "kernel_record_fault",
        "kernel_fault_seen",
        "kernel_trigger_sync_fault",
    ):
        assert symbol in support


def test_uart_shell_v5_commands_and_response_prefixes_exist() -> None:
    shell = read_repo("Sources/Application/UARTShell.swift")

    for marker in (
        "commands=help,status,heap,queues,tasks,diag,irqs,timers,memcheck,faults,panic-test,fault-test,reboot",
        "diag version=v5",
        "irqs total=",
        "timers now=",
        "memcheck ok=",
        "faults seen=",
        "shell panic-test reason=command",
        "shell fault-test reason=command",
    ):
        assert marker in shell


def test_irq_handler_records_v5_irq_counters() -> None:
    irq = read_repo("Sources/Application/IRQHandler.swift")

    assert "kernel_irq_record(intid)" in irq
    assert "kernel_irq_unknown_count" not in irq


def test_heap_allocator_tracks_v5_pressure_and_integrity() -> None:
    alloc = read_repo("Sources/Support/alloc.c")

    for marker in (
        "heap_high_water",
        "heap_failed_allocs",
        "heap_allocated_bytes",
        "heap_high_water_bytes",
        "heap_failed_alloc_count",
        "heap_integrity_check",
    ):
        assert marker in alloc


def test_exception_handler_emits_machine_checkable_fault_line() -> None:
    exceptions = read_repo("Sources/Application/Exceptions.swift")

    assert "kernel_record_fault(UInt(esr), UInt(elr), UInt(far))" in exceptions
    assert "fault kind=sync esr=" in exceptions
    assert " elr=" in exceptions
    assert " far=" in exceptions


def test_runtime_v5_boot_marker_and_net_iterate_gate_exist() -> None:
    app = read_repo("Sources/Application/Application.swift")
    net_iterate = read_repo("net-iterate.sh")

    assert "runtime v5: diagnostics shell" in app
    assert "runtime v5: diagnostics shell" in net_iterate

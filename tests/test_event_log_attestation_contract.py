import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def _swift_function_body(shell: str, name: str) -> str:
    return shell.split(f"func {name}()")[1].split("func ")[0]


def test_event_log_bootcert_reads_before_shell_emit() -> None:
    shell = read_repo("Sources/Application/UARTShell.swift")
    bootcert = _swift_function_body(shell, "printBootcert")

    lost_idx = bootcert.index("let eventsLost = kernel_event_lost_count()")
    count_idx = bootcert.index("let eventCount = kernel_event_count()")
    emit_idx = bootcert.index("kernel_event_emit(KERNEL_EVENT_KIND_SHELL, 44")
    assert lost_idx < emit_idx
    assert count_idx < emit_idx


def test_event_log_print_events_reads_lost_before_shell_emit() -> None:
    shell = read_repo("Sources/Application/UARTShell.swift")
    events_fn = _swift_function_body(shell, "printEvents")

    lost_idx = events_fn.index("let lost = kernel_event_lost_count()")
    emit_idx = events_fn.index("kernel_event_emit(KERNEL_EVENT_KIND_SHELL, 22")
    assert lost_idx < emit_idx
    assert 'uartPutDec(UInt64(lost))' in events_fn
    assert "kernel_event_lost_count())" not in events_fn.split("uartPuts(\" lost=\")")[1]


def test_event_log_certificate_and_bootcert_print_live_event_count() -> None:
    shell = read_repo("Sources/Application/UARTShell.swift")

    for name in ("printBootcert", "printSubstrateCertificate"):
        body = _swift_function_body(shell, name)
        assert "let eventsSelftest = kernel_event_log_selftest()" in body
        assert "let eventCount = kernel_event_count()" in body
        assert "eventsSelftest != 0" in body
        events_print = body.split('uartPuts(" events=")')[1].split("uartPuts")[0]
        assert "eventCount" in events_print
        assert "eventsSelftest" not in events_print

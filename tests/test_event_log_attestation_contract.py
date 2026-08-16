import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def _swift_function_body(shell: str, name: str) -> str:
    return shell.split(f"func {name}()")[1].split("func ")[0]


def test_event_log_bootcert_snapshots_lost_after_shell_emit() -> None:
    """EL-11 (S57): bootcert must not attest events_lost/events before SHELL emit."""
    shell = read_repo("Sources/Application/UARTShell.swift")
    bootcert = _swift_function_body(shell, "printBootcert")

    emit_idx = bootcert.index("kernel_event_emit(KERNEL_EVENT_KIND_SHELL, 44")
    lost_idx = bootcert.index("let eventsLost = kernel_event_lost_count()")
    count_idx = bootcert.index("let eventCount = kernel_event_count()")
    selftest_idx = bootcert.index("let eventsSelftest = kernel_event_log_selftest()")
    assert emit_idx < lost_idx
    assert emit_idx < count_idx
    assert lost_idx < selftest_idx
    assert count_idx < selftest_idx

    pre_emit = bootcert[:emit_idx]
    assert "kernel_event_lost_count()" not in pre_emit
    assert "let eventCount = kernel_event_count()" not in pre_emit
    assert "let countBefore = kernel_event_count()" in pre_emit

    post_selftest = bootcert[selftest_idx:]
    assert "kernel_event_lost_count()" not in post_selftest
    assert "kernel_event_count()" not in post_selftest


def test_event_log_print_events_snapshots_lost_after_shell_emit() -> None:
    """EL-11 (S57): events command must not print stale lost before SHELL emit."""
    shell = read_repo("Sources/Application/UARTShell.swift")
    events_fn = _swift_function_body(shell, "printEvents")

    emit_idx = events_fn.index("kernel_event_emit(KERNEL_EVENT_KIND_SHELL, 22")
    lost_idx = events_fn.index("let lost = kernel_event_lost_count()")
    count_idx = events_fn.index("let count = kernel_event_count()")
    selftest_idx = events_fn.index("let selftest = kernel_event_log_selftest()")
    assert emit_idx < lost_idx
    assert emit_idx < count_idx
    assert lost_idx < selftest_idx
    assert count_idx < selftest_idx

    pre_emit = events_fn[:emit_idx]
    assert "kernel_event_lost_count()" not in pre_emit
    lost_print = events_fn.split('uartPuts(" lost=")')[1].split("uartPuts")[0]
    assert "uartPutDec(UInt64(lost))" in lost_print
    assert "kernel_event_lost_count())" not in lost_print

    post_selftest = events_fn[selftest_idx:]
    assert "kernel_event_lost_count()" not in post_selftest
    assert "kernel_event_count()" not in post_selftest


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

import pathlib
import re


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_boot_selftest_id_18_is_unique_cancel_snapshot() -> None:
    app = read_repo("Sources/Application/Application.swift")

    matches = re.findall(
        r"kernel_event_emit\(KERNEL_EVENT_KIND_SELFTEST,\s*18\b",
        app,
    )
    assert len(matches) == 1
    assert "UInt(kernel_cancel_selftest())" in app
    assert (
        "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 18, UInt(kernel_cancel_selftest()), 0)"
        in app
    )


def test_canceltest_shell_emits_shell_kind_not_selftest_id_18() -> None:
    shell = read_repo("Sources/Application/UARTShell.swift")

    assert "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 18" not in shell
    assert (
        "kernel_event_emit(KERNEL_EVENT_KIND_SHELL, 18, UInt(selftest), UInt(kernel_cancel_completed_count()))"
        in shell
    )
    assert "func printCanceltest()" in shell
    assert "canceltest ok=" in shell

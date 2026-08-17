"""EPIC G V95: system timer C3 match after GENET. No GPU C0/C2. No EL0."""

import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_stimer3_c3_match_without_el0() -> None:
    load = read_repo("Sources/Support/kernel_stimer3.c")
    st_ro = read_repo("Sources/Support/kernel_stimer.c")
    st2 = read_repo("Sources/Support/kernel_stimer2.c")
    support = read_repo("Sources/Support/include/Support.h")
    app = read_repo("Sources/Application/Application.swift")
    shell = read_repo("Sources/Application/UARTShell.swift")
    iterate = read_repo("scripts/netboot/net-iterate.sh")
    scheduler = read_repo("Sources/Support/kernel_scheduler.c")

    assert "0xFE003000" in load
    assert "ST_C3" in load
    assert "ST_CS" in load
    assert "G32(ST_C3) =" in load
    assert "G32(ST_CS) =" in load
    assert "G32(ST_C0) =" not in load
    assert "G32(ST_C1) =" not in load
    assert "G32(ST_C2) =" not in load
    assert "int           kernel_stimer3_selftest(void);" in support
    assert 'uartPuts("runtime v95: system timer C3 match\\n")' in app
    assert 'uartPuts("stimer3 ok=")' in app
    assert "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 95" not in app
    assert "func printStimer3()" in shell
    assert 'shellBufferSliceEquals(commandStart, commandLen, "stimer3")' in shell
    assert ",gpio3,stimer3" in shell
    assert 'grep -qa "stimer3 ok=1 version=95 chan=3 match=1"' in iterate
    assert 'probe_shell "stimer3" "^stimer3 ok=1 version=95 chan=3 match=1"' in iterate
    assert "ping -c 2" in iterate
    assert "SOCK_STREAM" in iterate
    assert "sched12" in iterate
    assert "kernel_stimer3_selftest" not in scheduler
    assert "alloc_dma" not in load
    assert "kernel_event_emit" not in load
    assert "kernel_enter_el0_and_wait" not in load
    assert "0xbf000002" in load
    # V84 stays read-only. V92 stays C1-only.
    assert "G32(ST_CS) =" not in st_ro
    assert "G32(ST_C3) =" not in st_ro
    assert "G32(ST_C3) =" not in st2


def test_no_boot_el0_enter_after_genet12() -> None:
    """sys_socket after GENET DMA I-aborted; do not ship a second boot EL0 enter."""
    app = read_repo("Sources/Application/Application.swift")
    after = app.split("kernel_genet12_selftest", 1)[1]
    assert "kernel_enter_el0_and_wait" not in after
    assert "kernel_socket_selftest" not in after
    assert "kernel_stimer3_selftest" in after

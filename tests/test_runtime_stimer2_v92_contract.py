"""EPIC G V92: system timer C1 match after GENET. No GPU C0/C2. No EL0."""

import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_stimer2_c1_match_without_el0() -> None:
    load = read_repo("Sources/Support/kernel_stimer2.c")
    st_ro = read_repo("Sources/Support/kernel_stimer.c")
    support = read_repo("Sources/Support/include/Support.h")
    app = read_repo("Sources/Application/Application.swift")
    shell = read_repo("Sources/Application/UARTShell.swift")
    iterate = read_repo("scripts/netboot/net-iterate.sh")
    scheduler = read_repo("Sources/Support/kernel_scheduler.c")

    assert "0xFE003000" in load
    assert "ST_C1" in load
    assert "ST_CS" in load
    assert "G32(ST_C1) =" in load
    assert "G32(ST_CS) =" in load
    assert "G32(ST_C0) =" not in load
    assert "G32(ST_C2) =" not in load
    assert "G32(ST_C3) =" not in load
    assert "int           kernel_stimer2_selftest(void);" in support
    assert 'uartPuts("runtime v92: system timer C1 match\\n")' in app
    assert 'uartPuts("stimer2 ok=")' in app
    assert "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 92" not in app
    assert "func printStimer2()" in shell
    assert 'shellBufferSliceEquals(commandStart, commandLen, "stimer2")' in shell
    assert ",gpio2,stimer2" in shell
    assert 'grep -qa "stimer2 ok=1 version=92 chan=1 match=1"' in iterate
    assert 'probe_shell "stimer2" "^stimer2 ok=1 version=92 chan=1 match=1"' in iterate
    assert "ping -c 2" in iterate
    assert "SOCK_STREAM" in iterate
    assert "sched12" in iterate
    assert "kernel_stimer2_selftest" not in scheduler
    assert "alloc_dma" not in load
    assert "kernel_event_emit" not in load
    assert "kernel_enter_el0_and_wait" not in load
    assert "0xbf000002" in load
    # V84 stays read-only.
    assert "G32(ST_CS) =" not in st_ro
    assert "G32(ST_C1) =" not in st_ro


def test_no_boot_el0_enter_after_genet12() -> None:
    """sys_socket after GENET DMA I-aborted; do not ship a second boot EL0 enter."""
    app = read_repo("Sources/Application/Application.swift")
    after = app.split("kernel_genet12_selftest", 1)[1]
    assert "kernel_enter_el0_and_wait" not in after
    assert "kernel_socket_selftest" not in after
    assert "kernel_stimer2_selftest" in after

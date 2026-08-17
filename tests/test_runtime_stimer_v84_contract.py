"""EPIC G V84: system timer CLO/CHI probe. Read-only. No boot event emit."""

import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_stimer_is_read_only_register_probe() -> None:
    st = read_repo("Sources/Support/kernel_stimer.c")
    support = read_repo("Sources/Support/include/Support.h")
    app = read_repo("Sources/Application/Application.swift")
    shell = read_repo("Sources/Application/UARTShell.swift")
    iterate = read_repo("scripts/netboot/net-iterate.sh")
    scheduler = read_repo("Sources/Support/kernel_scheduler.c")

    assert "0xFE003000" in st
    assert "ST_CLO" in st
    assert "ST_CHI" in st
    assert "ST_C0" in st
    assert "ST_C3" in st
    assert "STIMER_CLO_WAIT_TICKS" in st
    assert "int           kernel_stimer_selftest(void);" in support
    assert 'uartPuts("runtime v84: system timer register probe\\n")' in app
    assert 'uartPuts("stimer ok=")' in app
    assert "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 84" not in app
    assert "func printStimer()" in shell
    assert 'shellBufferSliceEquals(commandStart, commandLen, "stimer")' in shell
    assert ",spi2,stimer" in shell
    assert 'grep -qa "stimer ok=1 version=84 clo=.* chi=.* chans="' in iterate
    assert 'probe_shell "stimer" "^stimer ok=1 version=84 clo=.* chi=.* chans=1"' in iterate
    assert "ping -c 2" in iterate
    assert "SOCK_STREAM" in iterate
    assert "sched12" in iterate
    assert "kernel_stimer_selftest" not in scheduler
    assert "i < 200000" not in st
    assert "alloc_dma" not in st
    assert "kernel_event_emit" not in st
    assert "kernel_enter_el0_and_wait" not in st
    # Read-only: never write CS or compare (GPU uses C0/C2).
    assert "G32(ST_CS) =" not in st
    assert "G32(ST_C0) =" not in st
    assert "G32(ST_C1) =" not in st
    assert "G32(ST_C2) =" not in st
    assert "G32(ST_C3) =" not in st


def test_no_boot_el0_enter_after_genet12() -> None:
    """sys_socket after GENET DMA I-aborted (esr=0xbf000002 elr=0x100002000)."""
    app = read_repo("Sources/Application/Application.swift")
    st = read_repo("Sources/Support/kernel_stimer.c")
    after = app.split("kernel_genet12_selftest", 1)[1]
    assert "kernel_enter_el0_and_wait" not in after
    assert "kernel_socket_selftest" not in after
    assert "kernel_stimer_selftest" in after
    assert "kernel_enter_el0_and_wait" not in st
    assert "0xbf000002" in st

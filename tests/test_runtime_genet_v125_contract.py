"""EPIC F V125: GENET TX MIB after one frame. No EL0. Not a protocol clone."""

import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_genet21_tx_mib_without_el0() -> None:
    genet = read_repo("Sources/Support/kernel_genet.c")
    support = read_repo("Sources/Support/include/Support.h")
    app = read_repo("Sources/Application/Application.swift")
    shell = read_repo("Sources/Application/UARTShell.swift")
    iterate = read_repo("scripts/netboot/net-iterate.sh")
    scheduler = read_repo("Sources/Support/kernel_scheduler.c")

    assert "kernel_genet21_selftest" in genet
    g21 = genet.split("kernel_genet21_selftest", 1)[1]
    assert "0x4EC" in g21 or "0x4ec" in g21
    assert "0x4E8" in g21 or "0x4e8" in g21
    assert "genet10_park" in g21
    assert "genet10_unpark" in g21
    assert "kernel_genet20_selftest" in g21
    assert "kernel_enter_el0_and_wait" not in g21
    assert "kernel_event_emit" not in g21
    assert "watchdog_reset_now" not in g21
    assert "M-SEARCH" not in g21
    assert "HTTP/1.0" not in g21
    assert "200000" not in genet
    assert "static unsigned long alloc_dma" not in genet
    assert "int           kernel_genet21_selftest(void);" in support
    assert 'uartPuts("runtime v125: GENET TX MIB\\n")' in app
    assert 'uartPuts("genet21 ok=")' in app
    assert "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 125" not in app
    assert "func printGenet21()" in shell
    assert 'shellBufferSliceEquals(commandStart, commandLen, "genet21")' in shell
    assert ",genet20,genet21" in shell
    assert 'grep -qa "genet21 ok=1 version=125 mib="' in iterate
    assert 'probe_shell "genet21" "^genet21 ok=1 version=125 mib="' in iterate
    assert "CONFIG.TXT" not in g21
    assert "AETHER.TMP" not in g21
    assert "ping -c 2" in iterate
    assert "SOCK_STREAM" in iterate
    assert "sched12" in iterate
    assert "kernel_genet21_selftest" not in scheduler
    after = app.split("kernel_genet12_selftest", 1)[1]
    assert "kernel_enter_el0_and_wait" not in after
    assert "kernel_socket_selftest" not in after
    assert "kernel_genet21_selftest" in after

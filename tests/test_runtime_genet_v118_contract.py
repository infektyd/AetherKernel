"""EPIC F V118: originate UDP echo after GENET. No EL0. Not a clone."""

import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_genet14_originate_udp_without_el0() -> None:
    genet = read_repo("Sources/Support/kernel_genet.c")
    support = read_repo("Sources/Support/include/Support.h")
    app = read_repo("Sources/Application/Application.swift")
    shell = read_repo("Sources/Application/UARTShell.swift")
    iterate = read_repo("scripts/netboot/net-iterate.sh")
    scheduler = read_repo("Sources/Support/kernel_scheduler.c")

    assert "kernel_genet14_selftest" in genet
    g14 = genet.split("kernel_genet14_selftest", 1)[1]
    assert "0x0a2a0001" in g14
    assert "0xA118" in g14 or "0xa118" in g14
    assert "0x11U" in g14 or "17U" in g14
    assert "genet10_park" in g14
    assert "genet10_unpark" in g14
    assert "kernel_enter_el0_and_wait" not in g14
    assert "kernel_event_emit" not in g14
    assert "watchdog_reset_now" not in g14
    assert "200000" not in genet
    assert "static unsigned long alloc_dma" not in genet
    assert "int           kernel_genet14_selftest(void);" in support
    assert 'uartPuts("runtime v118: GENET originate UDP\\n")' in app
    assert 'uartPuts("genet14 ok=")' in app
    assert "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 118" not in app
    assert "func printGenet14()" in shell
    assert 'shellBufferSliceEquals(commandStart, commandLen, "genet14")' in shell
    assert ",genet13,genet14" in shell
    assert 'grep -qa "genet14 ok=1 version=118 udp="' in iterate
    assert 'probe_shell "genet14" "^genet14 ok=1 version=118 udp="' in iterate
    assert "41240" in iterate
    assert "aether-udp-echo-v118" in iterate
    assert "ping -c 2" in iterate
    assert "SOCK_STREAM" in iterate
    assert "sched12" in iterate
    assert "kernel_genet14_selftest" not in scheduler
    after = app.split("kernel_genet12_selftest", 1)[1]
    assert "kernel_enter_el0_and_wait" not in after
    assert "kernel_socket_selftest" not in after
    assert "kernel_genet14_selftest" in after

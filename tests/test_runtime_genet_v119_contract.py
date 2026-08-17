"""EPIC F V119: originate TCP echo after GENET. No EL0. Not a clone."""

import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_genet15_originate_tcp_without_el0() -> None:
    genet = read_repo("Sources/Support/kernel_genet.c")
    support = read_repo("Sources/Support/include/Support.h")
    app = read_repo("Sources/Application/Application.swift")
    shell = read_repo("Sources/Application/UARTShell.swift")
    iterate = read_repo("scripts/netboot/net-iterate.sh")
    scheduler = read_repo("Sources/Support/kernel_scheduler.c")
    listener = read_repo("scripts/netboot/aether-tcp-echo-v119.py")

    assert "kernel_genet15_selftest" in genet
    g15 = genet.split("kernel_genet15_selftest", 1)[1]
    assert "0x0a2a0001" in g15
    assert "0xA119" in g15 or "0xa119" in g15
    assert "6U" in g15
    assert "0x12" in g15
    assert "0x18" in g15
    assert "genet10_park" in g15
    assert "genet10_unpark" in g15
    assert "kernel_enter_el0_and_wait" not in g15
    assert "kernel_event_emit" not in g15
    assert "watchdog_reset_now" not in g15
    assert "200000" not in genet
    assert "static unsigned long alloc_dma" not in genet
    assert "int           kernel_genet15_selftest(void);" in support
    assert 'uartPuts("runtime v119: GENET originate TCP\\n")' in app
    assert 'uartPuts("genet15 ok=")' in app
    assert "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 119" not in app
    assert "func printGenet15()" in shell
    assert 'shellBufferSliceEquals(commandStart, commandLen, "genet15")' in shell
    assert ",genet14,genet15" in shell
    assert 'grep -qa "genet15 ok=1 version=119 tcp="' in iterate
    assert 'probe_shell "genet15" "^genet15 ok=1 version=119 tcp="' in iterate
    assert "41241" in iterate
    assert "aether-tcp-echo-v119" in iterate
    assert "41241" in listener
    assert "SOCK_STREAM" in listener
    assert "ping -c 2" in iterate
    assert "SOCK_STREAM" in iterate
    assert "sched12" in iterate
    assert "kernel_genet15_selftest" not in scheduler
    after = app.split("kernel_genet12_selftest", 1)[1]
    assert "kernel_enter_el0_and_wait" not in after
    assert "kernel_socket_selftest" not in after
    assert "kernel_genet15_selftest" in after

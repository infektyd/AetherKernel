"""EPIC F V117: originate ARP + ICMP echo after GENET. No EL0. Not a clone."""

import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_genet13_originate_ping_without_el0() -> None:
    genet = read_repo("Sources/Support/kernel_genet.c")
    support = read_repo("Sources/Support/include/Support.h")
    app = read_repo("Sources/Application/Application.swift")
    shell = read_repo("Sources/Application/UARTShell.swift")
    iterate = read_repo("scripts/netboot/net-iterate.sh")
    scheduler = read_repo("Sources/Support/kernel_scheduler.c")

    assert "kernel_genet13_selftest" in genet
    g13 = genet.split("kernel_genet13_selftest", 1)[1]
    assert "0x0a2a0001" in g13
    assert "0xA117" in g13 or "0xa117" in g13
    assert "genet10_park" in g13
    assert "genet10_unpark" in g13
    assert "kernel_enter_el0_and_wait" not in g13
    assert "kernel_event_emit" not in g13
    assert "watchdog_reset_now" not in g13
    assert "200000" not in genet
    assert "static unsigned long alloc_dma" not in genet
    assert "int           kernel_genet13_selftest(void);" in support
    assert 'uartPuts("runtime v117: GENET originate ping\\n")' in app
    assert 'uartPuts("genet13 ok=")' in app
    assert "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 117" not in app
    assert "func printGenet13()" in shell
    assert 'shellBufferSliceEquals(commandStart, commandLen, "genet13")' in shell
    assert ",sdrm,genet13" in shell
    assert 'grep -qa "genet13 ok=1 version=117 arp="' in iterate
    assert 'probe_shell "genet13" "^genet13 ok=1 version=117 arp="' in iterate
    assert "ping -c 2" in iterate
    assert "SOCK_STREAM" in iterate
    assert "sched12" in iterate
    assert "kernel_genet13_selftest" not in scheduler
    after = app.split("kernel_genet12_selftest", 1)[1]
    assert "kernel_enter_el0_and_wait" not in after
    assert "kernel_socket_selftest" not in after
    assert "kernel_genet13_selftest" in after

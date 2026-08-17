"""EPIC F V120: originate TFTP RRQ after GENET. No EL0. Not a clone."""

import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_genet16_originate_tftp_without_el0() -> None:
    genet = read_repo("Sources/Support/kernel_genet.c")
    support = read_repo("Sources/Support/include/Support.h")
    app = read_repo("Sources/Application/Application.swift")
    shell = read_repo("Sources/Application/UARTShell.swift")
    iterate = read_repo("scripts/netboot/net-iterate.sh")
    scheduler = read_repo("Sources/Support/kernel_scheduler.c")

    assert "kernel_genet16_selftest" in genet
    g16 = genet.split("kernel_genet16_selftest", 1)[1]
    assert "0x0a2a0001" in g16
    assert "0xA120" in g16 or "0xa120" in g16
    assert "69U" in g16
    assert "octet" in g16
    assert "v120.bin" in g16
    assert "genet10_park" in g16
    assert "genet10_unpark" in g16
    assert "kernel_enter_el0_and_wait" not in g16
    assert "kernel_event_emit" not in g16
    assert "watchdog_reset_now" not in g16
    assert "200000" not in genet
    assert "static unsigned long alloc_dma" not in genet
    assert "int           kernel_genet16_selftest(void);" in support
    assert 'uartPuts("runtime v120: GENET originate TFTP\\n")' in app
    assert 'uartPuts("genet16 ok=")' in app
    assert "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 120" not in app
    assert "func printGenet16()" in shell
    assert 'shellBufferSliceEquals(commandStart, commandLen, "genet16")' in shell
    assert ",genet15,genet16" in shell
    assert 'grep -qa "genet16 ok=1 version=120 tftp="' in iterate
    assert 'probe_shell "genet16" "^genet16 ok=1 version=120 tftp="' in iterate
    assert "v120.bin" in iterate
    assert "CONFIG.TXT" not in g16
    assert "AETHER.TMP" not in g16
    assert "ping -c 2" in iterate
    assert "SOCK_STREAM" in iterate
    assert "sched12" in iterate
    assert "kernel_genet16_selftest" not in scheduler
    after = app.split("kernel_genet12_selftest", 1)[1]
    assert "kernel_enter_el0_and_wait" not in after
    assert "kernel_socket_selftest" not in after
    assert "kernel_genet16_selftest" in after

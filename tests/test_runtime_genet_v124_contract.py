"""EPIC F V124: originate SSDP M-SEARCH after GENET. No EL0. Not a clone."""

import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_genet20_originate_ssdp_without_el0() -> None:
    genet = read_repo("Sources/Support/kernel_genet.c")
    support = read_repo("Sources/Support/include/Support.h")
    app = read_repo("Sources/Application/Application.swift")
    shell = read_repo("Sources/Application/UARTShell.swift")
    iterate = read_repo("scripts/netboot/net-iterate.sh")
    scheduler = read_repo("Sources/Support/kernel_scheduler.c")
    listener = read_repo("scripts/netboot/aether-ssdp-v124.py")

    assert "kernel_genet20_selftest" in genet
    g20 = genet.split("kernel_genet20_selftest", 1)[1]
    assert "0x0a2a0001" in g20
    assert "0xA124" in g20 or "0xa124" in g20
    assert "M-SEARCH" in g20
    assert "ssdp:discover" in g20
    assert "urn:aether:device:v124" in g20
    assert "genet10_park" in g20
    assert "genet10_unpark" in g20
    assert "kernel_enter_el0_and_wait" not in g20
    assert "kernel_event_emit" not in g20
    assert "watchdog_reset_now" not in g20
    assert "200000" not in genet
    assert "static unsigned long alloc_dma" not in genet
    assert "int           kernel_genet20_selftest(void);" in support
    assert 'uartPuts("runtime v124: GENET originate SSDP\\n")' in app
    assert 'uartPuts("genet20 ok=")' in app
    assert "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 124" not in app
    assert "func printGenet20()" in shell
    assert 'shellBufferSliceEquals(commandStart, commandLen, "genet20")' in shell
    assert ",genet19,genet20" in shell
    assert 'grep -qa "genet20 ok=1 version=124 ssdp="' in iterate
    assert 'probe_shell "genet20" "^genet20 ok=1 version=124 ssdp="' in iterate
    assert "aether-ssdp-v124" in iterate
    assert "41252" in listener or "0xA124" in listener
    assert "M-SEARCH" in listener
    assert "CONFIG.TXT" not in g20
    assert "AETHER.TMP" not in g20
    assert "ping -c 2" in iterate
    assert "SOCK_STREAM" in iterate
    assert "sched12" in iterate
    assert "kernel_genet20_selftest" not in scheduler
    after = app.split("kernel_genet12_selftest", 1)[1]
    assert "kernel_enter_el0_and_wait" not in after
    assert "kernel_socket_selftest" not in after
    assert "kernel_genet20_selftest" in after

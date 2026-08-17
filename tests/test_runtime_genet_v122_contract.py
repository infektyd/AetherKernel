"""EPIC F V122: originate HTTP GET after GENET. No EL0. Not a clone."""

import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_genet18_originate_http_without_el0() -> None:
    genet = read_repo("Sources/Support/kernel_genet.c")
    support = read_repo("Sources/Support/include/Support.h")
    app = read_repo("Sources/Application/Application.swift")
    shell = read_repo("Sources/Application/UARTShell.swift")
    iterate = read_repo("scripts/netboot/net-iterate.sh")
    scheduler = read_repo("Sources/Support/kernel_scheduler.c")
    listener = read_repo("scripts/netboot/aether-http-v122.py")

    assert "kernel_genet18_selftest" in genet
    g18 = genet.split("kernel_genet18_selftest", 1)[1]
    assert "0x0a2a0001" in g18
    assert "0xA122" in g18 or "0xa122" in g18
    assert "GET /aether/v122.txt" in g18
    assert "HTTP/1.0" in g18
    assert "genet10_park" in g18
    assert "genet10_unpark" in g18
    assert "kernel_enter_el0_and_wait" not in g18
    assert "kernel_event_emit" not in g18
    assert "watchdog_reset_now" not in g18
    assert "200000" not in genet
    assert "static unsigned long alloc_dma" not in genet
    assert "int           kernel_genet18_selftest(void);" in support
    assert 'uartPuts("runtime v122: GENET originate HTTP\\n")' in app
    assert 'uartPuts("genet18 ok=")' in app
    assert "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 122" not in app
    assert "func printGenet18()" in shell
    assert 'shellBufferSliceEquals(commandStart, commandLen, "genet18")' in shell
    assert ",genet17,genet18" in shell
    assert 'grep -qa "genet18 ok=1 version=122 http="' in iterate
    assert 'probe_shell "genet18" "^genet18 ok=1 version=122 http="' in iterate
    assert "aether-http-v122" in iterate
    assert "v122.txt" in listener
    assert "41250" in listener or "0xA122" in listener
    assert "CONFIG.TXT" not in g18
    assert "AETHER.TMP" not in g18
    assert "ping -c 2" in iterate
    assert "SOCK_STREAM" in iterate
    assert "sched12" in iterate
    assert "kernel_genet18_selftest" not in scheduler
    after = app.split("kernel_genet12_selftest", 1)[1]
    assert "kernel_enter_el0_and_wait" not in after
    assert "kernel_socket_selftest" not in after
    assert "kernel_genet18_selftest" in after

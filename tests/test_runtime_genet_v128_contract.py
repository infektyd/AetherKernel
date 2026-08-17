"""EPIC F V128: GENET UMAC station filter, PROMISC off. No EL0."""

import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def _selftest_body(genet: str, name: str) -> str:
    key = f"int {name}(void) {{"
    chunk = genet.split(key, 1)[1]
    nxt = chunk.find("\nint kernel_genet")
    return chunk if nxt < 0 else chunk[:nxt]


def test_genet24_umac_station_filter_without_el0() -> None:
    genet = read_repo("Sources/Support/kernel_genet.c")
    support = read_repo("Sources/Support/include/Support.h")
    app = read_repo("Sources/Application/Application.swift")
    shell = read_repo("Sources/Application/UARTShell.swift")
    iterate = read_repo("scripts/netboot/net-iterate.sh")
    scheduler = read_repo("Sources/Support/kernel_scheduler.c")

    assert "kernel_genet24_selftest" in genet
    g24 = genet.split("kernel_genet24_selftest", 1)[1]
    body24 = _selftest_body(genet, "kernel_genet24_selftest")
    assert "CMD_PROMISC" in g24
    assert "UMAC_MAC0" in g24
    assert "UMAC_MAC1" in g24
    assert "genet_write_arp" in g24
    assert "genet10_park" in g24
    assert "genet10_unpark" in g24
    assert "kernel_genet13_selftest" in body24
    assert "kernel_genet15_selftest" not in g24
    assert "kernel_genet23_selftest" not in g24
    assert "GIC" not in g24
    assert "kernel_enter_el0_and_wait" not in g24
    assert "kernel_event_emit" not in g24
    assert "watchdog_reset_now" not in g24
    assert "M-SEARCH" not in g24
    assert "HTTP/1.0" not in g24
    assert "200000" not in genet
    assert "static unsigned long alloc_dma" not in genet
    assert "int           kernel_genet24_selftest(void);" in support
    assert 'uartPuts("runtime v128: GENET UMAC station filter\\n")' in app
    assert 'uartPuts("genet24 ok=")' in app
    assert "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 128" not in app
    assert "func printGenet24()" in shell
    assert 'shellBufferSliceEquals(commandStart, commandLen, "genet24")' in shell
    assert ",genet23,genet24" in shell
    assert 'grep -qa "genet24 ok=1 version=128 filter="' in iterate
    assert 'probe_shell "genet24" "^genet24 ok=1 version=128 filter="' in iterate
    assert "CONFIG.TXT" not in g24
    assert "AETHER.TMP" not in g24
    assert "ping -c 2" in iterate
    assert "SOCK_STREAM" in iterate
    assert "sched12" in iterate
    assert "kernel_genet24_selftest" not in scheduler
    after = app.split("kernel_genet12_selftest", 1)[1]
    assert "kernel_enter_el0_and_wait" not in after
    assert "kernel_socket_selftest" not in after
    assert "kernel_genet24_selftest" in after

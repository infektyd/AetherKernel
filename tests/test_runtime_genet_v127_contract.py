"""EPIC F V127: GENET INTRL2 TXDMA_DONE after one frame. No EL0."""

import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def _selftest_body(genet: str, name: str) -> str:
    key = f"int {name}(void) {{"
    chunk = genet.split(key, 1)[1]
    nxt = chunk.find("\nint kernel_genet")
    return chunk if nxt < 0 else chunk[:nxt]


def test_genet23_intrl2_txdma_done_without_el0() -> None:
    genet = read_repo("Sources/Support/kernel_genet.c")
    support = read_repo("Sources/Support/include/Support.h")
    app = read_repo("Sources/Application/Application.swift")
    shell = read_repo("Sources/Application/UARTShell.swift")
    iterate = read_repo("scripts/netboot/net-iterate.sh")
    scheduler = read_repo("Sources/Support/kernel_scheduler.c")

    assert "kernel_genet23_selftest" in genet
    g23 = genet.split("kernel_genet23_selftest", 1)[1]
    body23 = _selftest_body(genet, "kernel_genet23_selftest")
    assert "0x0200" in g23
    assert "0x10000" in g23 or "(1U << 16)" in g23 or "(1u << 16)" in g23
    assert "0x88B5" in g23 or "0x88b5" in g23
    assert "DMA_MBUF_DONE" in g23
    assert "genet10_park" in g23
    assert "genet10_unpark" in g23
    assert "kernel_genet13_selftest" in body23
    assert "kernel_genet15_selftest" not in g23
    assert "kernel_genet22_selftest" not in g23
    assert "kernel_enter_el0_and_wait" not in g23
    assert "kernel_event_emit" not in g23
    assert "watchdog_reset_now" not in g23
    assert "M-SEARCH" not in g23
    assert "HTTP/1.0" not in g23
    assert "200000" not in genet
    assert "static unsigned long alloc_dma" not in genet
    assert "int           kernel_genet23_selftest(void);" in support
    assert 'uartPuts("runtime v127: GENET INTRL2 TX done\\n")' in app
    assert 'uartPuts("genet23 ok=")' in app
    assert "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 127" not in app
    assert "func printGenet23()" in shell
    assert 'shellBufferSliceEquals(commandStart, commandLen, "genet23")' in shell
    assert ",genet22,genet23" in shell
    assert 'grep -qa "genet23 ok=1 version=127 irq="' in iterate
    assert 'probe_shell "genet23" "^genet23 ok=1 version=127 irq="' in iterate
    assert "CONFIG.TXT" not in g23
    assert "AETHER.TMP" not in g23
    assert "ping -c 2" in iterate
    assert "SOCK_STREAM" in iterate
    assert "sched12" in iterate
    assert "kernel_genet23_selftest" not in scheduler
    after = app.split("kernel_genet12_selftest", 1)[1]
    assert "kernel_enter_el0_and_wait" not in after
    assert "kernel_socket_selftest" not in after
    assert "kernel_genet23_selftest" in after

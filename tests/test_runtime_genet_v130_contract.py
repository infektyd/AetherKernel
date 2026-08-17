"""EPIC F V130: GENET UMAC_MAX_FRAME_LEN write+readback. No EL0."""

import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def _selftest_body(genet: str, name: str) -> str:
    key = f"int {name}(void) {{"
    chunk = genet.split(key, 1)[1]
    nxt = chunk.find("\nint kernel_genet")
    return chunk if nxt < 0 else chunk[:nxt]


def test_genet26_umac_max_frame_len_without_el0() -> None:
    genet = read_repo("Sources/Support/kernel_genet.c")
    support = read_repo("Sources/Support/include/Support.h")
    app = read_repo("Sources/Application/Application.swift")
    shell = read_repo("Sources/Application/UARTShell.swift")
    iterate = read_repo("scripts/netboot/net-iterate.sh")
    scheduler = read_repo("Sources/Support/kernel_scheduler.c")
    roadmap = read_repo("docs/ROADMAP.md")

    assert "kernel_genet26_selftest" in genet
    g26 = genet.split("kernel_genet26_selftest", 1)[1]
    body26 = _selftest_body(genet, "kernel_genet26_selftest")
    assert "UMAC_MAX_FRAME_LEN" in body26
    assert "1518" in body26
    assert "1536" in body26
    assert "genet_wr32" in body26
    assert "kernel_genet_selftest" in body26
    assert "kernel_genet13_selftest" not in body26
    assert "genet_mdio_read" not in body26
    assert "MII_BMSR" not in body26
    assert "genet10_unpark" not in body26
    assert "genet10_park" not in body26
    assert "GIC" not in g26
    assert "kernel_enter_el0_and_wait" not in g26
    assert "kernel_event_emit" not in g26
    assert "watchdog_reset_now" not in g26
    assert "M-SEARCH" not in g26
    assert "HTTP/1.0" not in g26
    assert "200000" not in genet
    assert "static unsigned long alloc_dma" not in genet
    assert "int           kernel_genet26_selftest(void);" in support
    assert 'uartPuts("runtime v130: GENET UMAC max frame\\n")' in app
    assert 'uartPuts("genet26 ok=")' in app
    assert "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 130" not in app
    assert "func printGenet26()" in shell
    assert 'shellBufferSliceEquals(commandStart, commandLen, "genet26")' in shell
    assert ",genet25,genet26" in shell
    assert 'grep -qa "genet26 ok=1 version=130 len="' in iterate
    assert 'probe_shell "genet26" "^genet26 ok=1 version=130 len="' in iterate
    assert "V130 UMAC max frame" in roadmap
    assert "CONFIG.TXT" not in g26
    assert "AETHER.TMP" not in g26
    assert "ping -c 2" in iterate
    assert "SOCK_STREAM" in iterate
    assert "sched12" in iterate
    assert "kernel_genet26_selftest" not in scheduler
    after = app.split("kernel_genet12_selftest", 1)[1]
    assert "kernel_enter_el0_and_wait" not in after
    assert "kernel_socket_selftest" not in after
    assert "kernel_genet26_selftest" in after

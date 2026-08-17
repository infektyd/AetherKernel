"""EPIC F V132: GENET UMAC MDF perfect-match filter program+readback. No EL0."""

import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def _selftest_body(genet: str, name: str) -> str:
    key = f"int {name}(void) {{"
    chunk = genet.split(key, 1)[1]
    nxt = chunk.find("\nint kernel_genet")
    return chunk if nxt < 0 else chunk[:nxt]


def test_genet28_umac_mdf_without_el0() -> None:
    genet = read_repo("Sources/Support/kernel_genet.c")
    support = read_repo("Sources/Support/include/Support.h")
    app = read_repo("Sources/Application/Application.swift")
    shell = read_repo("Sources/Application/UARTShell.swift")
    iterate = read_repo("scripts/netboot/net-iterate.sh")
    scheduler = read_repo("Sources/Support/kernel_scheduler.c")
    roadmap = read_repo("docs/ROADMAP.md")

    assert "kernel_genet28_selftest" in genet
    g28 = genet.split("kernel_genet28_selftest", 1)[1]
    body28 = _selftest_body(genet, "kernel_genet28_selftest")
    assert "UMAC_MDF_CTRL" in genet
    assert "UMAC_MDF_ADDR" in genet
    assert "UMAC_MDF_CTRL" in body28
    assert "UMAC_MDF_ADDR" in body28
    assert "kernel_genet_selftest" in body28
    assert "kernel_genet3_mac" in body28
    assert "UMAC_MAX_FRAME_LEN" not in body28
    assert "CMD_PROMISC" not in body28
    assert "MII_BMSR" not in body28
    assert "BMSR_LSTATUS" not in body28
    assert "MII_BMCR" not in body28
    assert "kernel_genet13_selftest" not in body28
    assert "genet10_unpark" not in body28
    assert "genet10_park" not in body28
    assert "GIC" not in g28
    assert "kernel_enter_el0_and_wait" not in g28
    assert "kernel_event_emit" not in g28
    assert "watchdog_reset_now" not in g28
    assert "M-SEARCH" not in g28
    assert "HTTP/1.0" not in g28
    assert "200000" not in genet
    assert "static unsigned long alloc_dma" not in genet
    assert "int           kernel_genet28_selftest(void);" in support
    assert 'uartPuts("runtime v132: GENET UMAC MDF\\n")' in app
    assert 'uartPuts("genet28 ok=")' in app
    assert "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 132" not in app
    assert "func printGenet28()" in shell
    assert 'shellBufferSliceEquals(commandStart, commandLen, "genet28")' in shell
    assert ",genet27,genet28" in shell
    assert 'grep -qa "genet28 ok=1 version=132 mdf="' in iterate
    assert 'probe_shell "genet28" "^genet28 ok=1 version=132 mdf="' in iterate
    assert "V132 UMAC MDF" in roadmap
    assert "CONFIG.TXT" not in g28
    assert "AETHER.TMP" not in g28
    assert "ping -c 2" in iterate
    assert "SOCK_STREAM" in iterate
    assert "sched12" in iterate
    assert "kernel_genet28_selftest" not in scheduler
    after = app.split("kernel_genet12_selftest", 1)[1]
    assert "kernel_enter_el0_and_wait" not in after
    assert "kernel_socket_selftest" not in after
    assert "kernel_genet28_selftest" in after

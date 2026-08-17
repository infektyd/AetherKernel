"""EPIC F V129: GENET MDIO PHY identifier (PHYSID1+PHYSID2). No EL0."""

import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def _selftest_body(genet: str, name: str) -> str:
    key = f"int {name}(void) {{"
    chunk = genet.split(key, 1)[1]
    nxt = chunk.find("\nint kernel_genet")
    return chunk if nxt < 0 else chunk[:nxt]


def test_genet25_mdio_phy_id_without_el0() -> None:
    genet = read_repo("Sources/Support/kernel_genet.c")
    support = read_repo("Sources/Support/include/Support.h")
    app = read_repo("Sources/Application/Application.swift")
    shell = read_repo("Sources/Application/UARTShell.swift")
    iterate = read_repo("scripts/netboot/net-iterate.sh")
    scheduler = read_repo("Sources/Support/kernel_scheduler.c")
    roadmap = read_repo("docs/ROADMAP.md")

    assert "kernel_genet25_selftest" in genet
    g25 = genet.split("kernel_genet25_selftest", 1)[1]
    body25 = _selftest_body(genet, "kernel_genet25_selftest")
    assert "MII_PHYSID1" in genet
    assert "MII_PHYSID2" in genet
    assert "genet_mdio_read" in body25
    assert "0xFFFF" in body25
    assert "kernel_genet_selftest" in body25
    assert "kernel_genet13_selftest" not in body25
    assert "genet10_unpark" not in body25
    assert "genet10_park" not in body25
    assert "GIC" not in g25
    assert "kernel_enter_el0_and_wait" not in g25
    assert "kernel_event_emit" not in g25
    assert "watchdog_reset_now" not in g25
    assert "M-SEARCH" not in g25
    assert "HTTP/1.0" not in g25
    assert "0x600d84a2" not in genet
    assert "200000" not in genet
    assert "static unsigned long alloc_dma" not in genet
    assert "int           kernel_genet25_selftest(void);" in support
    assert 'uartPuts("runtime v129: GENET MDIO PHY ID\\n")' in app
    assert 'uartPuts("genet25 ok=")' in app
    assert "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 129" not in app
    assert "func printGenet25()" in shell
    assert 'shellBufferSliceEquals(commandStart, commandLen, "genet25")' in shell
    assert ",genet24,genet25" in shell
    assert 'grep -qa "genet25 ok=1 version=129 phy="' in iterate
    assert 'probe_shell "genet25" "^genet25 ok=1 version=129 phy="' in iterate
    assert "V129 MDIO PHY identifier" in roadmap
    assert "CONFIG.TXT" not in g25
    assert "AETHER.TMP" not in g25
    assert "ping -c 2" in iterate
    assert "SOCK_STREAM" in iterate
    assert "sched12" in iterate
    assert "kernel_genet25_selftest" not in scheduler
    after = app.split("kernel_genet12_selftest", 1)[1]
    assert "kernel_enter_el0_and_wait" not in after
    assert "kernel_socket_selftest" not in after
    assert "kernel_genet25_selftest" in after

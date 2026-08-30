"""EPIC F V136: GENET UMAC TX MIB reset. No EL0."""

import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def _selftest_body(genet: str, name: str) -> str:
    key = f"int {name}(void) {{"
    chunk = genet.split(key, 1)[1]
    nxt = chunk.find("\nint kernel_genet")
    return chunk if nxt < 0 else chunk[:nxt]


def test_genet32_umac_mib_reset_without_el0() -> None:
    genet = read_repo("Sources/Support/kernel_genet.c")
    support = read_repo("Sources/Support/include/Support.h")
    app = read_repo("Sources/Application/Application.swift")
    shell = read_repo("Sources/Application/UARTShell.swift")
    iterate = read_repo("scripts/netboot/net-iterate.sh")
    scheduler = read_repo("Sources/Support/kernel_scheduler.c")
    roadmap = read_repo("docs/ROADMAP.md")

    assert "kernel_genet32_selftest" in genet
    g32 = genet.split("kernel_genet32_selftest", 1)[1]
    body32 = _selftest_body(genet, "kernel_genet32_selftest")
    assert "UMAC_MIB_CTRL" in genet
    assert "UMAC_MIB_RESET_TX" in genet
    assert "UMAC_MIB_CTRL" in body32
    assert "UMAC_MIB_RESET_TX" in body32
    assert "UMAC_MIB_TX_POK" in body32
    assert "UMAC_MIB_TX_BYTES" in body32
    assert "kernel_genet_selftest" in body32
    assert "HFB_FLT_ENABLE" not in body32
    assert "HFB_FLT0_EN" not in body32
    assert "RBUF_HFB_EN" not in body32
    assert "TBUF_ENERGY_CTRL" not in body32
    assert "TBUF_EEE_EN" not in body32
    assert "RBUF_CHK_CTRL" not in body32
    assert "RBUF_RXCHK_EN" not in body32
    assert "UMAC_MDF_CTRL" not in body32
    assert "UMAC_MAX_FRAME_LEN" not in body32
    assert "CMD_PROMISC" not in body32
    assert "DMA_TX_DO_CSUM" not in body32
    assert "RBUF_64B_EN" not in body32
    assert "MII_BMSR" not in body32
    assert "BMSR_LSTATUS" not in body32
    assert "MII_BMCR" not in body32
    assert "kernel_genet13_selftest" not in body32
    assert "kernel_genet21_selftest" not in body32
    assert "genet10_unpark" not in body32
    assert "genet10_park" not in body32
    assert "GIC" not in g32
    assert "kernel_enter_el0_and_wait" not in g32
    assert "kernel_event_emit" not in g32
    assert "watchdog_reset_now" not in g32
    assert "M-SEARCH" not in g32
    assert "HTTP/1.0" not in g32
    assert "200000" not in genet
    assert "static unsigned long alloc_dma" not in genet
    assert "int           kernel_genet32_selftest(void);" in support
    assert 'uartPuts("runtime v136: GENET UMAC MIB reset\\n")' in app
    assert 'uartPuts("genet32 ok=")' in app
    assert "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 136" not in app
    assert "func printGenet32()" in shell
    assert 'shellBufferSliceEquals(commandStart, commandLen, "genet32")' in shell
    assert ",genet31,genet32" in shell
    assert 'grep -qa "genet32 ok=1 version=136 rst="' in iterate
    assert 'probe_shell "genet32" "^genet32 ok=1 version=136 rst="' in iterate
    assert "V136 UMAC TX MIB reset" in roadmap
    assert "CONFIG.TXT" not in g32
    assert "AETHER.TMP" not in g32
    assert "ping -c 2" in iterate
    assert "SOCK_STREAM" in iterate
    assert "sched12" in iterate
    assert "kernel_genet32_selftest" not in scheduler
    after = app.split("kernel_genet12_selftest", 1)[1]
    assert "kernel_enter_el0_and_wait" not in after
    assert "kernel_socket_selftest" not in after
    assert "kernel_genet32_selftest" in after

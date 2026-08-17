"""EPIC F V135: GENET HFB enable program+readback. No EL0."""

import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def _selftest_body(genet: str, name: str) -> str:
    key = f"int {name}(void) {{"
    chunk = genet.split(key, 1)[1]
    nxt = chunk.find("\nint kernel_genet")
    return chunk if nxt < 0 else chunk[:nxt]


def test_genet31_hfb_enable_without_el0() -> None:
    genet = read_repo("Sources/Support/kernel_genet.c")
    support = read_repo("Sources/Support/include/Support.h")
    app = read_repo("Sources/Application/Application.swift")
    shell = read_repo("Sources/Application/UARTShell.swift")
    iterate = read_repo("scripts/netboot/net-iterate.sh")
    scheduler = read_repo("Sources/Support/kernel_scheduler.c")
    roadmap = read_repo("docs/ROADMAP.md")

    assert "kernel_genet31_selftest" in genet
    g31 = genet.split("kernel_genet31_selftest", 1)[1]
    body31 = _selftest_body(genet, "kernel_genet31_selftest")
    assert "HFB_FLT_ENABLE" in genet
    assert "HFB_FLT_ENABLE" in body31
    assert "HFB_REG_OFF" in body31
    assert "HFB_FLT0_EN" in body31
    assert "kernel_genet_selftest" in body31
    assert "RBUF_HFB_EN" not in body31
    assert "HFB_RAM_PATTERN" not in body31
    assert "0xA5F0B1C3" not in body31
    assert "TBUF_ENERGY_CTRL" not in body31
    assert "TBUF_EEE_EN" not in body31
    assert "RBUF_CHK_CTRL" not in body31
    assert "RBUF_RXCHK_EN" not in body31
    assert "UMAC_MDF_CTRL" not in body31
    assert "UMAC_MAX_FRAME_LEN" not in body31
    assert "CMD_PROMISC" not in body31
    assert "DMA_TX_DO_CSUM" not in body31
    assert "RBUF_64B_EN" not in body31
    assert "MII_BMSR" not in body31
    assert "BMSR_LSTATUS" not in body31
    assert "MII_BMCR" not in body31
    assert "kernel_genet13_selftest" not in body31
    assert "genet10_unpark" not in body31
    assert "genet10_park" not in body31
    assert "GIC" not in g31
    assert "kernel_enter_el0_and_wait" not in g31
    assert "kernel_event_emit" not in g31
    assert "watchdog_reset_now" not in g31
    assert "M-SEARCH" not in g31
    assert "HTTP/1.0" not in g31
    assert "200000" not in genet
    assert "static unsigned long alloc_dma" not in genet
    assert "int           kernel_genet31_selftest(void);" in support
    assert 'uartPuts("runtime v135: GENET HFB enable\\n")' in app
    assert 'uartPuts("genet31 ok=")' in app
    assert "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 135" not in app
    assert "func printGenet31()" in shell
    assert 'shellBufferSliceEquals(commandStart, commandLen, "genet31")' in shell
    assert ",genet30,genet31" in shell
    assert 'grep -qa "genet31 ok=1 version=135 hfb="' in iterate
    assert 'probe_shell "genet31" "^genet31 ok=1 version=135 hfb="' in iterate
    assert "V135 HFB" in roadmap
    assert "CONFIG.TXT" not in g31
    assert "AETHER.TMP" not in g31
    assert "ping -c 2" in iterate
    assert "SOCK_STREAM" in iterate
    assert "sched12" in iterate
    assert "kernel_genet31_selftest" not in scheduler
    after = app.split("kernel_genet12_selftest", 1)[1]
    assert "kernel_enter_el0_and_wait" not in after
    assert "kernel_socket_selftest" not in after
    assert "kernel_genet31_selftest" in after

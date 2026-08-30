"""EPIC F V138: GENET EXT RGMII OOB writeback. No EL0."""

import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def _selftest_body(genet: str, name: str) -> str:
    key = f"int {name}(void) {{"
    chunk = genet.split(key, 1)[1]
    nxt = chunk.find("\nint kernel_genet")
    return chunk if nxt < 0 else chunk[:nxt]


def test_genet34_ext_rgmii_oob_without_el0() -> None:
    genet = read_repo("Sources/Support/kernel_genet.c")
    support = read_repo("Sources/Support/include/Support.h")
    app = read_repo("Sources/Application/Application.swift")
    shell = read_repo("Sources/Application/UARTShell.swift")
    iterate = read_repo("scripts/netboot/net-iterate.sh")
    scheduler = read_repo("Sources/Support/kernel_scheduler.c")
    roadmap = read_repo("docs/ROADMAP.md")

    assert "kernel_genet34_selftest" in genet
    g34 = genet.split("kernel_genet34_selftest", 1)[1]
    body34 = _selftest_body(genet, "kernel_genet34_selftest")
    assert "EXT_RGMII_OOB_CTRL" in genet
    assert "OOB_DISABLE" in genet
    assert "EXT_RGMII_OOB_CTRL" in body34
    assert "OOB_DISABLE" in body34
    assert "kernel_genet33_selftest" in body34
    assert "CMD_LCL_LOOP_EN" not in genet
    assert "SYS_RBUF_FLUSH_CTRL" not in body34
    assert "SYS_TBUF_FLUSH_CTRL" not in body34
    assert "UMAC_MIB_CTRL" not in body34
    assert "UMAC_MIB_RESET_TX" not in body34
    assert "UMAC_MIB_RESET_RX" not in body34
    assert "HFB_FLT_ENABLE" not in body34
    assert "HFB_FLT0_EN" not in body34
    assert "RBUF_HFB_EN" not in body34
    assert "TBUF_ENERGY_CTRL" not in body34
    assert "TBUF_EEE_EN" not in body34
    assert "RBUF_CHK_CTRL" not in body34
    assert "RBUF_RXCHK_EN" not in body34
    assert "UMAC_MDF_CTRL" not in body34
    assert "UMAC_MAX_FRAME_LEN" not in body34
    assert "DMA_TX_DO_CSUM" not in body34
    assert "RBUF_64B_EN" not in body34
    assert "MII_BMSR" not in body34
    assert "BMSR_LSTATUS" not in body34
    assert "MII_BMCR" not in body34
    assert "RGMII_LINK" not in body34
    assert "kernel_genet13_selftest" not in body34
    assert "kernel_genet21_selftest" not in body34
    assert "genet10_unpark" not in body34
    assert "genet10_park" not in body34
    assert "GIC" not in g34
    assert "kernel_enter_el0_and_wait" not in g34
    assert "kernel_event_emit" not in g34
    assert "watchdog_reset_now" not in g34
    assert "M-SEARCH" not in g34
    assert "HTTP/1.0" not in g34
    assert "200000" not in genet
    assert "static unsigned long alloc_dma" not in genet
    assert "int           kernel_genet34_selftest(void);" in support
    assert 'uartPuts("runtime v138: GENET EXT OOB\\n")' in app
    assert 'uartPuts("genet34 ok=")' in app
    assert "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 138" not in app
    assert "func printGenet34()" in shell
    assert 'shellBufferSliceEquals(commandStart, commandLen, "genet34")' in shell
    assert ",genet33,genet34" in shell
    assert 'grep -qa "genet34 ok=1 version=138 oob="' in iterate
    assert 'probe_shell "genet34" "^genet34 ok=1 version=138 oob="' in iterate
    assert "V138 EXT RGMII OOB" in roadmap
    assert "CONFIG.TXT" not in g34
    assert "AETHER.TMP" not in g34
    assert "ping -c 2" in iterate
    assert "SOCK_STREAM" in iterate
    assert "sched12" in iterate
    assert "kernel_genet34_selftest" not in scheduler
    after = app.split("kernel_genet12_selftest", 1)[1]
    assert "kernel_enter_el0_and_wait" not in after
    assert "kernel_socket_selftest" not in after
    assert "kernel_genet34_selftest" in after

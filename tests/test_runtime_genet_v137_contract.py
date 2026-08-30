"""EPIC F V137: GENET SYS RBUF/TBUF flush. No EL0."""

import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def _selftest_body(genet: str, name: str) -> str:
    key = f"int {name}(void) {{"
    chunk = genet.split(key, 1)[1]
    nxt = chunk.find("\nint kernel_genet")
    return chunk if nxt < 0 else chunk[:nxt]


def test_genet33_sys_rbuf_tbuf_flush_without_el0() -> None:
    genet = read_repo("Sources/Support/kernel_genet.c")
    support = read_repo("Sources/Support/include/Support.h")
    app = read_repo("Sources/Application/Application.swift")
    shell = read_repo("Sources/Application/UARTShell.swift")
    iterate = read_repo("scripts/netboot/net-iterate.sh")
    scheduler = read_repo("Sources/Support/kernel_scheduler.c")
    roadmap = read_repo("docs/ROADMAP.md")

    assert "kernel_genet33_selftest" in genet
    g33 = genet.split("kernel_genet33_selftest", 1)[1]
    body33 = _selftest_body(genet, "kernel_genet33_selftest")
    assert "SYS_RBUF_FLUSH_CTRL" in genet
    assert "SYS_TBUF_FLUSH_CTRL" in genet
    assert "SYS_RBUF_FLUSH_CTRL" in body33
    assert "SYS_TBUF_FLUSH_CTRL" in body33
    assert "kernel_genet32_selftest" in body33
    assert "CMD_LCL_LOOP_EN" not in genet
    assert "UMAC_MIB_CTRL" not in body33
    assert "UMAC_MIB_RESET_TX" not in body33
    assert "UMAC_MIB_RESET_RX" not in body33
    assert "HFB_FLT_ENABLE" not in body33
    assert "HFB_FLT0_EN" not in body33
    assert "RBUF_HFB_EN" not in body33
    assert "TBUF_ENERGY_CTRL" not in body33
    assert "TBUF_EEE_EN" not in body33
    assert "RBUF_CHK_CTRL" not in body33
    assert "RBUF_RXCHK_EN" not in body33
    assert "UMAC_MDF_CTRL" not in body33
    assert "UMAC_MAX_FRAME_LEN" not in body33
    assert "DMA_TX_DO_CSUM" not in body33
    assert "RBUF_64B_EN" not in body33
    assert "MII_BMSR" not in body33
    assert "BMSR_LSTATUS" not in body33
    assert "MII_BMCR" not in body33
    assert "kernel_genet13_selftest" not in body33
    assert "kernel_genet21_selftest" not in body33
    assert "genet10_unpark" not in body33
    assert "genet10_park" not in body33
    assert "GIC" not in g33
    assert "kernel_enter_el0_and_wait" not in g33
    assert "kernel_event_emit" not in g33
    assert "watchdog_reset_now" not in g33
    assert "M-SEARCH" not in g33
    assert "HTTP/1.0" not in g33
    assert "200000" not in genet
    assert "static unsigned long alloc_dma" not in genet
    assert "int           kernel_genet33_selftest(void);" in support
    assert 'uartPuts("runtime v137: GENET SYS flush\\n")' in app
    assert 'uartPuts("genet33 ok=")' in app
    assert "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 137" not in app
    assert "func printGenet33()" in shell
    assert 'shellBufferSliceEquals(commandStart, commandLen, "genet33")' in shell
    assert ",genet32,genet33" in shell
    assert 'grep -qa "genet33 ok=1 version=137 rflush="' in iterate
    assert 'probe_shell "genet33" "^genet33 ok=1 version=137 rflush="' in iterate
    assert "V137 SYS RBUF/TBUF flush" in roadmap
    assert "CONFIG.TXT" not in g33
    assert "AETHER.TMP" not in g33
    assert "ping -c 2" in iterate
    assert "SOCK_STREAM" in iterate
    assert "sched12" in iterate
    assert "kernel_genet33_selftest" not in scheduler
    after = app.split("kernel_genet12_selftest", 1)[1]
    assert "kernel_enter_el0_and_wait" not in after
    assert "kernel_socket_selftest" not in after
    assert "kernel_genet33_selftest" in after

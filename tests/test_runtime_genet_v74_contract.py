"""EPIC F V74: Linux GENET ring-16 geometry + leftover RBUF reset. Honest TX CONS."""

import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_genet7_matches_linux_ring16_and_reports_cons() -> None:
    genet = read_repo("Sources/Support/kernel_genet.c")
    support = read_repo("Sources/Support/include/Support.h")
    app = read_repo("Sources/Application/Application.swift")
    shell = read_repo("Sources/Application/UARTShell.swift")
    iterate = read_repo("scripts/netboot/net-iterate.sh")
    scheduler = read_repo("Sources/Support/kernel_scheduler.c")

    genet6 = genet.split("kernel_genet7_selftest")[0]
    genet7 = genet.split("kernel_genet7_selftest", 1)[1].split("kernel_genet8_selftest")[0]

    assert "int           kernel_genet7_selftest(void);" in support
    assert "RX_Q16_N = 256" in genet7
    assert "TX_Q16_START = 128" in genet7
    assert "UMAC_TX_FLUSH" in genet7
    assert "DMA_SCB_BURST" in genet7
    assert "tdma_ring16(RR_CONS_INDEX)" in genet7
    assert "200000" not in genet
    assert 'uartPuts("runtime v74: GENET Linux ring-16 and TX CONS\\n")' in app
    assert 'uartPuts("genet7 ok=")' in app
    assert "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 74" not in app
    assert "func printGenet6()" in shell
    assert "func printGenet7()" in shell
    assert 'shellBufferSliceEquals(commandStart, commandLen, "genet7")' in shell
    assert ",genet5,genet6,genet7" in shell
    assert 'grep -qa "genet7 ok=1 version=74 ring=.* tx=.* cons=.* prod=.* frames="' in iterate
    assert 'probe_shell "genet7" "^genet7 ok=1 version=74 ring=1 tx=.* cons=.* prod=.* frames="' in iterate
    assert 'grep -qa "genet6 ok=1 version=73 mac=.* tx=.* frames="' in iterate
    assert "sched12" in iterate
    assert "kernel_genet7_selftest" not in scheduler
    assert "0x400000000" not in genet
    assert "static unsigned long alloc_dma" not in genet
    assert "DMA_TO_BUS" not in genet
    assert "TCP" not in genet7
    assert "ICMP" not in genet7
    # genet6 path stays the 4-BD doorbell slice; geometry fix is genet7.
    assert "TX_Q16_START" not in genet6

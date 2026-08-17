"""EPIC F V73: UMAC station MAC write + own TX ring + one ARP. No TCP/IP."""

import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_genet6_writes_umac_mac_and_owns_tx() -> None:
    genet = read_repo("Sources/Support/kernel_genet.c")
    support = read_repo("Sources/Support/include/Support.h")
    app = read_repo("Sources/Application/Application.swift")
    shell = read_repo("Sources/Application/UARTShell.swift")
    iterate = read_repo("scripts/netboot/net-iterate.sh")
    scheduler = read_repo("Sources/Support/kernel_scheduler.c")
    xhci = read_repo("Sources/Support/kernel_xhci.c")

    genet5 = genet.split("kernel_genet6_selftest")[0]
    genet6 = genet.split("kernel_genet6_selftest", 1)[1].split("kernel_genet7_selftest")[0]

    assert "int           kernel_genet6_selftest(void);" in support
    assert "G32(UMAC_MAC0) =" not in genet5
    assert "G32(UMAC_MAC1) =" not in genet5
    assert "G32(UMAC_MAC0) =" in genet6
    assert "G32(UMAC_MAC1) =" in genet6
    assert "TDMA_OFF" in genet6 or "0x4000" in genet6
    assert "CMD_TX_EN" in genet6
    assert "CMD_PROMISC" in genet6
    assert "kernel_dma_alloc_nc" in genet6
    assert "200000" not in genet
    assert 'uartPuts("runtime v73: GENET UMAC MAC and TX ARP\\n")' in app
    assert 'uartPuts("genet6 ok=")' in app
    assert "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 73" not in app
    assert "func printGenet6()" in shell
    assert 'shellBufferSliceEquals(commandStart, commandLen, "genet6")' in shell
    assert ",gpio,genet5,genet6" in shell or ",genet5,genet6" in shell
    assert 'grep -qa "genet6 ok=1 version=73 mac=.* tx=.* frames="' in iterate
    assert 'probe_shell "genet6" "^genet6 ok=1 version=73 mac=1 tx=1 frames="' in iterate
    assert "sched12" in iterate
    assert "kernel_genet6_selftest" not in scheduler
    assert "0x400000000" not in genet
    assert "static unsigned long alloc_dma" not in genet
    assert "DMA_TO_BUS" not in genet
    assert "static unsigned long alloc_dma" in xhci
    # No TCP/IP stack this slice.
    assert "TCP" not in genet6
    assert "ICMP" not in genet6

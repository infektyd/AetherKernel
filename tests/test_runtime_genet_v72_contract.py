"""EPIC F V72: GENET leftover-RX stop + NC RX ring. System DMA, not PCIe."""

import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_genet5_is_system_nc_rx_dma() -> None:
    genet = read_repo("Sources/Support/kernel_genet.c")
    dma = read_repo("Sources/Support/kernel_dma.c")
    support = read_repo("Sources/Support/include/Support.h")
    app = read_repo("Sources/Application/Application.swift")
    shell = read_repo("Sources/Application/UARTShell.swift")
    iterate = read_repo("scripts/netboot/net-iterate.sh")
    scheduler = read_repo("Sources/Support/kernel_scheduler.c")
    xhci = read_repo("Sources/Support/kernel_xhci.c")

    assert "kernel_dma_alloc_nc" in dma
    assert "kernel_vmm_map_4k_nc" in dma
    assert "DMA_TO_BUS" not in dma
    assert "int           kernel_dma_alloc_nc(unsigned long *pa_out, void **nc_out);" in support
    assert "int           kernel_genet5_selftest(void);" in support
    assert "kernel_dma_alloc_nc" in genet
    assert "CMD_RX_EN" in genet
    assert "DMA_EN" in genet
    assert "DMA_DISABLED" in genet
    assert 'uartPuts("runtime v72: GENET leftover-RX stop and NC RX ring\\n")' in app
    assert 'uartPuts("genet5 ok=")' in app
    assert "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 72" not in app
    assert "func printGenet5()" in shell
    assert 'shellBufferSliceEquals(commandStart, commandLen, "genet5")' in shell
    assert ",genet4,gpio,genet5" in shell or ",genet4,genet5" in shell
    assert 'grep -qa "genet5 ok=1 version=72 stop=.* ring=.* rx=.* frames="' in iterate
    assert 'probe_shell "genet5" "^genet5 ok=1 version=72 stop=1 ring=1 rx=1 frames="' in iterate
    assert "sched12" in iterate
    assert "kernel_genet5_selftest" not in scheduler
    # System DMA, not the xHCI PCIe window. xHCI keeps its own alloc_dma.
    assert "0x400000000" not in genet
    assert "alloc_dma" not in genet
    assert "DMA_TO_BUS" not in genet
    assert "static unsigned long alloc_dma" in xhci

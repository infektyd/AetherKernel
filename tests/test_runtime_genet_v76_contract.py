"""EPIC F V76: parse one RX ARP/ICMP request and reply. Honest kind=."""

import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_genet9_parses_rx_and_replies() -> None:
    genet = read_repo("Sources/Support/kernel_genet.c")
    support = read_repo("Sources/Support/include/Support.h")
    app = read_repo("Sources/Application/Application.swift")
    shell = read_repo("Sources/Application/UARTShell.swift")
    iterate = read_repo("scripts/netboot/net-iterate.sh")
    scheduler = read_repo("Sources/Support/kernel_scheduler.c")
    dma = read_repo("Sources/Support/kernel_dma.c")

    genet8 = genet.split("kernel_genet9_selftest")[0]
    genet9 = genet.split("kernel_genet9_selftest", 1)[1].split("kernel_genet10_selftest")[0]

    assert "int           kernel_genet9_selftest(void);" in support
    assert "int           kernel_dma_nc_from_pa(unsigned long pa, void **nc_out);" in support
    assert "kernel_dma_nc_from_pa" in dma
    assert "0x0a2a0002" in genet9
    assert "kind" in genet9
    assert "ICMP" in genet9 or "echo" in genet9
    assert "0x0806" in genet9
    assert "tdma_ring16_wr(V4_TDMA_PROD" in genet9 or "tdma_ring16_wr(0x0C" in genet9
    assert "200000" not in genet
    assert 'uartPuts("runtime v76: GENET ARP or ICMP reply\\n")' in app
    assert 'uartPuts("genet9 ok=")' in app
    assert "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 76" not in app
    assert "func printGenet9()" in shell
    assert 'shellBufferSliceEquals(commandStart, commandLen, "genet9")' in shell
    assert ",genet7,genet8,genet9" in shell
    assert 'grep -qa "genet9 ok=1 version=76 rx=.* tx=.* kind="' in iterate
    assert 'probe_shell "genet9" "^genet9 ok=1 version=76 rx=.* tx=.* kind="' in iterate
    assert "10.42.0.2" in iterate
    assert "sched12" in iterate
    assert "kernel_genet9_selftest" not in scheduler
    assert "0x400000000" not in genet
    assert "static unsigned long alloc_dma" not in genet
    assert "TCP" not in genet9
    # genet8 stays the v4 doorbell slice; reply path is genet9.
    assert "0x0a2a0002" not in genet8
    assert "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 75" not in app

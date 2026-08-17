"""EPIC F V75: GENET v4/v5 TDMA PROD is 0x0C, not 0x08. Honest doorbell."""

import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_genet8_uses_v4_tdma_prod_offset() -> None:
    genet = read_repo("Sources/Support/kernel_genet.c")
    support = read_repo("Sources/Support/include/Support.h")
    app = read_repo("Sources/Application/Application.swift")
    shell = read_repo("Sources/Application/UARTShell.swift")
    iterate = read_repo("scripts/netboot/net-iterate.sh")
    scheduler = read_repo("Sources/Support/kernel_scheduler.c")

    genet7 = genet.split("kernel_genet8_selftest")[0]
    genet8 = genet.split("kernel_genet8_selftest", 1)[1]

    assert "int           kernel_genet8_selftest(void);" in support
    assert "V4_TDMA_PROD = 0x0C" in genet8
    assert "V4_TDMA_CONS = 0x08" in genet8
    assert "tdma_ring16_wr(V4_TDMA_PROD" in genet8 or "tdma_ring16_wr(0x0C" in genet8
    assert "200000" not in genet
    assert 'uartPuts("runtime v75: GENET v4 TDMA PROD doorbell\\n")' in app
    assert 'uartPuts("genet8 ok=")' in app
    assert "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 75" not in app
    assert "func printGenet8()" in shell
    assert 'shellBufferSliceEquals(commandStart, commandLen, "genet8")' in shell
    assert ",genet6,genet7,genet8" in shell
    assert 'grep -qa "genet8 ok=1 version=75 prod=.* cons=.* tx=.* frames="' in iterate
    assert 'probe_shell "genet8" "^genet8 ok=1 version=75 prod=.* cons=.* tx=.* frames="' in iterate
    assert 'grep -qa "genet7 ok=1 version=74 ring=.* tx=.* cons=.* prod=.* frames="' in iterate
    assert "sched12" in iterate
    assert "kernel_genet8_selftest" not in scheduler
    assert "0x400000000" not in genet
    assert "static unsigned long alloc_dma" not in genet
    assert "TCP" not in genet8
    assert "ICMP" not in genet8
    # genet7 keeps the v123-named 0x08 doorbell; the v4 fix is genet8.
    assert "V4_TDMA_PROD" not in genet7

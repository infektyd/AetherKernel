"""EPIC F V77: bounded multi-BD GENET poll after shell-ready. Honest replies=."""

import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_genet10_bounded_multi_reply_poll() -> None:
    genet = read_repo("Sources/Support/kernel_genet.c")
    support = read_repo("Sources/Support/include/Support.h")
    app = read_repo("Sources/Application/Application.swift")
    shell = read_repo("Sources/Application/UARTShell.swift")
    iterate = read_repo("scripts/netboot/net-iterate.sh")
    scheduler = read_repo("Sources/Support/kernel_scheduler.c")

    genet9 = genet.split("kernel_genet10_selftest")[0]
    genet10 = genet.split("kernel_genet10_selftest", 1)[1]

    assert "int           kernel_genet10_selftest(void);" in support
    assert "int           kernel_genet10_poll(void);" in support
    assert "rdma_ring16_wr(V4_RDMA_CONS" in genet10 or "rdma_ring16_wr(0x0C" in genet10
    assert "replies" in genet10
    assert "200000" not in genet
    assert 'uartPuts("runtime v77: GENET bounded multi-reply poll\\n")' in app
    assert 'uartPuts("genet10 ok=")' in app
    assert "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 77" not in app
    assert "func printGenet10()" in shell
    assert "kernel_genet10_poll()" in shell
    assert 'shellBufferSliceEquals(commandStart, commandLen, "genet10")' in shell
    assert ",genet8,genet9,genet10" in shell
    assert 'grep -qa "genet10 ok=1 version=77 rx=.* tx=.* replies=.* kind="' in iterate
    assert 'probe_shell "genet10" "^genet10 ok=1 version=77 rx=.* tx=.* replies=.* kind="' in iterate
    assert "ping -c 2" in iterate
    assert "10.42.0.2" in iterate
    assert "sched12" in iterate
    assert "kernel_genet10_selftest" not in scheduler
    assert "kernel_genet10_poll" not in scheduler
    assert "0x400000000" not in genet
    assert "static unsigned long alloc_dma" not in genet
    assert "TCP" not in genet10
    # genet9 stays one-shot; standing poll is genet10.
    assert "kernel_genet10_poll" not in genet9
    assert "V4_RDMA_CONS" not in genet9

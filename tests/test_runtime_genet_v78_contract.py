"""EPIC F V78: bounded UDP echo after shell-ready. Same poll/park as genet10."""

import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_genet11_bounded_udp_echo() -> None:
    genet = read_repo("Sources/Support/kernel_genet.c")
    support = read_repo("Sources/Support/include/Support.h")
    app = read_repo("Sources/Application/Application.swift")
    shell = read_repo("Sources/Application/UARTShell.swift")
    iterate = read_repo("scripts/netboot/net-iterate.sh")
    scheduler = read_repo("Sources/Support/kernel_scheduler.c")

    genet10 = genet.split("kernel_genet11_selftest")[0]
    genet11 = genet.split("kernel_genet11_selftest", 1)[1].split("kernel_genet12_selftest")[0]

    assert "int           kernel_genet11_selftest(void);" in support
    assert "int           kernel_genet11_poll(void);" in support
    assert "rdma_ring16_wr(V4_RDMA_CONS" in genet11 or "rdma_ring16_wr(0x0C" in genet11
    assert "replies" in genet11
    assert "0x11" in genet11 or "UDP" in genet11
    assert "ECHO_PORT" in genet11 or "7U" in genet11
    assert "200000" not in genet
    assert 'uartPuts("runtime v78: GENET bounded UDP echo\\n")' in app
    assert 'uartPuts("genet11 ok=")' in app
    assert "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 78" not in app
    assert "func printGenet11()" in shell
    assert "kernel_genet11_poll()" in shell
    assert 'shellBufferSliceEquals(commandStart, commandLen, "genet11")' in shell
    assert ",genet9,genet10,genet11" in shell
    assert 'grep -qa "genet11 ok=1 version=78 rx=.* tx=.* replies=.* kind="' in iterate
    assert 'probe_shell "genet11" "^genet11 ok=1 version=78 rx=.* tx=.* replies=.* kind="' in iterate
    assert "SOCK_DGRAM" in iterate
    assert "10.42.0.2" in iterate
    assert "ping -c 2" in iterate
    assert "sched12" in iterate
    assert "kernel_genet11_selftest" not in scheduler
    assert "kernel_genet11_poll" not in scheduler
    assert "0x400000000" not in genet
    assert "static unsigned long alloc_dma" not in genet
    assert "TCP" not in genet11
    # genet10 stays ICMP poll; UDP echo is genet11. No free-running ring.
    assert "kernel_genet11_poll" not in genet10
    assert "0x11" not in genet10

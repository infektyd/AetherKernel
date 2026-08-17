"""EPIC G V83: bounded SPI0 byte. DONE is success. loop=1 only on RX==TX."""

import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_spi2_is_bounded_done_transfer() -> None:
    xfer = read_repo("Sources/Support/kernel_spi_xfer.c")
    probe = read_repo("Sources/Support/kernel_i2c.c")
    support = read_repo("Sources/Support/include/Support.h")
    app = read_repo("Sources/Application/Application.swift")
    shell = read_repo("Sources/Application/UARTShell.swift")
    iterate = read_repo("scripts/netboot/net-iterate.sh")
    scheduler = read_repo("Sources/Support/kernel_scheduler.c")

    assert "0xFE204000" in xfer
    assert "SPI2_TX" in xfer
    assert "0x5AU" in xfer
    assert "SPI_CS_DONE" in xfer
    assert "SPI2_TIMEOUT_TICKS" in xfer
    assert "read_cntpct" in xfer
    assert "int           kernel_spi2_selftest(void);" in support
    assert 'uartPuts("runtime v83: SPI0 bounded transfer\\n")' in app
    assert 'uartPuts("spi2 ok=")' in app
    assert "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 83" not in app
    assert "func printSpi2()" in shell
    assert 'shellBufferSliceEquals(commandStart, commandLen, "spi2")' in shell
    assert ",i2c2,spi2" in shell
    assert 'grep -qa "spi2 ok=1 version=83 done=1 loop=.* rx="' in iterate
    assert 'probe_shell "spi2" "^spi2 ok=1 version=83 done=1 loop=.* rx="' in iterate
    assert "ping -c 2" in iterate
    assert "SOCK_STREAM" in iterate
    assert "sched12" in iterate
    assert "kernel_spi2_selftest" not in scheduler
    assert "i < 200000" not in xfer
    assert "alloc_dma" not in xfer
    assert "kernel_event_emit" not in xfer
    assert "kernel_enter_el0_and_wait" not in xfer
    assert "G32(SPI0_BASE, SPI_CS) =" not in probe
    assert "spi2_mux_restore" in xfer
    # loop=1 is RX==TX only; missing jumper is not a failure.
    assert "spi2_rx_val == SPI2_TX" in xfer


def test_no_boot_el0_enter_after_genet12() -> None:
    """sys_socket after GENET DMA I-aborted; do not ship a second boot EL0 enter."""
    app = read_repo("Sources/Application/Application.swift")
    after = app.split("kernel_genet12_selftest", 1)[1]
    assert "kernel_enter_el0_and_wait" not in after
    assert "kernel_socket_selftest" not in after
    assert "kernel_spi2_selftest" in after

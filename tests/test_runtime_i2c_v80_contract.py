"""EPIC G V80: BSC1 + SPI0 register probe. Read-only. No boot event emit."""

import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_i2c_spi_is_read_only_register_probe() -> None:
    i2c = read_repo("Sources/Support/kernel_i2c.c")
    support = read_repo("Sources/Support/include/Support.h")
    app = read_repo("Sources/Application/Application.swift")
    shell = read_repo("Sources/Application/UARTShell.swift")
    iterate = read_repo("scripts/netboot/net-iterate.sh")
    scheduler = read_repo("Sources/Support/kernel_scheduler.c")
    genet = read_repo("Sources/Support/kernel_genet.c")

    assert "0xFE804000" in i2c  # BSC1
    assert "0xFE204000" in i2c  # SPI0
    assert "BSC_DIV" in i2c
    assert "SPI_CS" in i2c
    assert "int           kernel_i2c_selftest(void);" in support
    assert 'uartPuts("runtime v80: I2C and SPI register probe\\n")' in app
    assert 'uartPuts("i2c ok=")' in app
    assert "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 80" not in app
    assert "func printI2c()" in shell
    assert 'shellBufferSliceEquals(commandStart, commandLen, "i2c")' in shell
    assert ",genet10,genet11,genet12,i2c" in shell
    assert 'grep -qa "i2c ok=1 version=80 bsc=.* div=.* spi="' in iterate
    assert 'probe_shell "i2c" "^i2c ok=1 version=80 bsc=1 div=.* spi=1"' in iterate
    assert "ping -c 2" in iterate
    assert "SOCK_STREAM" in iterate
    assert "sched12" in iterate
    assert "kernel_i2c_selftest" not in scheduler
    assert "200000" not in i2c
    # Read-only: no BSC/SPI control writes, no DMA, no standing GENET ring.
    assert "G32(BSC1_BASE, BSC_C) =" not in i2c
    assert "G32(SPI0_BASE, SPI_CS) =" not in i2c
    assert "alloc_dma" not in i2c
    assert "kernel_event_emit" not in i2c
    assert "kernel_genet12_poll" in genet
    assert "kernel_genet11_poll" in genet
    assert "kernel_genet10_poll" in genet

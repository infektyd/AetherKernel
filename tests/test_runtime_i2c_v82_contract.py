"""EPIC G V82: bounded BSC1 no-ACK write. Honest nack=1. No boot event emit."""

import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_i2c2_is_bounded_nack_transfer() -> None:
    xfer = read_repo("Sources/Support/kernel_i2c_nack.c")
    probe = read_repo("Sources/Support/kernel_i2c.c")
    support = read_repo("Sources/Support/include/Support.h")
    app = read_repo("Sources/Application/Application.swift")
    shell = read_repo("Sources/Application/UARTShell.swift")
    iterate = read_repo("scripts/netboot/net-iterate.sh")
    scheduler = read_repo("Sources/Support/kernel_scheduler.c")

    assert "0xFE804000" in xfer
    assert "I2C2_ADDR" in xfer
    assert "0x7FU" in xfer
    assert "BSC_S_ERR" in xfer
    assert "I2C2_TIMEOUT_TICKS" in xfer
    assert "read_cntpct" in xfer
    assert "int           kernel_i2c2_selftest(void);" in support
    assert 'uartPuts("runtime v82: I2C no-ACK transfer\\n")' in app
    assert 'uartPuts("i2c2 ok=")' in app
    assert "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 82" not in app
    assert "func printI2c2()" in shell
    assert 'shellBufferSliceEquals(commandStart, commandLen, "i2c2")' in shell
    assert ",pwm,i2c2" in shell
    assert 'grep -qa "i2c2 ok=1 version=82 nack=1 addr=.* sta="' in iterate
    assert 'probe_shell "i2c2" "^i2c2 ok=1 version=82 nack=1 addr=0x7f sta="' in iterate
    assert "ping -c 2" in iterate
    assert "SOCK_STREAM" in iterate
    assert "sched12" in iterate
    assert "kernel_i2c2_selftest" not in scheduler
    assert "i < 200000" not in xfer
    assert "alloc_dma" not in xfer
    assert "kernel_event_emit" not in xfer
    assert "kernel_enter_el0_and_wait" not in xfer
    # V80 probe file stays read-only.
    assert "G32(BSC1_BASE, BSC_C) =" not in probe
    # Transfer is bounded and fail-closed: ACK or CLKT is not success.
    assert "BSC_S_CLKT" in xfer
    assert "i2c2_mux_restore" in xfer


def test_no_boot_el0_enter_after_genet12() -> None:
    """sys_socket after GENET DMA I-aborted; do not ship a second boot EL0 enter."""
    app = read_repo("Sources/Application/Application.swift")
    after = app.split("kernel_genet12_selftest", 1)[1]
    assert "kernel_enter_el0_and_wait" not in after
    assert "kernel_socket_selftest" not in after
    assert "kernel_i2c2_selftest" in after

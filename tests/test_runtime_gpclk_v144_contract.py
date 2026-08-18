"""EPIC G V144: GPCLK0 enable after GENET. Fail-closed CM_GP0 readback. No EL0."""

import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_gpclk_enable_fail_closed_without_el0() -> None:
    load = read_repo("Sources/Support/kernel_gpclk.c")
    aux = read_repo("Sources/Support/kernel_auxspi1.c")
    pcm = read_repo("Sources/Support/kernel_pcm.c")
    support = read_repo("Sources/Support/include/Support.h")
    app = read_repo("Sources/Application/Application.swift")
    shell = read_repo("Sources/Application/UARTShell.swift")
    iterate = read_repo("scripts/netboot/net-iterate.sh")
    doctor = read_repo("scripts/netboot/netboot-doctor.sh")
    scheduler = read_repo("Sources/Support/kernel_scheduler.c")
    roadmap = read_repo("docs/ROADMAP.md")

    assert "0xFE101070" in load
    assert "0xFE101074" in load
    assert "CM_GP0CTL" in load
    assert "CM_ENAB" in load
    assert "int           kernel_gpclk_selftest(void);" in support
    assert "unsigned int  kernel_gpclk_clk(void);" in support
    assert "unsigned int  kernel_gpclk_restore(void);" in support
    assert 'uartPuts("runtime v144: GPCLK probe\\n")' in app
    assert 'uartPuts("gpclk ok=")' in app
    assert "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 144" not in app
    assert "func printGpclk()" in shell
    assert 'shellBufferSliceEquals(commandStart, commandLen, "gpclk")' in shell
    assert ",auxspi1,gpclk" in shell
    assert 'grep -qa "gpclk ok=1 version=144 clk="' in iterate
    assert 'probe_shell "gpclk" "^gpclk ok=1 version=144 clk="' in iterate
    assert 'grep -q "gpclk ok=1 version=144 clk="' in doctor
    assert "V144 GPCLK0 enable" in roadmap
    assert "kernel_gpclk_selftest" not in scheduler
    assert "alloc_dma" not in load
    assert "kernel_event_emit" not in load
    assert "kernel_enter_el0_and_wait" not in load
    assert "0xbf000002" in load
    assert "watchdog_reset_now" not in load
    # New unused GPCLK0 CM block. Do not retry UART2-5 / BSC0 / SPI0 TA.
    assert "0xFE201400" not in load
    assert "0xFE201000" not in load
    assert "0xFE205000" not in load
    assert "0xFE804000" not in load
    assert "0xFE203000" not in load
    assert "0xFE20C000" not in load
    assert "0xFE20C800" not in load
    assert "0xFE215000" not in load
    assert "UARTCR" not in load
    assert "PCM_CS" not in load
    assert "AUXENB" not in load
    assert "PWM_DAT1" not in load
    assert "GPREN" not in load
    assert "GPEDS" not in load
    assert "GPFSEL" not in load
    assert "ALT0" not in load
    assert "SPI_CS_TA" not in load
    assert "0xFE204000" not in load
    assert "PUP_PDN" not in load
    assert "RBUF_64B_EN" not in load
    assert "DMA_TX_DO_CSUM" not in load
    assert "CMD_LCL_LOOP_EN" not in load
    assert "EXT_RGMII_OOB_CTRL" not in load
    assert "RBUF_HFB_EN" not in load
    assert "0xFE101070" not in aux
    assert "0xFE101070" not in pcm
    assert "ping -c 2" in iterate
    assert "SOCK_STREAM" in iterate
    assert "sched12" in iterate


def test_no_boot_el0_enter_after_genet12() -> None:
    """sys_socket after GENET DMA I-aborted; do not ship a second boot EL0 enter."""
    app = read_repo("Sources/Application/Application.swift")
    after = app.split("kernel_genet12_selftest", 1)[1]
    assert "kernel_enter_el0_and_wait" not in after
    assert "kernel_socket_selftest" not in after
    assert "kernel_gpclk_selftest" in after
    assert "kernel_uart2_selftest" not in after
    assert "kernel_bsc0_selftest" not in after
    assert "kernel_spi3_selftest" not in after

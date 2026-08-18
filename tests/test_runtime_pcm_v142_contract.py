"""EPIC G V142: PCM/I2S clock+enable after GENET. Fail-closed readback. No EL0."""

import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_pcm_enable_fail_closed_without_el0() -> None:
    load = read_repo("Sources/Support/kernel_pcm.c")
    pwm2 = read_repo("Sources/Support/kernel_pwm2.c")
    pwm4 = read_repo("Sources/Support/kernel_pwm4.c")
    support = read_repo("Sources/Support/include/Support.h")
    app = read_repo("Sources/Application/Application.swift")
    shell = read_repo("Sources/Application/UARTShell.swift")
    iterate = read_repo("scripts/netboot/net-iterate.sh")
    doctor = read_repo("scripts/netboot/netboot-doctor.sh")
    scheduler = read_repo("Sources/Support/kernel_scheduler.c")
    roadmap = read_repo("docs/ROADMAP.md")

    assert "0xFE203000" in load
    assert "0xFE101098" in load
    assert "PCM_CS" in load
    assert "PCM_CS_EN" in load
    assert "CM_PCMCTL" in load
    assert "int           kernel_pcm_selftest(void);" in support
    assert "unsigned int  kernel_pcm_clk(void);" in support
    assert "unsigned int  kernel_pcm_en(void);" in support
    assert "unsigned int  kernel_pcm_restore(void);" in support
    assert 'uartPuts("runtime v142: PCM enable\\n")' in app
    assert 'uartPuts("pcm ok=")' in app
    assert "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 142" not in app
    assert "func printPcm()" in shell
    assert 'shellBufferSliceEquals(commandStart, commandLen, "pcm")' in shell
    assert ",pwm4,pcm" in shell
    assert 'grep -qa "pcm ok=1 version=142 clk=1 en="' in iterate
    assert 'probe_shell "pcm" "^pcm ok=1 version=142 clk=1 en="' in iterate
    assert 'grep -q "pcm ok=1 version=142 clk=1 en="' in doctor
    assert "V142 PCM/I2S enable" in roadmap
    assert "kernel_pcm_selftest" not in scheduler
    assert "alloc_dma" not in load
    assert "kernel_event_emit" not in load
    assert "kernel_enter_el0_and_wait" not in load
    assert "0xbf000002" in load
    assert "watchdog_reset_now" not in load
    # New unused PCM block, not a PWM-DAT / edge / ALT0 / SPI0-TA clone.
    assert "0xFE20C000" not in load
    assert "0xFE20C800" not in load
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
    assert "PCM_CS_TXON" in load
    assert "PCM_CS_RXON" in load
    assert "0xFE203000" not in pwm2
    assert "0xFE203000" not in pwm4
    assert "ping -c 2" in iterate
    assert "SOCK_STREAM" in iterate
    assert "sched12" in iterate


def test_no_boot_el0_enter_after_genet12() -> None:
    """sys_socket after GENET DMA I-aborted; do not ship a second boot EL0 enter."""
    app = read_repo("Sources/Application/Application.swift")
    after = app.split("kernel_genet12_selftest", 1)[1]
    assert "kernel_enter_el0_and_wait" not in after
    assert "kernel_socket_selftest" not in after
    assert "kernel_pcm_selftest" in after
    assert "kernel_spi3_selftest" not in after

"""EPIC G V141: PWM1 channel-1 program after GENET. Fail-closed readback. No EL0."""

import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_pwm4_pwm1_program_fail_closed_without_el0() -> None:
    load = read_repo("Sources/Support/kernel_pwm4.c")
    pwm2 = read_repo("Sources/Support/kernel_pwm2.c")
    pwm3 = read_repo("Sources/Support/kernel_pwm3.c")
    support = read_repo("Sources/Support/include/Support.h")
    app = read_repo("Sources/Application/Application.swift")
    shell = read_repo("Sources/Application/UARTShell.swift")
    iterate = read_repo("scripts/netboot/net-iterate.sh")
    doctor = read_repo("scripts/netboot/netboot-doctor.sh")
    scheduler = read_repo("Sources/Support/kernel_scheduler.c")
    roadmap = read_repo("docs/ROADMAP.md")

    assert "0xFE20C800" in load
    assert "PWM_RNG1" in load
    assert "PWM_DAT1" in load
    assert "PWM_PWEN1" in load
    assert "kernel_pwm3_selftest" in load
    assert "int           kernel_pwm4_selftest(void);" in support
    assert "unsigned int  kernel_pwm4_pwm1(void);" in support
    assert "unsigned int  kernel_pwm4_en(void);" in support
    assert "unsigned int  kernel_pwm4_restore(void);" in support
    assert 'uartPuts("runtime v141: PWM1 program\\n")' in app
    assert 'uartPuts("pwm4 ok=")' in app
    assert "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 141" not in app
    assert "func printPwm4()" in shell
    assert 'shellBufferSliceEquals(commandStart, commandLen, "pwm4")' in shell
    assert ",gpio4,pwm4" in shell
    assert 'grep -qa "pwm4 ok=1 version=141 pwm1=1 en="' in iterate
    assert 'probe_shell "pwm4" "^pwm4 ok=1 version=141 pwm1=1 en="' in iterate
    assert 'grep -q "pwm4 ok=1 version=141 pwm1=1 en="' in doctor
    assert "V141 PWM1 channel-1 program" in roadmap
    assert "kernel_pwm4_selftest" not in scheduler
    assert "alloc_dma" not in load
    assert "kernel_event_emit" not in load
    assert "kernel_enter_el0_and_wait" not in load
    assert "0xbf000002" in load
    assert "watchdog_reset_now" not in load
    # Second PWM block, not another PWM0 / pin-mux / edge / SPI-TA clone.
    assert "0xFE20C000" not in load
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
    assert "0xFE20C800" not in pwm2
    assert "PWM1_BASE" not in pwm3
    assert "ping -c 2" in iterate
    assert "SOCK_STREAM" in iterate
    assert "sched12" in iterate


def test_no_boot_el0_enter_after_genet12() -> None:
    """sys_socket after GENET DMA I-aborted; do not ship a second boot EL0 enter."""
    app = read_repo("Sources/Application/Application.swift")
    after = app.split("kernel_genet12_selftest", 1)[1]
    assert "kernel_enter_el0_and_wait" not in after
    assert "kernel_socket_selftest" not in after
    assert "kernel_pwm4_selftest" in after
    assert "kernel_spi3_selftest" not in after

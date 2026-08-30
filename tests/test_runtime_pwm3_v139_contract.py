"""EPIC G V139: PWM GPIO12 ALT0 pin-mux after GENET. Fail-closed FSEL. No EL0."""

import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_pwm3_pin_mux_fail_closed_without_el0() -> None:
    load = read_repo("Sources/Support/kernel_pwm3.c")
    pwm_ro = read_repo("Sources/Support/kernel_pwm.c")
    pwm2 = read_repo("Sources/Support/kernel_pwm2.c")
    support = read_repo("Sources/Support/include/Support.h")
    app = read_repo("Sources/Application/Application.swift")
    shell = read_repo("Sources/Application/UARTShell.swift")
    iterate = read_repo("scripts/netboot/net-iterate.sh")
    doctor = read_repo("scripts/netboot/netboot-doctor.sh")
    scheduler = read_repo("Sources/Support/kernel_scheduler.c")
    roadmap = read_repo("docs/ROADMAP.md")

    assert "0xFE200000" in load
    assert "GPFSEL1" in load
    assert "12U" in load or "GPIO12" in load
    assert "ALT0" in load
    assert "kernel_pwm2_selftest" in load
    assert "int           kernel_pwm3_selftest(void);" in support
    assert "unsigned int  kernel_pwm3_pin(void);" in support
    assert "unsigned int  kernel_pwm3_alt(void);" in support
    assert "unsigned int  kernel_pwm3_restore(void);" in support
    assert 'uartPuts("runtime v139: PWM pin-mux\\n")' in app
    assert 'uartPuts("pwm3 ok=")' in app
    assert "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 139" not in app
    assert "func printPwm3()" in shell
    assert 'shellBufferSliceEquals(commandStart, commandLen, "pwm3")' in shell
    assert ",genet34,pwm3" in shell
    assert 'grep -qa "pwm3 ok=1 version=139 pin=12 alt="' in iterate
    assert 'probe_shell "pwm3" "^pwm3 ok=1 version=139 pin=12 alt="' in iterate
    assert 'grep -q "pwm3 ok=1 version=139 pin=12 alt="' in doctor
    assert "V139 PWM GPIO12 ALT0 pin-mux" in roadmap
    assert "kernel_pwm3_selftest" not in scheduler
    assert "alloc_dma" not in load
    assert "kernel_event_emit" not in load
    assert "kernel_enter_el0_and_wait" not in load
    assert "0xbf000002" in load
    assert "watchdog_reset_now" not in load
    # Fail-closed FSEL readback; no output claim; UART 14/15 pulls untouched.
    assert "GPFSEL1" in load
    assert "PUP_PDN" not in load
    assert "0xE4" not in load
    assert "GPSET" not in load
    assert "GPCLR" not in load
    assert "PWM_CTL" not in load
    assert "PWM_DAT1" not in load
    assert "0xFE20C800" not in load
    assert "RBUF_64B_EN" not in load
    assert "DMA_TX_DO_CSUM" not in load
    assert "CMD_LCL_LOOP_EN" not in load
    assert "EXT_RGMII_OOB_CTRL" not in load
    assert "G32(PWM0_BASE, PWM_CTL) =" not in pwm_ro
    assert "GPFSEL" not in pwm2
    assert "ping -c 2" in iterate
    assert "SOCK_STREAM" in iterate
    assert "sched12" in iterate


def test_no_boot_el0_enter_after_genet12() -> None:
    """sys_socket after GENET DMA I-aborted; do not ship a second boot EL0 enter."""
    app = read_repo("Sources/Application/Application.swift")
    after = app.split("kernel_genet12_selftest", 1)[1]
    assert "kernel_enter_el0_and_wait" not in after
    assert "kernel_socket_selftest" not in after
    assert "kernel_pwm3_selftest" in after

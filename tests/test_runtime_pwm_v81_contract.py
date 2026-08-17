"""EPIC G V81: PWM0+PWM1 register probe. Read-only. No boot event emit."""

import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_pwm_is_read_only_register_probe() -> None:
    pwm = read_repo("Sources/Support/kernel_pwm.c")
    support = read_repo("Sources/Support/include/Support.h")
    app = read_repo("Sources/Application/Application.swift")
    shell = read_repo("Sources/Application/UARTShell.swift")
    iterate = read_repo("scripts/netboot/net-iterate.sh")
    scheduler = read_repo("Sources/Support/kernel_scheduler.c")
    genet = read_repo("Sources/Support/kernel_genet.c")

    assert "0xFE20C000" in pwm  # PWM0
    assert "0xFE20C800" in pwm  # PWM1, 2711-only second block
    assert "PWM_STA_EMPT1" in pwm
    assert "int           kernel_pwm_selftest(void);" in support
    assert 'uartPuts("runtime v81: PWM register probe\\n")' in app
    assert 'uartPuts("pwm ok=")' in app
    assert "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 81" not in app
    assert "func printPwm()" in shell
    assert 'shellBufferSliceEquals(commandStart, commandLen, "pwm")' in shell
    assert ",genet12,i2c,pwm" in shell
    assert 'grep -qa "pwm ok=1 version=81 ctl=.* sta=.* pwm1="' in iterate
    assert 'probe_shell "pwm" "^pwm ok=1 version=81 ctl=.* sta=.* pwm1=1"' in iterate
    assert "ping -c 2" in iterate
    assert "SOCK_STREAM" in iterate
    assert "sched12" in iterate
    assert "kernel_pwm_selftest" not in scheduler
    assert "200000" not in pwm
    # Read-only: no CTL/STA writes, no DMA, no standing GENET ring, no EL0.
    assert "G32(PWM0_BASE, PWM_CTL) =" not in pwm
    assert "G32(PWM1_BASE, PWM_CTL) =" not in pwm
    assert "alloc_dma" not in pwm
    assert "kernel_event_emit" not in pwm
    assert "kernel_enter_el0_and_wait" not in pwm
    assert "kernel_genet12_poll" in genet


def test_no_boot_el0_enter_after_genet12() -> None:
    """sys_socket after GENET DMA I-aborted; do not ship a second boot EL0 enter."""
    app = read_repo("Sources/Application/Application.swift")
    after = app.split("kernel_genet12_selftest", 1)[1]
    assert "kernel_enter_el0_and_wait" not in after
    assert "kernel_socket_selftest" not in after
    assert "kernel_pwm_selftest" in after

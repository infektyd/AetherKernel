"""EPIC G V93: PWM clock enable + CTL poke after GENET. No pin-mux. No EL0."""

import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_pwm2_clock_enable_without_pin_mux() -> None:
    load = read_repo("Sources/Support/kernel_pwm2.c")
    pwm_ro = read_repo("Sources/Support/kernel_pwm.c")
    support = read_repo("Sources/Support/include/Support.h")
    app = read_repo("Sources/Application/Application.swift")
    shell = read_repo("Sources/Application/UARTShell.swift")
    iterate = read_repo("scripts/netboot/net-iterate.sh")
    scheduler = read_repo("Sources/Support/kernel_scheduler.c")

    assert "0xFE20C000" in load
    assert "0xFE1010A0" in load  # CM_PWMCTL
    assert "CM_ENAB" in load
    assert "CM_BUSY" in load
    assert "PWM_PWEN1" in load
    assert "int           kernel_pwm2_selftest(void);" in support
    assert 'uartPuts("runtime v93: PWM clock enable\\n")' in app
    assert 'uartPuts("pwm2 ok=")' in app
    assert "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 93" not in app
    assert "func printPwm2()" in shell
    assert 'shellBufferSliceEquals(commandStart, commandLen, "pwm2")' in shell
    assert ",stimer2,pwm2" in shell
    assert 'grep -qa "pwm2 ok=1 version=93 clk=1 en=1"' in iterate
    assert 'probe_shell "pwm2" "^pwm2 ok=1 version=93 clk=1 en=1"' in iterate
    assert "ping -c 2" in iterate
    assert "SOCK_STREAM" in iterate
    assert "sched12" in iterate
    assert "kernel_pwm2_selftest" not in scheduler
    assert "alloc_dma" not in load
    assert "kernel_event_emit" not in load
    assert "kernel_enter_el0_and_wait" not in load
    assert "0xbf000002" in load
    # No pin-mux, no PWM1 poke, no output claim.
    assert "GPFSEL" not in load
    assert "0xFE20C800" not in load
    assert "loop=" not in load
    # V81 stays read-only.
    assert "G32(PWM0_BASE, PWM_CTL) =" not in pwm_ro
    assert "CM_PWMCTL" not in pwm_ro


def test_no_boot_el0_enter_after_genet12() -> None:
    """sys_socket after GENET DMA I-aborted; do not ship a second boot EL0 enter."""
    app = read_repo("Sources/Application/Application.swift")
    after = app.split("kernel_genet12_selftest", 1)[1]
    assert "kernel_enter_el0_and_wait" not in after
    assert "kernel_socket_selftest" not in after
    assert "kernel_pwm2_selftest" in after

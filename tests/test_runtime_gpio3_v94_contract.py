"""EPIC G V94: GPIO26 PUP_PDN write+readback after GENET. No UART. No EL0."""

import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_gpio3_pup_readback_without_el0() -> None:
    load = read_repo("Sources/Support/kernel_gpio3.c")
    gpio_ro = read_repo("Sources/Support/kernel_gpio.c")
    support = read_repo("Sources/Support/include/Support.h")
    app = read_repo("Sources/Application/Application.swift")
    shell = read_repo("Sources/Application/UARTShell.swift")
    iterate = read_repo("scripts/netboot/net-iterate.sh")
    scheduler = read_repo("Sources/Support/kernel_scheduler.c")

    assert "0xFE200000" in load
    assert "PUP_PDN1" in load
    assert "0xE8" in load  # GPIO_PUP_PDN_CNTRL_REG1, pins 16-31
    assert "26" in load
    assert "int           kernel_gpio3_selftest(void);" in support
    assert 'uartPuts("runtime v94: GPIO PUP readback\\n")' in app
    assert 'uartPuts("gpio3 ok=")' in app
    assert "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 94" not in app
    assert "func printGpio3()" in shell
    assert 'shellBufferSliceEquals(commandStart, commandLen, "gpio3")' in shell
    assert ",pwm2,gpio3" in shell
    assert 'grep -qa "gpio3 ok=1 version=94 pin=26 up=1 dn=1"' in iterate
    assert 'probe_shell "gpio3" "^gpio3 ok=1 version=94 pin=26 up=1 dn=1"' in iterate
    assert "ping -c 2" in iterate
    assert "SOCK_STREAM" in iterate
    assert "sched12" in iterate
    assert "kernel_gpio3_selftest" not in scheduler
    assert "alloc_dma" not in load
    assert "kernel_event_emit" not in load
    assert "kernel_enter_el0_and_wait" not in load
    assert "0xbf000002" in load
    # No UART mux (REG0 / pins 14-15), no PWM pin-mux, no SET/CLR.
    assert "GPFSEL1" not in load
    assert "GPSET" not in load
    assert "GPCLR" not in load
    assert "PUP_PDN0" not in load
    assert "0xE4" not in load
    # V71 stays read-only.
    assert "G32(PUP_PDN0) =" not in gpio_ro
    assert "G32(GPFSEL0) =" not in gpio_ro


def test_no_boot_el0_enter_after_genet12() -> None:
    """sys_socket after GENET DMA I-aborted; do not ship a second boot EL0 enter."""
    app = read_repo("Sources/Application/Application.swift")
    after = app.split("kernel_genet12_selftest", 1)[1]
    assert "kernel_enter_el0_and_wait" not in after
    assert "kernel_socket_selftest" not in after
    assert "kernel_gpio3_selftest" in after

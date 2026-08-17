"""EPIC G V91: GPIO42 output + GPLEV readback after GENET. No EL0. No jumper."""

import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_gpio2_output_readback_without_el0() -> None:
    load = read_repo("Sources/Support/kernel_gpio2.c")
    gpio_ro = read_repo("Sources/Support/kernel_gpio.c")
    support = read_repo("Sources/Support/include/Support.h")
    app = read_repo("Sources/Application/Application.swift")
    shell = read_repo("Sources/Application/UARTShell.swift")
    iterate = read_repo("scripts/netboot/net-iterate.sh")
    scheduler = read_repo("Sources/Support/kernel_scheduler.c")

    assert "0xFE200000" in load
    assert "GPFSEL4" in load
    assert "GPSET1" in load
    assert "GPCLR1" in load
    assert "GPLEV1" in load
    assert "0x38" in load  # GPLEV1; 0x34 is GPLEV0
    assert "42" in load
    assert "int           kernel_gpio2_selftest(void);" in support
    assert 'uartPuts("runtime v91: GPIO output readback\\n")' in app
    assert 'uartPuts("gpio2 ok=")' in app
    assert "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 91" not in app
    assert "func printGpio2()" in shell
    assert 'shellBufferSliceEquals(commandStart, commandLen, "gpio2")' in shell
    assert ",sdiss,gpio2" in shell
    assert 'grep -qa "gpio2 ok=1 version=91 pin=42 set=1 clr=1"' in iterate
    assert 'probe_shell "gpio2" "^gpio2 ok=1 version=91 pin=42 set=1 clr=1"' in iterate
    assert "ping -c 2" in iterate
    assert "SOCK_STREAM" in iterate
    assert "sched12" in iterate
    assert "kernel_gpio2_selftest" not in scheduler
    assert "alloc_dma" not in load
    assert "kernel_event_emit" not in load
    assert "kernel_enter_el0_and_wait" not in load
    assert "0xbf000002" in load
    assert "GPFSEL1" not in load
    assert "PUP_PDN" not in load
    # V71 stays read-only.
    assert "G32(GPFSEL0) =" not in gpio_ro
    assert "G32(GPFSEL1) =" not in gpio_ro
    assert "GPSET" not in gpio_ro
    assert "GPCLR" not in gpio_ro


def test_no_boot_el0_enter_after_genet12() -> None:
    """sys_socket after GENET DMA I-aborted; do not ship a second boot EL0 enter."""
    app = read_repo("Sources/Application/Application.swift")
    after = app.split("kernel_genet12_selftest", 1)[1]
    assert "kernel_enter_el0_and_wait" not in after
    assert "kernel_socket_selftest" not in after
    assert "kernel_gpio2_selftest" in after

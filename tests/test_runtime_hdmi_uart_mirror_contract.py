"""EPIC D remainder: UART bytes are queued for HDMI, painted off the IRQ path."""

import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_uart_hdmi_mirror_is_queued_not_irq_blitted() -> None:
    console = read_repo("Sources/Support/kernel_vc_console.c")
    support = read_repo("Sources/Support/include/Support.h")
    uart = read_repo("Sources/Application/UART.swift")
    executor = read_repo("Sources/Application/KernelExecutor.swift")
    shell = read_repo("Sources/Application/UARTShell.swift")
    smp = read_repo("Sources/Support/kernel_smp.c")
    scheduler = read_repo("Sources/Support/kernel_scheduler.c")

    assert "void kernel_vc_console_note_uart(unsigned int byte)" in console
    assert "int kernel_vc_console_paint_if_needed(void)" in console
    assert "console_mirror_ring" in console
    assert "kernel_vc_console_note_uart(UInt32(c))" in uart
    assert "kernel_vc_console_paint_if_needed()" in executor
    assert "kernel_vc_console_paint_if_needed()" not in smp
    tick_body = console.split("void kernel_vc_console_tick")[1].split(
        "void kernel_vc_console_note_uart"
    )[0]
    assert "blit_char" not in tick_body
    assert "kernel_vc_console_tick(tick)" in scheduler
    assert "void         kernel_vc_console_note_uart(unsigned int byte);" in support
    assert 'uartPuts(" display=0 mirror=")' in shell
    assert "kernel_vc_console_mirror_count()" in shell

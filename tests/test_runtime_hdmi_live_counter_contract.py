"""EPIC D: HDMI live counter is ticked from the scheduler, not a stub."""

import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_hdmi_counter_is_blitted_and_ticked_from_scheduler() -> None:
    console = read_repo("Sources/Support/kernel_vc_console.c")
    support = read_repo("Sources/Support/include/Support.h")
    scheduler = read_repo("Sources/Support/kernel_scheduler.c")
    shell = read_repo("Sources/Application/UARTShell.swift")

    assert "void kernel_vc_console_tick(unsigned long scheduler_tick)" in console
    assert "void kernel_vc_console_blit_counter(unsigned long value)" in console
    assert "int kernel_vc_console_paint_if_needed(void)" in console
    assert "blit_decimal_row1" in console
    assert "console_counter_val++" in console
    tick_body = console.split("void kernel_vc_console_tick")[1].split("int kernel_vc_console_paint")[0]
    assert "blit_decimal_row1" not in tick_body
    assert "blit_char" not in tick_body
    assert "void         kernel_vc_console_tick(unsigned long scheduler_tick);" in support
    assert "int          kernel_vc_console_paint_if_needed(void);" in support
    assert "kernel_vc_console_tick(tick)" in scheduler
    smp = read_repo("Sources/Support/kernel_smp.c")
    executor = read_repo("Sources/Application/KernelExecutor.swift")
    assert "kernel_vc_console_paint_if_needed()" not in smp
    assert "kernel_vc_console_paint_if_needed()" in executor
    assert 'uartPuts(" counter=")' in shell
    assert "kernel_vc_console_counter()" in shell
    app = read_repo("Sources/Application/Application.swift")
    assert 'uartPuts(" counter=")' in app
    net = read_repo("scripts/netboot/net-iterate.sh")
    assert (
        'probe_shell "console" "^console ok=1 version=60 .* display=0"'
        in net
    )

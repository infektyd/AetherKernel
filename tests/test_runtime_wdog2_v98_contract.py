"""EPIC G/H V98: PM watchdog remaining-tick readback after GENET. No EL0. No reset."""

import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_wdog2_remaining_without_el0_or_reset() -> None:
    load = read_repo("Sources/Support/kernel_wdog2.c")
    wdog = read_repo("Sources/Support/watchdog.c")
    support = read_repo("Sources/Support/include/Support.h")
    app = read_repo("Sources/Application/Application.swift")
    shell = read_repo("Sources/Application/UARTShell.swift")
    iterate = read_repo("scripts/netboot/net-iterate.sh")
    scheduler = read_repo("Sources/Support/kernel_scheduler.c")

    assert "watchdog_remaining_ticks" in load
    assert "watchdog_remaining_ticks" in wdog
    assert "watchdog_full_reset_armed" in load
    assert "watchdog_disable" in load
    assert "watchdog_arm_seconds" in load
    assert "watchdog_reset_now" not in load
    assert "int           kernel_wdog2_selftest(void);" in support
    assert 'uartPuts("runtime v98: watchdog remaining\\n")' in app
    assert 'uartPuts("wdog2 ok=")' in app
    assert "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 98" not in app
    assert "func printWdog2()" in shell
    assert 'shellBufferSliceEquals(commandStart, commandLen, "wdog2")' in shell
    assert ",mboxc,wdog2" in shell
    assert 'grep -qa "wdog2 ok=1 version=98 armed=1 off=1 remain="' in iterate
    assert 'probe_shell "wdog2" "^wdog2 ok=1 version=98 armed=1 off=1 remain="' in iterate
    assert "ping -c 2" in iterate
    assert "SOCK_STREAM" in iterate
    assert "sched12" in iterate
    assert "kernel_wdog2_selftest" not in scheduler
    assert "alloc_dma" not in load
    assert "kernel_event_emit" not in load
    assert "kernel_enter_el0_and_wait" not in load
    assert "0xbf000002" in load
    # New PM mechanism, not another GPIO/timer/mailbox-tag poke, no PWM pin-mux.
    assert "GPFSEL" not in load
    assert "PUP_PDN" not in load
    assert "ST_C0" not in load
    assert "ST_C2" not in load
    assert "CM_PWM" not in load
    assert "0x00030002" not in load
    assert "0x00030006" not in load


def test_no_boot_el0_enter_after_genet12() -> None:
    """sys_socket after GENET DMA I-aborted; do not ship a second boot EL0 enter."""
    app = read_repo("Sources/Application/Application.swift")
    after = app.split("kernel_genet12_selftest", 1)[1]
    assert "kernel_enter_el0_and_wait" not in after
    assert "kernel_socket_selftest" not in after
    assert "kernel_wdog2_selftest" in after

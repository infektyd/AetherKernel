"""EPIC D/G V97: mailbox GET_CLOCK_RATE (ARM) after GENET. No EL0. Honest Hz."""

import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_mboxc_clock_rate_without_el0() -> None:
    load = read_repo("Sources/Support/kernel_mboxc.c")
    mbox = read_repo("Sources/Support/kernel_vc_mbox.c")
    support = read_repo("Sources/Support/include/Support.h")
    app = read_repo("Sources/Application/Application.swift")
    shell = read_repo("Sources/Application/UARTShell.swift")
    iterate = read_repo("scripts/netboot/net-iterate.sh")
    scheduler = read_repo("Sources/Support/kernel_scheduler.c")

    assert "0x00030002" in mbox
    assert "kernel_vc_mbox_get_clock_rate" in load
    assert "kernel_vc_mbox_get_clock_rate" in mbox
    assert "int           kernel_mboxc_selftest(void);" in support
    assert 'uartPuts("runtime v97: mailbox clock rate\\n")' in app
    assert 'uartPuts("mboxc ok=")' in app
    assert "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 97" not in app
    assert "func printMboxc()" in shell
    assert 'shellBufferSliceEquals(commandStart, commandLen, "mboxc")' in shell
    assert ",mboxt,mboxc" in shell
    assert 'grep -qa "mboxc ok=1 version=97 clk=3 hz="' in iterate
    assert 'probe_shell "mboxc" "^mboxc ok=1 version=97 clk=3 hz="' in iterate
    assert "ping -c 2" in iterate
    assert "SOCK_STREAM" in iterate
    assert "sched12" in iterate
    assert "kernel_mboxc_selftest" not in scheduler
    assert "alloc_dma" not in load
    assert "kernel_event_emit" not in load
    assert "kernel_enter_el0_and_wait" not in load
    assert "0xbf000002" in load
    # New mailbox tag, not another GPIO/timer poke, no PWM pin-mux, no UART pull.
    assert "GPFSEL" not in load
    assert "PUP_PDN" not in load
    assert "ST_C0" not in load
    assert "ST_C2" not in load
    assert "CM_PWM" not in load
    assert "CLK_ARM" in load or "clock_id = 3" in load or "clk_id = 3" in load


def test_no_boot_el0_enter_after_genet12() -> None:
    """sys_socket after GENET DMA I-aborted; do not ship a second boot EL0 enter."""
    app = read_repo("Sources/Application/Application.swift")
    after = app.split("kernel_genet12_selftest", 1)[1]
    assert "kernel_enter_el0_and_wait" not in after
    assert "kernel_socket_selftest" not in after
    assert "kernel_mboxc_selftest" in after

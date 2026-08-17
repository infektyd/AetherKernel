"""EPIC D/G V96: mailbox GET_TEMPERATURE after GENET. No EL0. Honest millidegrees."""

import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_mboxt_temperature_without_el0() -> None:
    load = read_repo("Sources/Support/kernel_mboxt.c")
    mbox = read_repo("Sources/Support/kernel_vc_mbox.c")
    support = read_repo("Sources/Support/include/Support.h")
    app = read_repo("Sources/Application/Application.swift")
    shell = read_repo("Sources/Application/UARTShell.swift")
    iterate = read_repo("scripts/netboot/net-iterate.sh")
    scheduler = read_repo("Sources/Support/kernel_scheduler.c")

    assert "0x00030006" in mbox
    assert "kernel_vc_mbox_get_temp" in load
    assert "kernel_vc_mbox_get_temp" in mbox
    assert "int           kernel_mboxt_selftest(void);" in support
    assert 'uartPuts("runtime v96: mailbox temperature\\n")' in app
    assert 'uartPuts("mboxt ok=")' in app
    assert "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 96" not in app
    assert "func printMboxt()" in shell
    assert 'shellBufferSliceEquals(commandStart, commandLen, "mboxt")' in shell
    assert ",stimer3,mboxt" in shell
    assert 'grep -qa "mboxt ok=1 version=96 temp="' in iterate
    assert 'probe_shell "mboxt" "^mboxt ok=1 version=96 temp="' in iterate
    assert "ping -c 2" in iterate
    assert "SOCK_STREAM" in iterate
    assert "sched12" in iterate
    assert "kernel_mboxt_selftest" not in scheduler
    assert "alloc_dma" not in load
    assert "kernel_event_emit" not in load
    assert "kernel_enter_el0_and_wait" not in load
    assert "0xbf000002" in load
    # Not another GPIO/timer poke, no PWM pin-mux, no UART pull.
    assert "GPFSEL" not in load
    assert "PUP_PDN" not in load
    assert "ST_C0" not in load
    assert "ST_C2" not in load
    assert "CM_PWM" not in load


def test_no_boot_el0_enter_after_genet12() -> None:
    """sys_socket after GENET DMA I-aborted; do not ship a second boot EL0 enter."""
    app = read_repo("Sources/Application/Application.swift")
    after = app.split("kernel_genet12_selftest", 1)[1]
    assert "kernel_enter_el0_and_wait" not in after
    assert "kernel_socket_selftest" not in after
    assert "kernel_mboxt_selftest" in after

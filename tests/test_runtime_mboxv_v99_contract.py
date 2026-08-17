"""EPIC D/G V99: mailbox GET_VOLTAGE (core) after GENET. No EL0. Honest microvolts."""

import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_mboxv_voltage_without_el0() -> None:
    load = read_repo("Sources/Support/kernel_mboxv.c")
    mbox = read_repo("Sources/Support/kernel_vc_mbox.c")
    support = read_repo("Sources/Support/include/Support.h")
    app = read_repo("Sources/Application/Application.swift")
    shell = read_repo("Sources/Application/UARTShell.swift")
    iterate = read_repo("scripts/netboot/net-iterate.sh")
    scheduler = read_repo("Sources/Support/kernel_scheduler.c")

    assert "0x00030003" in mbox
    assert "kernel_vc_mbox_get_voltage" in load
    assert "kernel_vc_mbox_get_voltage" in mbox
    assert "int           kernel_mboxv_selftest(void);" in support
    assert 'uartPuts("runtime v99: mailbox voltage\\n")' in app
    assert 'uartPuts("mboxv ok=")' in app
    assert "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 99" not in app
    assert "func printMboxv()" in shell
    assert 'shellBufferSliceEquals(commandStart, commandLen, "mboxv")' in shell
    assert ",wdog2,mboxv" in shell
    assert 'grep -qa "mboxv ok=1 version=99 id=1 uv="' in iterate
    assert 'probe_shell "mboxv" "^mboxv ok=1 version=99 id=1 uv="' in iterate
    assert "ping -c 2" in iterate
    assert "SOCK_STREAM" in iterate
    assert "sched12" in iterate
    assert "kernel_mboxv_selftest" not in scheduler
    assert "alloc_dma" not in load
    assert "kernel_event_emit" not in load
    assert "kernel_enter_el0_and_wait" not in load
    assert "0xbf000002" in load
    assert "watchdog_reset_now" not in load
    assert "watchdog_arm_seconds" not in load
    # New mailbox tag, not another GPIO/timer poke, no PWM pin-mux, no UART pull.
    assert "GPFSEL" not in load
    assert "PUP_PDN" not in load
    assert "ST_C0" not in load
    assert "ST_C2" not in load
    assert "CM_PWM" not in load
    assert "VOLT_CORE" in load or "volt_id = 1" in load or "id = 1" in load


def test_no_boot_el0_enter_after_genet12() -> None:
    """sys_socket after GENET DMA I-aborted; do not ship a second boot EL0 enter."""
    app = read_repo("Sources/Application/Application.swift")
    after = app.split("kernel_genet12_selftest", 1)[1]
    assert "kernel_enter_el0_and_wait" not in after
    assert "kernel_socket_selftest" not in after
    assert "kernel_mboxv_selftest" in after

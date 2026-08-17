"""EPIC G V100: BCM2711 RNG200 word after GENET. No EL0. Honest FIFO data."""

import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_rng200_without_el0() -> None:
    load = read_repo("Sources/Support/kernel_rng.c")
    support = read_repo("Sources/Support/include/Support.h")
    app = read_repo("Sources/Application/Application.swift")
    shell = read_repo("Sources/Application/UARTShell.swift")
    iterate = read_repo("scripts/netboot/net-iterate.sh")
    scheduler = read_repo("Sources/Support/kernel_scheduler.c")

    assert "0xFE104000" in load
    assert "0x20" in load
    assert "0x24" in load
    assert "int           kernel_rng_selftest(void);" in support
    assert 'uartPuts("runtime v100: RNG200 word\\n")' in app
    assert 'uartPuts("rng ok=")' in app
    assert "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 100" not in app
    assert "func printRng()" in shell
    assert 'shellBufferSliceEquals(commandStart, commandLen, "rng")' in shell
    assert ",mboxv,rng" in shell
    assert 'grep -qa "rng ok=1 version=100 ready=1 data="' in iterate
    assert 'probe_shell "rng" "^rng ok=1 version=100 ready=1 data="' in iterate
    assert "ping -c 2" in iterate
    assert "SOCK_STREAM" in iterate
    assert "sched12" in iterate
    assert "kernel_rng_selftest" not in scheduler
    assert "alloc_dma" not in load
    assert "kernel_event_emit" not in load
    assert "kernel_enter_el0_and_wait" not in load
    assert "0xbf000002" in load
    assert "watchdog_reset_now" not in load
    assert "watchdog_arm_seconds" not in load
    # New RNG200 block, not another GPIO/timer/mailbox-tag poke.
    assert "GPFSEL" not in load
    assert "PUP_PDN" not in load
    assert "ST_C0" not in load
    assert "ST_C2" not in load
    assert "CM_PWM" not in load
    assert "0x00030002" not in load
    assert "0x00030003" not in load
    assert "0x00030006" not in load


def test_no_boot_el0_enter_after_genet12() -> None:
    """sys_socket after GENET DMA I-aborted; do not ship a second boot EL0 enter."""
    app = read_repo("Sources/Application/Application.swift")
    after = app.split("kernel_genet12_selftest", 1)[1]
    assert "kernel_enter_el0_and_wait" not in after
    assert "kernel_socket_selftest" not in after
    assert "kernel_rng_selftest" in after

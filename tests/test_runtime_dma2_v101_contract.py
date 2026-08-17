"""EPIC G V101: BCM2711 DMA engine memcpy after GENET. No EL0. Fail-closed match."""

import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_dma2_memcpy_without_el0() -> None:
    load = read_repo("Sources/Support/kernel_dma2.c")
    support = read_repo("Sources/Support/include/Support.h")
    app = read_repo("Sources/Application/Application.swift")
    shell = read_repo("Sources/Application/UARTShell.swift")
    iterate = read_repo("scripts/netboot/net-iterate.sh")
    scheduler = read_repo("Sources/Support/kernel_scheduler.c")

    assert "0xFE007000" in load
    assert "0xFE007FF0" in load
    assert "kernel_dma_alloc_nc" in load
    assert "0xC0000000" in load
    assert "int           kernel_dma2_selftest(void);" in support
    assert 'uartPuts("runtime v101: DMA memcpy\\n")' in app
    assert 'uartPuts("dma2 ok=")' in app
    assert "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 101" not in app
    assert "func printDma2()" in shell
    assert 'shellBufferSliceEquals(commandStart, commandLen, "dma2")' in shell
    assert ",rng,dma2" in shell
    assert 'grep -qa "dma2 ok=1 version=101 chan=4 match=1 bytes=32"' in iterate
    assert 'probe_shell "dma2" "^dma2 ok=1 version=101 chan=4 match=1 bytes=32"' in iterate
    assert "ping -c 2" in iterate
    assert "SOCK_STREAM" in iterate
    assert "sched12" in iterate
    assert "kernel_dma2_selftest" not in scheduler
    assert "kernel_event_emit" not in load
    assert "kernel_enter_el0_and_wait" not in load
    assert "0xbf000002" in load
    assert "watchdog_reset_now" not in load
    assert "watchdog_arm_seconds" not in load
    # New DMA-engine memcpy, not another GPIO/timer/mailbox-tag poke.
    assert "GPFSEL" not in load
    assert "PUP_PDN" not in load
    assert "ST_C0" not in load
    assert "ST_C2" not in load
    assert "0xFE00300C" not in load
    assert "0xFE003014" not in load
    assert "CM_PWM" not in load
    assert "0x00030002" not in load
    assert "0x00030003" not in load
    assert "0x00030006" not in load
    assert "0xFE104000" not in load
    # CB must not live on the 4 KiB core0 stack.
    assert "kernel_dma_alloc_nc" in load


def test_no_boot_el0_enter_after_genet12() -> None:
    """sys_socket after GENET DMA I-aborted; do not ship a second boot EL0 enter."""
    app = read_repo("Sources/Application/Application.swift")
    after = app.split("kernel_genet12_selftest", 1)[1]
    assert "kernel_enter_el0_and_wait" not in after
    assert "kernel_socket_selftest" not in after
    assert "kernel_dma2_selftest" in after

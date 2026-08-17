"""EPIC H V85: reload config.txt after GENET. match=1 vs V57. No EL0."""

import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_sdload_reloads_fat32_without_el0() -> None:
    load = read_repo("Sources/Support/kernel_sdload.c")
    support = read_repo("Sources/Support/include/Support.h")
    app = read_repo("Sources/Application/Application.swift")
    shell = read_repo("Sources/Application/UARTShell.swift")
    iterate = read_repo("scripts/netboot/net-iterate.sh")
    scheduler = read_repo("Sources/Support/kernel_scheduler.c")

    assert "kernel_sdhci_fat32_read" in load
    assert "kernel_sdhci_fat32_selftest" in load
    assert "int           kernel_sdload_selftest(void);" in support
    assert 'uartPuts("runtime v85: SD config.txt reload\\n")' in app
    assert 'uartPuts("sdload ok=")' in app
    assert "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 85" not in app
    assert "func printSdload()" in shell
    assert 'shellBufferSliceEquals(commandStart, commandLen, "sdload")' in shell
    assert ",stimer,sdload" in shell
    assert 'grep -qa "sdload ok=1 version=85 file=config.txt bytes=.* checksum=.* match=1"' in iterate
    assert 'probe_shell "sdload" "^sdload ok=1 version=85 file=config.txt bytes=.* checksum=.* match=1"' in iterate
    assert "ping -c 2" in iterate
    assert "SOCK_STREAM" in iterate
    assert "sched12" in iterate
    assert "kernel_sdload_selftest" not in scheduler
    assert "alloc_dma" not in load
    assert "kernel_event_emit" not in load
    assert "kernel_enter_el0_and_wait" not in load
    assert "0xbf000002" in load


def test_no_boot_el0_enter_after_genet12() -> None:
    """sys_socket after GENET DMA I-aborted; do not ship a second boot EL0 enter."""
    app = read_repo("Sources/Application/Application.swift")
    after = app.split("kernel_genet12_selftest", 1)[1]
    assert "kernel_enter_el0_and_wait" not in after
    assert "kernel_socket_selftest" not in after
    assert "kernel_sdload_selftest" in after

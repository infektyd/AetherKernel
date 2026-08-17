"""EPIC H V88: walk FAT32 overlays/ after GENET. No EL0."""

import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_sdovl_walks_overlays_without_el0() -> None:
    walk = read_repo("Sources/Support/kernel_sdovl.c")
    sdhci = read_repo("Sources/Support/kernel_sdhci.c")
    support = read_repo("Sources/Support/include/Support.h")
    app = read_repo("Sources/Application/Application.swift")
    shell = read_repo("Sources/Application/UARTShell.swift")
    iterate = read_repo("scripts/netboot/net-iterate.sh")
    scheduler = read_repo("Sources/Support/kernel_scheduler.c")

    assert "kernel_sdhci_fat32_list_overlays" in walk
    assert "files >= 1" in walk or "files >= 1u" in walk
    assert "kernel_sdhci_fat32_list_overlays" in sdhci
    assert "OVERLAYS   " in sdhci
    assert "0x10" in sdhci
    assert "int           kernel_sdovl_selftest(void);" in support
    assert 'uartPuts("runtime v88: FAT32 overlays walk\\n")' in app
    assert 'uartPuts("sdovl ok=")' in app
    assert "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 88" not in app
    assert "func printSdovl()" in shell
    assert 'shellBufferSliceEquals(commandStart, commandLen, "sdovl")' in shell
    assert ",sdfile,sdovl" in shell
    assert 'grep -qa "sdovl ok=1 version=88 files=.* name="' in iterate
    assert 'probe_shell "sdovl" "^sdovl ok=1 version=88 files=.* name="' in iterate
    assert "ping -c 2" in iterate
    assert "SOCK_STREAM" in iterate
    assert "sched12" in iterate
    assert "kernel_sdovl_selftest" not in scheduler
    assert "alloc_dma" not in walk
    assert "kernel_event_emit" not in walk
    assert "kernel_enter_el0_and_wait" not in walk
    assert "0xbf000002" in walk
    ovl = sdhci.split("kernel_sdhci_fat32_list_overlays", 1)[1]
    assert "fat32_file_bytes" not in ovl
    assert "fat32_ok" not in ovl


def test_no_boot_el0_enter_after_genet12() -> None:
    """sys_socket after GENET DMA I-aborted; do not ship a second boot EL0 enter."""
    app = read_repo("Sources/Application/Application.swift")
    after = app.split("kernel_genet12_selftest", 1)[1]
    assert "kernel_enter_el0_and_wait" not in after
    assert "kernel_socket_selftest" not in after
    assert "kernel_sdovl_selftest" in after

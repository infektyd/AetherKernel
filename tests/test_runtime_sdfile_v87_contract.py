"""EPIC H V87: load a second FAT32 root file after GENET. No EL0."""

import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_sdfile_loads_second_root_file_without_el0() -> None:
    load = read_repo("Sources/Support/kernel_sdfile.c")
    sdhci = read_repo("Sources/Support/kernel_sdhci.c")
    support = read_repo("Sources/Support/include/Support.h")
    app = read_repo("Sources/Application/Application.swift")
    shell = read_repo("Sources/Application/UARTShell.swift")
    iterate = read_repo("scripts/netboot/net-iterate.sh")
    scheduler = read_repo("Sources/Support/kernel_scheduler.c")

    assert "kernel_sdhci_fat32_read_second" in load
    assert "bytes > 0" in load or "bytes > 0u" in load
    assert "kernel_sdhci_fat32_read_second" in sdhci
    assert "CONFIG  TXT" in sdhci
    assert "0x10" in sdhci  # skip directories (OVERLAYS)
    assert "65536" in sdhci
    assert "int           kernel_sdfile_selftest(void);" in support
    assert 'uartPuts("runtime v87: FAT32 second file\\n")' in app
    assert 'uartPuts("sdfile ok=")' in app
    assert "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 87" not in app
    assert "func printSdfile()" in shell
    assert 'shellBufferSliceEquals(commandStart, commandLen, "sdfile")' in shell
    assert ",sdls,sdfile" in shell
    assert 'grep -qa "sdfile ok=1 version=87 name=.* bytes=.* checksum="' in iterate
    assert 'probe_shell "sdfile" "^sdfile ok=1 version=87 name=.* bytes=.* checksum="' in iterate
    assert "ping -c 2" in iterate
    assert "SOCK_STREAM" in iterate
    assert "sched12" in iterate
    assert "kernel_sdfile_selftest" not in scheduler
    assert "alloc_dma" not in load
    assert "kernel_event_emit" not in load
    assert "kernel_enter_el0_and_wait" not in load
    assert "0xbf000002" in load
    second = sdhci.split("kernel_sdhci_fat32_read_second", 1)[1]
    assert "fat32_file_bytes" not in second
    assert "fat32_ok" not in second


def test_no_boot_el0_enter_after_genet12() -> None:
    """sys_socket after GENET DMA I-aborted; do not ship a second boot EL0 enter."""
    app = read_repo("Sources/Application/Application.swift")
    after = app.split("kernel_genet12_selftest", 1)[1]
    assert "kernel_enter_el0_and_wait" not in after
    assert "kernel_socket_selftest" not in after
    assert "kernel_sdfile_selftest" in after

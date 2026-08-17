"""EPIC H V89: load one overlays/ file after GENET. No EL0."""

import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_sdovf_loads_one_overlay_without_el0() -> None:
    load = read_repo("Sources/Support/kernel_sdovf.c")
    sdhci = read_repo("Sources/Support/kernel_sdhci.c")
    support = read_repo("Sources/Support/include/Support.h")
    app = read_repo("Sources/Application/Application.swift")
    shell = read_repo("Sources/Application/UARTShell.swift")
    iterate = read_repo("scripts/netboot/net-iterate.sh")
    scheduler = read_repo("Sources/Support/kernel_scheduler.c")

    assert "kernel_sdhci_fat32_read_overlay" in load
    assert "bytes > 0" in load or "bytes > 0u" in load
    assert "kernel_sdhci_fat32_read_overlay" in sdhci
    assert "OVERLAYS   " in sdhci
    assert "65536" in sdhci
    assert "int           kernel_sdovf_selftest(void);" in support
    assert 'uartPuts("runtime v89: FAT32 overlay file\\n")' in app
    assert 'uartPuts("sdovf ok=")' in app
    assert "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 89" not in app
    assert "func printSdovf()" in shell
    assert 'shellBufferSliceEquals(commandStart, commandLen, "sdovf")' in shell
    assert ",sdovl,sdovf" in shell
    assert 'grep -qa "sdovf ok=1 version=89 name=.* bytes=.* checksum="' in iterate
    assert 'probe_shell "sdovf" "^sdovf ok=1 version=89 name=.* bytes=.* checksum="' in iterate
    assert "ping -c 2" in iterate
    assert "SOCK_STREAM" in iterate
    assert "sched12" in iterate
    assert "kernel_sdovf_selftest" not in scheduler
    assert "alloc_dma" not in load
    assert "kernel_event_emit" not in load
    assert "kernel_enter_el0_and_wait" not in load
    assert "0xbf000002" in load
    ovf = sdhci.split("kernel_sdhci_fat32_read_overlay", 1)[1]
    assert "fat32_file_bytes" not in ovf
    assert "fat32_ok" not in ovf


def test_no_boot_el0_enter_after_genet12() -> None:
    """sys_socket after GENET DMA I-aborted; do not ship a second boot EL0 enter."""
    app = read_repo("Sources/Application/Application.swift")
    after = app.split("kernel_genet12_selftest", 1)[1]
    assert "kernel_enter_el0_and_wait" not in after
    assert "kernel_socket_selftest" not in after
    assert "kernel_sdovf_selftest" in after

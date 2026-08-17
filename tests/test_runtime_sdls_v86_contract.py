"""EPIC H V86: list FAT32 root after GENET. files>=2 + config. No EL0."""

import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_sdls_lists_root_without_el0() -> None:
    ls = read_repo("Sources/Support/kernel_sdls.c")
    sdhci = read_repo("Sources/Support/kernel_sdhci.c")
    support = read_repo("Sources/Support/include/Support.h")
    app = read_repo("Sources/Application/Application.swift")
    shell = read_repo("Sources/Application/UARTShell.swift")
    iterate = read_repo("scripts/netboot/net-iterate.sh")
    scheduler = read_repo("Sources/Support/kernel_scheduler.c")

    assert "kernel_sdhci_fat32_listdir" in ls
    assert "files >= 2" in ls or "files >= 2u" in ls
    assert "kernel_sdhci_fat32_listdir" in sdhci
    assert "CONFIG  TXT" in sdhci
    assert "int           kernel_sdls_selftest(void);" in support
    assert 'uartPuts("runtime v86: FAT32 root list\\n")' in app
    assert 'uartPuts("sdls ok=")' in app
    assert "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 86" not in app
    assert "func printSdls()" in shell
    assert 'shellBufferSliceEquals(commandStart, commandLen, "sdls")' in shell
    assert ",sdload,sdls" in shell
    assert 'grep -qa "sdls ok=1 version=86 files=.* config=1 other="' in iterate
    assert 'probe_shell "sdls" "^sdls ok=1 version=86 files=.* config=1 other="' in iterate
    assert "ping -c 2" in iterate
    assert "SOCK_STREAM" in iterate
    assert "sched12" in iterate
    assert "kernel_sdls_selftest" not in scheduler
    assert "alloc_dma" not in ls
    assert "kernel_event_emit" not in ls
    assert "kernel_enter_el0_and_wait" not in ls
    assert "0xbf000002" in ls
    # Do not clobber V57/V85 latches by assigning fat32_file_bytes in the list path.
    list_fn = sdhci.split("kernel_sdhci_fat32_listdir", 1)[1]
    assert "fat32_file_bytes" not in list_fn.split("kernel_sdhci_fat32_selftest", 1)[0]
    assert "fat32_ok" not in list_fn.split("kernel_sdhci_fat32_selftest", 1)[0]


def test_no_boot_el0_enter_after_genet12() -> None:
    """sys_socket after GENET DMA I-aborted; do not ship a second boot EL0 enter."""
    app = read_repo("Sources/Application/Application.swift")
    after = app.split("kernel_genet12_selftest", 1)[1]
    assert "kernel_enter_el0_and_wait" not in after
    assert "kernel_socket_selftest" not in after
    assert "kernel_sdls_selftest" in after

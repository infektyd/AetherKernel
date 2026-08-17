"""EPIC H V90: load FAT32 root issue.txt by name after GENET. Fail-closed. No EL0."""

import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_sdiss_loads_issue_txt_by_name_without_el0() -> None:
    load = read_repo("Sources/Support/kernel_sdiss.c")
    sdhci = read_repo("Sources/Support/kernel_sdhci.c")
    support = read_repo("Sources/Support/include/Support.h")
    app = read_repo("Sources/Application/Application.swift")
    shell = read_repo("Sources/Application/UARTShell.swift")
    iterate = read_repo("scripts/netboot/net-iterate.sh")
    scheduler = read_repo("Sources/Support/kernel_scheduler.c")

    assert "kernel_sdhci_fat32_read_issue" in load
    assert "ISSUE   TXT" in sdhci
    assert "kernel_sdhci_fat32_read_issue" in sdhci
    assert "65536" in sdhci
    assert "int           kernel_sdiss_selftest(void);" in support
    assert 'uartPuts("runtime v90: FAT32 issue.txt\\n")' in app
    assert 'uartPuts("sdiss ok=")' in app
    assert 'uartPuts(" present=")' in app
    assert "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 90" not in app
    assert "func printSdiss()" in shell
    assert 'shellBufferSliceEquals(commandStart, commandLen, "sdiss")' in shell
    assert ",sdovf,sdiss" in shell
    assert (
        'grep -qa "sdiss ok=[01] version=90 present=[01] name=.* bytes=.* checksum="'
        in iterate
    )
    assert (
        'probe_shell "sdiss" "^sdiss ok=[01] version=90 present=[01] name=.* bytes=.* checksum="'
        in iterate
    )
    assert "ping -c 2" in iterate
    assert "SOCK_STREAM" in iterate
    assert "sched12" in iterate
    assert "kernel_sdiss_selftest" not in scheduler
    assert "alloc_dma" not in load
    assert "kernel_event_emit" not in load
    assert "kernel_enter_el0_and_wait" not in load
    assert "0xbf000002" in load
    assert "sdiss_ok_val = 1" not in load.split("if (kernel_sdhci_fat32_read_issue", 1)[0]
    issue = sdhci.split("kernel_sdhci_fat32_read_issue", 1)[1]
    assert "fat32_file_bytes" not in issue
    assert "fat32_ok" not in issue
    assert "ISSUE   TXT" in issue
    assert "CMDLINE TXT" not in issue
    assert "CONFIG  TXT" not in issue


def test_no_boot_el0_enter_after_genet12() -> None:
    """sys_socket after GENET DMA I-aborted; do not ship a second boot EL0 enter."""
    app = read_repo("Sources/Application/Application.swift")
    after = app.split("kernel_genet12_selftest", 1)[1]
    assert "kernel_enter_el0_and_wait" not in after
    assert "kernel_socket_selftest" not in after
    assert "kernel_sdiss_selftest" in after

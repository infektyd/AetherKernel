"""EPIC H V102: SDHCI CMD24 write of a free FAT32 cluster after GENET. No EL0."""

import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_sdwr_free_cluster_without_el0() -> None:
    load = read_repo("Sources/Support/kernel_sdwr.c")
    sdhci = read_repo("Sources/Support/kernel_sdhci.c")
    support = read_repo("Sources/Support/include/Support.h")
    app = read_repo("Sources/Application/Application.swift")
    shell = read_repo("Sources/Application/UARTShell.swift")
    iterate = read_repo("scripts/netboot/net-iterate.sh")
    scheduler = read_repo("Sources/Support/kernel_scheduler.c")

    assert "kernel_sdhci_fat32_write_free" in load
    assert "kernel_sdhci_fat32_write_free" in sdhci
    assert "SDHCI_INT_WRITE_RDY" in sdhci
    write = sdhci.split("kernel_sdhci_fat32_write_free", 1)[1]
    assert "CMDTM_CMD(24" in write
    assert "0xA1020000" in write
    assert "fat32_file_bytes" not in write
    assert "fat32_ok" not in write
    assert "CONFIG  TXT" not in write
    assert "CMDLINE TXT" not in write
    assert "ISSUE   TXT" not in write
    assert "int           kernel_sdwr_selftest(void);" in support
    assert 'uartPuts("runtime v102: SD free-cluster write\\n")' in app
    assert 'uartPuts("sdwr ok=")' in app
    assert "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 102" not in app
    assert "func printSdwr()" in shell
    assert 'shellBufferSliceEquals(commandStart, commandLen, "sdwr")' in shell
    assert ",dma2,sdwr" in shell
    assert 'grep -qa "sdwr ok=1 version=102 match=1 bytes=512 clus="' in iterate
    assert 'probe_shell "sdwr" "^sdwr ok=1 version=102 match=1 bytes=512 clus="' in iterate
    assert "ping -c 2" in iterate
    assert "SOCK_STREAM" in iterate
    assert "sched12" in iterate
    assert "kernel_sdwr_selftest" not in scheduler
    assert "kernel_event_emit" not in load
    assert "kernel_enter_el0_and_wait" not in load
    assert "0xbf000002" in load
    assert "watchdog_reset_now" not in load
    assert "watchdog_arm_seconds" not in load
    assert "unsigned int" not in load or "[" not in load
    assert "GPFSEL" not in load
    assert "PUP_PDN" not in load
    assert "ST_C0" not in load
    assert "ST_C2" not in load
    assert "0xFE00300C" not in load
    assert "0xFE003014" not in load
    assert "0x00030002" not in load
    assert "0x00030003" not in load
    assert "0x00030006" not in load


def test_no_boot_el0_enter_after_genet12() -> None:
    """sys_socket after GENET DMA I-aborted; do not ship a second boot EL0 enter."""
    app = read_repo("Sources/Application/Application.swift")
    after = app.split("kernel_genet12_selftest", 1)[1]
    assert "kernel_enter_el0_and_wait" not in after
    assert "kernel_socket_selftest" not in after
    assert "kernel_sdwr_selftest" in after

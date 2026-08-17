"""EPIC C/H V114: FAT32 backup boot sector after GENET. No EL0. Not an SDHCI command."""

import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_sdfb_backup_boot_without_el0() -> None:
    load = read_repo("Sources/Support/kernel_sdfb.c")
    sdhci = read_repo("Sources/Support/kernel_sdhci.c")
    support = read_repo("Sources/Support/include/Support.h")
    app = read_repo("Sources/Application/Application.swift")
    shell = read_repo("Sources/Application/UARTShell.swift")
    iterate = read_repo("scripts/netboot/net-iterate.sh")
    scheduler = read_repo("Sources/Support/kernel_scheduler.c")

    assert "kernel_sdhci_fat32_backup" in load
    assert "kernel_sdhci_fat32_backup" in sdhci
    bk = sdhci.split("kernel_sdhci_fat32_backup", 1)[1]
    assert "50u" in bk
    assert "sdhci_read_block_pio" in bk
    assert "sdhci_write_block_pio" not in bk
    assert "CMDTM_CMD(24" not in bk
    assert "CMDTM_CMD(13" not in bk
    assert "CMDTM_CMD(51" not in bk
    assert "CMDTM_CMD(18" not in bk
    assert "CMDTM_CMD(6" not in bk
    assert "(23u << 24)" not in bk
    assert "(25u << 24)" not in bk
    assert "TM_AUTO_CMD12" not in bk
    assert "SDHCI_C0_HCTL_DWIDTH" not in bk
    assert "fat32_set_entry" not in bk
    assert "fat32_file_bytes" not in bk
    assert "fat32_ok" not in bk
    assert "CONFIG  TXT" not in bk
    assert "CMDLINE TXT" not in bk
    assert "ISSUE   TXT" not in bk
    assert "OVERLAYS   " not in bk
    assert "AETHER  TMP" not in bk
    assert "int           kernel_sdfb_selftest(void);" in support
    assert 'uartPuts("runtime v114: FAT32 backup boot\\n")' in app
    assert 'uartPuts("sdfb ok=")' in app
    assert "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 114" not in app
    assert "func printSdfb()" in shell
    assert 'shellBufferSliceEquals(commandStart, commandLen, "sdfb")' in shell
    assert ",sdfi,sdfb" in shell
    assert 'grep -qa "sdfb ok=1 version=114 match="' in iterate
    assert 'probe_shell "sdfb" "^sdfb ok=1 version=114 match="' in iterate
    assert "ping -c 2" in iterate
    assert "SOCK_STREAM" in iterate
    assert "sched12" in iterate
    assert "kernel_sdfb_selftest" not in scheduler
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


def test_no_boot_el0_enter_after_genet12() -> None:
    """sys_socket after GENET DMA I-aborted; do not ship a second boot EL0 enter."""
    app = read_repo("Sources/Application/Application.swift")
    after = app.split("kernel_genet12_selftest", 1)[1]
    assert "kernel_enter_el0_and_wait" not in after
    assert "kernel_socket_selftest" not in after
    assert "kernel_sdfb_selftest" in after

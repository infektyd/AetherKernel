"""EPIC C/H V115: FAT32 FAT-mirror compare after GENET. No EL0. Not an SDHCI command."""

import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_sdfm_fat_mirror_without_el0() -> None:
    load = read_repo("Sources/Support/kernel_sdfm.c")
    sdhci = read_repo("Sources/Support/kernel_sdhci.c")
    support = read_repo("Sources/Support/include/Support.h")
    app = read_repo("Sources/Application/Application.swift")
    shell = read_repo("Sources/Application/UARTShell.swift")
    iterate = read_repo("scripts/netboot/net-iterate.sh")
    scheduler = read_repo("Sources/Support/kernel_scheduler.c")

    assert "kernel_sdhci_fat32_mirror" in load
    assert "kernel_sdhci_fat32_mirror" in sdhci
    mi = sdhci.split("kernel_sdhci_fat32_mirror", 1)[1]
    assert "16u" in mi
    assert "36u" in mi
    assert "sdhci_read_block_pio" in mi
    assert "sdhci_write_block_pio" not in mi
    assert "CMDTM_CMD(24" not in mi
    assert "CMDTM_CMD(13" not in mi
    assert "CMDTM_CMD(51" not in mi
    assert "CMDTM_CMD(18" not in mi
    assert "CMDTM_CMD(6" not in mi
    assert "(23u << 24)" not in mi
    assert "(25u << 24)" not in mi
    assert "TM_AUTO_CMD12" not in mi
    assert "SDHCI_C0_HCTL_DWIDTH" not in mi
    assert "fat32_set_entry" not in mi
    assert "fat32_file_bytes" not in mi
    assert "fat32_ok" not in mi
    assert "CONFIG  TXT" not in mi
    assert "CMDLINE TXT" not in mi
    assert "ISSUE   TXT" not in mi
    assert "OVERLAYS   " not in mi
    assert "AETHER  TMP" not in mi
    assert "int           kernel_sdfm_selftest(void);" in support
    assert 'uartPuts("runtime v115: FAT32 FAT mirror\\n")' in app
    assert 'uartPuts("sdfm ok=")' in app
    assert "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 115" not in app
    assert "func printSdfm()" in shell
    assert 'shellBufferSliceEquals(commandStart, commandLen, "sdfm")' in shell
    assert ",sdfb,sdfm" in shell
    assert 'grep -qa "sdfm ok=1 version=115 match="' in iterate
    assert 'probe_shell "sdfm" "^sdfm ok=1 version=115 match="' in iterate
    assert "ping -c 2" in iterate
    assert "SOCK_STREAM" in iterate
    assert "sched12" in iterate
    assert "kernel_sdfm_selftest" not in scheduler
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
    assert "kernel_sdfm_selftest" in after

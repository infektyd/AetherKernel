"""EPIC C/H V116: FAT32 scratch unlink after GENET. No EL0. Not FAT-metadata parse."""

import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_sdrm_unlink_scratch_without_el0() -> None:
    load = read_repo("Sources/Support/kernel_sdrm.c")
    sdhci = read_repo("Sources/Support/kernel_sdhci.c")
    support = read_repo("Sources/Support/include/Support.h")
    app = read_repo("Sources/Application/Application.swift")
    shell = read_repo("Sources/Application/UARTShell.swift")
    iterate = read_repo("scripts/netboot/net-iterate.sh")
    scheduler = read_repo("Sources/Support/kernel_scheduler.c")

    assert "kernel_sdhci_fat32_unlink_scratch" in load
    assert "kernel_sdhci_fat32_unlink_scratch" in sdhci
    un = sdhci.split("kernel_sdhci_fat32_unlink_scratch", 1)[1]
    assert "0xE5u" in un
    assert "0x41455448" in un
    assert "0xA1030000" in un
    assert "(24u << 24)" in un
    assert "sdhci_write_block_pio" not in un
    assert "CMDTM_CMD(24" not in un
    assert "CMDTM_CMD(13" not in un
    assert "CMDTM_CMD(51" not in un
    assert "CMDTM_CMD(18" not in un
    assert "CMDTM_CMD(6" not in un
    assert "(23u << 24)" not in un
    assert "(25u << 24)" not in un
    assert "TM_AUTO_CMD12" not in un
    assert "SDHCI_C0_HCTL_DWIDTH" not in un
    assert "fat32_set_entry" not in un
    assert "fat32_file_bytes" not in un
    assert "fat32_ok" not in un
    assert "CONFIG  TXT" not in un
    assert "CMDLINE TXT" not in un
    assert "ISSUE   TXT" not in un
    assert "OVERLAYS   " not in un
    assert "AETHER  TMP" not in un
    assert "int           kernel_sdrm_selftest(void);" in support
    assert 'uartPuts("runtime v116: FAT32 scratch unlink\\n")' in app
    assert 'uartPuts("sdrm ok=")' in app
    assert "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 116" not in app
    assert "func printSdrm()" in shell
    assert 'shellBufferSliceEquals(commandStart, commandLen, "sdrm")' in shell
    assert ",sdfm,sdrm" in shell
    assert 'grep -qa "sdrm ok=1 version=116 deleted="' in iterate
    assert 'probe_shell "sdrm" "^sdrm ok=1 version=116 deleted="' in iterate
    assert "ping -c 2" in iterate
    assert "SOCK_STREAM" in iterate
    assert "sched12" in iterate
    assert "kernel_sdrm_selftest" not in scheduler
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
    assert "kernel_sdrm_selftest" in after

"""EPIC H V104: re-read AETHER.TMP by name after GENET. Read-only. No EL0."""

import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_sdrd_rereads_scratch_without_el0() -> None:
    load = read_repo("Sources/Support/kernel_sdrd.c")
    sdhci = read_repo("Sources/Support/kernel_sdhci.c")
    support = read_repo("Sources/Support/include/Support.h")
    app = read_repo("Sources/Application/Application.swift")
    shell = read_repo("Sources/Application/UARTShell.swift")
    iterate = read_repo("scripts/netboot/net-iterate.sh")
    scheduler = read_repo("Sources/Support/kernel_scheduler.c")

    assert "kernel_sdhci_fat32_read_scratch" in load
    assert "kernel_sdhci_fat32_read_scratch" in sdhci
    read = sdhci.split("kernel_sdhci_fat32_read_scratch", 1)[1]
    assert "AETHER  TMP" in read
    assert "0xA1030000" in read or "fat32_scratch_data_match" in read
    assert "sdhci_write_block_pio" not in read
    assert "CMDTM_CMD(24" not in read
    assert "fat32_set_entry" not in read
    assert "fat32_file_bytes" not in read
    assert "fat32_ok" not in read
    assert "CONFIG  TXT" not in read
    assert "CMDLINE TXT" not in read
    assert "ISSUE   TXT" not in read
    assert "OVERLAYS   " not in read
    assert "int           kernel_sdrd_selftest(void);" in support
    assert 'uartPuts("runtime v104: FAT32 scratch reread\\n")' in app
    assert 'uartPuts("sdrd ok=")' in app
    assert "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 104" not in app
    assert "func printSdrd()" in shell
    assert 'shellBufferSliceEquals(commandStart, commandLen, "sdrd")' in shell
    assert ",sdmk,sdrd" in shell
    assert 'grep -qa "sdrd ok=1 version=104 match=1 present=1 name="' in iterate
    assert 'probe_shell "sdrd" "^sdrd ok=1 version=104 match=1 present=1 name="' in iterate
    assert "ping -c 2" in iterate
    assert "SOCK_STREAM" in iterate
    assert "sched12" in iterate
    assert "kernel_sdrd_selftest" not in scheduler
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
    assert "kernel_sdrd_selftest" in after

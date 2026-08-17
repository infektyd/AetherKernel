"""EPIC C/H V107: SDHCI ACMD13 SD_STATUS after GENET. No EL0. No named-file write."""

import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_sdss_acmd13_without_el0() -> None:
    load = read_repo("Sources/Support/kernel_sdss.c")
    sdhci = read_repo("Sources/Support/kernel_sdhci.c")
    support = read_repo("Sources/Support/include/Support.h")
    app = read_repo("Sources/Application/Application.swift")
    shell = read_repo("Sources/Application/UARTShell.swift")
    iterate = read_repo("scripts/netboot/net-iterate.sh")
    scheduler = read_repo("Sources/Support/kernel_scheduler.c")

    assert "kernel_sdhci_card_sd_status" in load
    assert "kernel_sdhci_card_sd_status" in sdhci
    st = sdhci.split("kernel_sdhci_card_sd_status", 1)[1]
    assert "CMDTM_CMD(55" in st
    assert "CMDTM_CMD(13" in st
    assert "sdhci_write_block_pio" not in st
    assert "CMDTM_CMD(24" not in st
    assert "fat32_set_entry" not in st
    assert "fat32_file_bytes" not in st
    assert "fat32_ok" not in st
    assert "CONFIG  TXT" not in st
    assert "CMDLINE TXT" not in st
    assert "ISSUE   TXT" not in st
    assert "OVERLAYS   " not in st
    assert "AETHER  TMP" not in st
    assert "int           kernel_sdss_selftest(void);" in support
    assert 'uartPuts("runtime v107: SDHCI SD_STATUS\\n")' in app
    assert 'uartPuts("sdss ok=")' in app
    assert "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 107" not in app
    assert "func printSdss()" in shell
    assert 'shellBufferSliceEquals(commandStart, commandLen, "sdss")' in shell
    assert ",sdscr,sdss" in shell
    assert 'grep -qa "sdss ok=1 version=107 type="' in iterate
    assert 'probe_shell "sdss" "^sdss ok=1 version=107 type="' in iterate
    assert "ping -c 2" in iterate
    assert "SOCK_STREAM" in iterate
    assert "sched12" in iterate
    assert "kernel_sdss_selftest" not in scheduler
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
    assert "kernel_sdss_selftest" in after

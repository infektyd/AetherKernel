"""EPIC C/H V109: SDHCI CMD18 multi-block read after GENET. No EL0. Not a status clone."""

import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_sdmb_cmd18_without_el0() -> None:
    load = read_repo("Sources/Support/kernel_sdmb.c")
    sdhci = read_repo("Sources/Support/kernel_sdhci.c")
    support = read_repo("Sources/Support/include/Support.h")
    app = read_repo("Sources/Application/Application.swift")
    shell = read_repo("Sources/Application/UARTShell.swift")
    iterate = read_repo("scripts/netboot/net-iterate.sh")
    scheduler = read_repo("Sources/Support/kernel_scheduler.c")

    assert "kernel_sdhci_card_multiblock" in load
    assert "kernel_sdhci_card_multiblock" in sdhci
    mb = sdhci.split("kernel_sdhci_card_multiblock", 1)[1]
    assert "CMDTM_CMD(18" in mb
    assert "TM_MULTI_BLOCK" in mb
    assert "sdhci_write_block_pio" not in mb
    assert "CMDTM_CMD(24" not in mb
    assert "CMDTM_CMD(13" not in mb
    assert "CMDTM_CMD(51" not in mb
    assert "CMDTM_CMD(6" not in mb
    assert "SDHCI_C0_HCTL_DWIDTH" not in mb
    assert "fat32_set_entry" not in mb
    assert "fat32_file_bytes" not in mb
    assert "fat32_ok" not in mb
    assert "CONFIG  TXT" not in mb
    assert "CMDLINE TXT" not in mb
    assert "ISSUE   TXT" not in mb
    assert "OVERLAYS   " not in mb
    assert "AETHER  TMP" not in mb
    assert "int           kernel_sdmb_selftest(void);" in support
    assert 'uartPuts("runtime v109: SDHCI multi-block\\n")' in app
    assert 'uartPuts("sdmb ok=")' in app
    assert "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 109" not in app
    assert "func printSdmb()" in shell
    assert 'shellBufferSliceEquals(commandStart, commandLen, "sdmb")' in shell
    assert ",sdbus,sdmb" in shell
    assert 'grep -qa "sdmb ok=1 version=109 blocks="' in iterate
    assert 'probe_shell "sdmb" "^sdmb ok=1 version=109 blocks="' in iterate
    assert "ping -c 2" in iterate
    assert "SOCK_STREAM" in iterate
    assert "sched12" in iterate
    assert "kernel_sdmb_selftest" not in scheduler
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
    assert "kernel_sdmb_selftest" in after

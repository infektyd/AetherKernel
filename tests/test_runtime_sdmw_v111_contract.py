"""EPIC C/H V111: SDHCI CMD25 multi-block write after GENET. No EL0. Not a status clone."""

import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_sdmw_cmd25_without_el0() -> None:
    load = read_repo("Sources/Support/kernel_sdmw.c")
    sdhci = read_repo("Sources/Support/kernel_sdhci.c")
    support = read_repo("Sources/Support/include/Support.h")
    app = read_repo("Sources/Application/Application.swift")
    shell = read_repo("Sources/Application/UARTShell.swift")
    iterate = read_repo("scripts/netboot/net-iterate.sh")
    scheduler = read_repo("Sources/Support/kernel_scheduler.c")

    assert "kernel_sdhci_card_multiwrite" in load
    assert "kernel_sdhci_card_multiwrite" in sdhci
    mw = sdhci.split("kernel_sdhci_card_multiwrite", 1)[1]
    assert "(25u << 24)" in mw
    assert "TM_MULTI_BLOCK" in mw
    assert "SDHCI_INT_WRITE_RDY" in mw
    assert "sdhci_write_block_pio" not in mw
    assert "CMDTM_CMD(24" not in mw
    assert "CMDTM_CMD(13" not in mw
    assert "CMDTM_CMD(51" not in mw
    assert "CMDTM_CMD(18" not in mw
    assert "CMDTM_CMD(6" not in mw
    assert "SDHCI_C0_HCTL_DWIDTH" not in mw
    assert "fat32_set_entry" not in mw
    assert "fat32_file_bytes" not in mw
    assert "fat32_ok" not in mw
    assert "CONFIG  TXT" not in mw
    assert "CMDLINE TXT" not in mw
    assert "ISSUE   TXT" not in mw
    assert "OVERLAYS   " not in mw
    assert "AETHER  TMP" not in mw
    assert "int           kernel_sdmw_selftest(void);" in support
    assert 'uartPuts("runtime v111: SDHCI multi-block write\\n")' in app
    assert 'uartPuts("sdmw ok=")' in app
    assert "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 111" not in app
    assert "func printSdmw()" in shell
    assert 'shellBufferSliceEquals(commandStart, commandLen, "sdmw")' in shell
    assert ",sdsw,sdmw" in shell
    assert 'grep -qa "sdmw ok=1 version=111 match="' in iterate
    assert 'probe_shell "sdmw" "^sdmw ok=1 version=111 match="' in iterate
    assert "ping -c 2" in iterate
    assert "SOCK_STREAM" in iterate
    assert "sched12" in iterate
    assert "kernel_sdmw_selftest" not in scheduler
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
    assert "kernel_sdmw_selftest" in after

"""EPIC C/H V110: SDHCI CMD6 SWITCH_FUNC check after GENET. No EL0. Not ACMD6."""

import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_sdsw_cmd6_check_without_el0() -> None:
    load = read_repo("Sources/Support/kernel_sdsw.c")
    sdhci = read_repo("Sources/Support/kernel_sdhci.c")
    support = read_repo("Sources/Support/include/Support.h")
    app = read_repo("Sources/Application/Application.swift")
    shell = read_repo("Sources/Application/UARTShell.swift")
    iterate = read_repo("scripts/netboot/net-iterate.sh")
    scheduler = read_repo("Sources/Support/kernel_scheduler.c")

    assert "kernel_sdhci_card_switch" in load
    assert "kernel_sdhci_card_switch" in sdhci
    sw = sdhci.split("kernel_sdhci_card_switch", 1)[1]
    assert "(6u << 24)" in sw
    assert "CMD_IS_DATA" in sw
    assert "64u" in sw
    assert "CMDTM_CMD(55" not in sw
    assert "sdhci_write_block_pio" not in sw
    assert "CMDTM_CMD(24" not in sw
    assert "CMDTM_CMD(13" not in sw
    assert "CMDTM_CMD(51" not in sw
    assert "CMDTM_CMD(18" not in sw
    assert "SDHCI_C0_HCTL_DWIDTH" not in sw
    assert "fat32_set_entry" not in sw
    assert "fat32_file_bytes" not in sw
    assert "fat32_ok" not in sw
    assert "CONFIG  TXT" not in sw
    assert "CMDLINE TXT" not in sw
    assert "ISSUE   TXT" not in sw
    assert "OVERLAYS   " not in sw
    assert "AETHER  TMP" not in sw
    assert "int           kernel_sdsw_selftest(void);" in support
    assert 'uartPuts("runtime v110: SDHCI switch check\\n")' in app
    assert 'uartPuts("sdsw ok=")' in app
    assert "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 110" not in app
    assert "func printSdsw()" in shell
    assert 'shellBufferSliceEquals(commandStart, commandLen, "sdsw")' in shell
    assert ",sdmb,sdsw" in shell
    assert 'grep -qa "sdsw ok=1 version=110 grp1="' in iterate
    assert 'probe_shell "sdsw" "^sdsw ok=1 version=110 grp1="' in iterate
    assert "ping -c 2" in iterate
    assert "SOCK_STREAM" in iterate
    assert "sched12" in iterate
    assert "kernel_sdsw_selftest" not in scheduler
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
    assert "kernel_sdsw_selftest" in after

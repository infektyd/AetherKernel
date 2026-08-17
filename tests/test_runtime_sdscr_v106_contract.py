"""EPIC C/H V106: SDHCI ACMD51 SEND_SCR after GENET. No EL0. No named-file write."""

import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_sdscr_acmd51_without_el0() -> None:
    load = read_repo("Sources/Support/kernel_sdscr.c")
    sdhci = read_repo("Sources/Support/kernel_sdhci.c")
    support = read_repo("Sources/Support/include/Support.h")
    app = read_repo("Sources/Application/Application.swift")
    shell = read_repo("Sources/Application/UARTShell.swift")
    iterate = read_repo("scripts/netboot/net-iterate.sh")
    scheduler = read_repo("Sources/Support/kernel_scheduler.c")

    assert "kernel_sdhci_card_scr" in load
    assert "kernel_sdhci_card_scr" in sdhci
    scr = sdhci.split("kernel_sdhci_card_scr", 1)[1]
    assert "CMDTM_CMD(55" in scr
    assert "CMDTM_CMD(51" in scr
    assert "sdhci_write_block_pio" not in scr
    assert "CMDTM_CMD(24" not in scr
    assert "fat32_set_entry" not in scr
    assert "fat32_file_bytes" not in scr
    assert "fat32_ok" not in scr
    assert "CONFIG  TXT" not in scr
    assert "CMDLINE TXT" not in scr
    assert "ISSUE   TXT" not in scr
    assert "OVERLAYS   " not in scr
    assert "AETHER  TMP" not in scr
    assert "int           kernel_sdscr_selftest(void);" in support
    assert 'uartPuts("runtime v106: SDHCI send SCR\\n")' in app
    assert 'uartPuts("sdscr ok=")' in app
    assert "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 106" not in app
    assert "func printSdscr()" in shell
    assert 'shellBufferSliceEquals(commandStart, commandLen, "sdscr")' in shell
    assert ",sdst,sdscr" in shell
    assert 'grep -qa "sdscr ok=1 version=106 spec="' in iterate
    assert 'probe_shell "sdscr" "^sdscr ok=1 version=106 spec="' in iterate
    assert "ping -c 2" in iterate
    assert "SOCK_STREAM" in iterate
    assert "sched12" in iterate
    assert "kernel_sdscr_selftest" not in scheduler
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
    assert "kernel_sdscr_selftest" in after

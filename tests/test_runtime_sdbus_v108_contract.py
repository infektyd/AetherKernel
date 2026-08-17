"""EPIC C/H V108: SDHCI ACMD6 SET_BUS_WIDTH after GENET. No EL0. Not a status clone."""

import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_sdbus_acmd6_without_el0() -> None:
    load = read_repo("Sources/Support/kernel_sdbus.c")
    sdhci = read_repo("Sources/Support/kernel_sdhci.c")
    support = read_repo("Sources/Support/include/Support.h")
    app = read_repo("Sources/Application/Application.swift")
    shell = read_repo("Sources/Application/UARTShell.swift")
    iterate = read_repo("scripts/netboot/net-iterate.sh")
    scheduler = read_repo("Sources/Support/kernel_scheduler.c")

    assert "kernel_sdhci_card_bus_width" in load
    assert "kernel_sdhci_card_bus_width" in sdhci
    bw = sdhci.split("kernel_sdhci_card_bus_width", 1)[1]
    assert "CMDTM_CMD(55" in bw
    assert "CMDTM_CMD(6" in bw
    assert "SDHCI_C0_HCTL_DWIDTH" in bw
    assert "sdhci_read_block_pio" in bw
    assert "sdhci_write_block_pio" not in bw
    assert "CMDTM_CMD(24" not in bw
    assert "CMDTM_CMD(13" not in bw
    assert "CMDTM_CMD(51" not in bw
    assert "fat32_set_entry" not in bw
    assert "fat32_file_bytes" not in bw
    assert "fat32_ok" not in bw
    assert "CONFIG  TXT" not in bw
    assert "CMDLINE TXT" not in bw
    assert "ISSUE   TXT" not in bw
    assert "OVERLAYS   " not in bw
    assert "AETHER  TMP" not in bw
    assert "int           kernel_sdbus_selftest(void);" in support
    assert 'uartPuts("runtime v108: SDHCI bus width\\n")' in app
    assert 'uartPuts("sdbus ok=")' in app
    assert "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 108" not in app
    assert "func printSdbus()" in shell
    assert 'shellBufferSliceEquals(commandStart, commandLen, "sdbus")' in shell
    assert ",sdss,sdbus" in shell
    assert 'grep -qa "sdbus ok=1 version=108 bits="' in iterate
    assert 'probe_shell "sdbus" "^sdbus ok=1 version=108 bits="' in iterate
    assert "ping -c 2" in iterate
    assert "SOCK_STREAM" in iterate
    assert "sched12" in iterate
    assert "kernel_sdbus_selftest" not in scheduler
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
    assert "kernel_sdbus_selftest" in after

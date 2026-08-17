"""EPIC C/H V112: SDHCI CMD23 SET_BLOCK_COUNT after GENET. No EL0. Not AUTO_CMD12."""

import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_sdbc_cmd23_without_el0() -> None:
    load = read_repo("Sources/Support/kernel_sdbc.c")
    sdhci = read_repo("Sources/Support/kernel_sdhci.c")
    support = read_repo("Sources/Support/include/Support.h")
    app = read_repo("Sources/Application/Application.swift")
    shell = read_repo("Sources/Application/UARTShell.swift")
    iterate = read_repo("scripts/netboot/net-iterate.sh")
    scheduler = read_repo("Sources/Support/kernel_scheduler.c")

    assert "kernel_sdhci_card_blockcount" in load
    assert "kernel_sdhci_card_blockcount" in sdhci
    bc = sdhci.split("kernel_sdhci_card_blockcount", 1)[1]
    assert "(23u << 24)" in bc
    assert "(18u << 24)" in bc
    assert "TM_MULTI_BLOCK" in bc
    assert "TM_AUTO_CMD12" not in bc
    assert "sdhci_write_block_pio" not in bc
    assert "CMDTM_CMD(24" not in bc
    assert "CMDTM_CMD(13" not in bc
    assert "CMDTM_CMD(51" not in bc
    assert "CMDTM_CMD(18" not in bc
    assert "CMDTM_CMD(6" not in bc
    assert "SDHCI_C0_HCTL_DWIDTH" not in bc
    assert "fat32_set_entry" not in bc
    assert "fat32_file_bytes" not in bc
    assert "fat32_ok" not in bc
    assert "CONFIG  TXT" not in bc
    assert "CMDLINE TXT" not in bc
    assert "ISSUE   TXT" not in bc
    assert "OVERLAYS   " not in bc
    assert "AETHER  TMP" not in bc
    assert "int           kernel_sdbc_selftest(void);" in support
    assert 'uartPuts("runtime v112: SDHCI set block count\\n")' in app
    assert 'uartPuts("sdbc ok=")' in app
    assert "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 112" not in app
    assert "func printSdbc()" in shell
    assert 'shellBufferSliceEquals(commandStart, commandLen, "sdbc")' in shell
    assert ",sdmw,sdbc" in shell
    assert 'grep -qa "sdbc ok=1 version=112 count="' in iterate
    assert 'probe_shell "sdbc" "^sdbc ok=1 version=112 count="' in iterate
    assert "ping -c 2" in iterate
    assert "SOCK_STREAM" in iterate
    assert "sched12" in iterate
    assert "kernel_sdbc_selftest" not in scheduler
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
    assert "kernel_sdbc_selftest" in after

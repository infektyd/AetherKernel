"""EPIC H V103: FAT32 create/link of AETHER.TMP after GENET. No EL0. No boot files."""

import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_sdmk_creates_scratch_without_el0() -> None:
    load = read_repo("Sources/Support/kernel_sdmk.c")
    sdhci = read_repo("Sources/Support/kernel_sdhci.c")
    support = read_repo("Sources/Support/include/Support.h")
    app = read_repo("Sources/Application/Application.swift")
    shell = read_repo("Sources/Application/UARTShell.swift")
    iterate = read_repo("scripts/netboot/net-iterate.sh")
    scheduler = read_repo("Sources/Support/kernel_scheduler.c")

    assert "kernel_sdhci_fat32_create_scratch" in load
    assert "kernel_sdhci_fat32_create_scratch" in sdhci
    create = sdhci.split("kernel_sdhci_fat32_create_scratch", 1)[1]
    assert "AETHER  TMP" in create
    assert "0xA1030000" in create
    assert "0x0FFFFFF8" in create
    assert "fat32_file_bytes" not in create
    assert "fat32_ok" not in create
    assert "CONFIG  TXT" not in create
    assert "CMDLINE TXT" not in create
    assert "ISSUE   TXT" not in create
    assert "OVERLAYS   " not in create
    assert "int           kernel_sdmk_selftest(void);" in support
    assert 'uartPuts("runtime v103: FAT32 scratch create\\n")' in app
    assert 'uartPuts("sdmk ok=")' in app
    assert "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 103" not in app
    assert "func printSdmk()" in shell
    assert 'shellBufferSliceEquals(commandStart, commandLen, "sdmk")' in shell
    assert ",sdwr,sdmk" in shell
    assert 'grep -qa "sdmk ok=1 version=103 match=1 created="' in iterate
    assert 'probe_shell "sdmk" "^sdmk ok=1 version=103 match=1 created="' in iterate
    assert "ping -c 2" in iterate
    assert "SOCK_STREAM" in iterate
    assert "sched12" in iterate
    assert "kernel_sdmk_selftest" not in scheduler
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
    assert "0xFE00300C" not in load
    assert "0xFE003014" not in load


def test_no_boot_el0_enter_after_genet12() -> None:
    """sys_socket after GENET DMA I-aborted; do not ship a second boot EL0 enter."""
    app = read_repo("Sources/Application/Application.swift")
    after = app.split("kernel_genet12_selftest", 1)[1]
    assert "kernel_enter_el0_and_wait" not in after
    assert "kernel_socket_selftest" not in after
    assert "kernel_sdmk_selftest" in after

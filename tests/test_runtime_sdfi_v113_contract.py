"""EPIC C/H V113: FAT32 FSInfo sector parse after GENET. No EL0. Not an SDHCI command."""

import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_sdfi_fsinfo_without_el0() -> None:
    load = read_repo("Sources/Support/kernel_sdfi.c")
    sdhci = read_repo("Sources/Support/kernel_sdhci.c")
    support = read_repo("Sources/Support/include/Support.h")
    app = read_repo("Sources/Application/Application.swift")
    shell = read_repo("Sources/Application/UARTShell.swift")
    iterate = read_repo("scripts/netboot/net-iterate.sh")
    scheduler = read_repo("Sources/Support/kernel_scheduler.c")

    assert "kernel_sdhci_fat32_fsinfo" in load
    assert "kernel_sdhci_fat32_fsinfo" in sdhci
    fi = sdhci.split("kernel_sdhci_fat32_fsinfo", 1)[1]
    assert "0x41615252" in fi
    assert "0x61417272" in fi
    assert "sdhci_read_block_pio" in fi
    assert "48u" in fi
    assert "484u" in fi
    assert "488u" in fi
    assert "sdhci_write_block_pio" not in fi
    assert "CMDTM_CMD(24" not in fi
    assert "CMDTM_CMD(13" not in fi
    assert "CMDTM_CMD(51" not in fi
    assert "CMDTM_CMD(18" not in fi
    assert "CMDTM_CMD(6" not in fi
    assert "(23u << 24)" not in fi
    assert "(25u << 24)" not in fi
    assert "TM_AUTO_CMD12" not in fi
    assert "SDHCI_C0_HCTL_DWIDTH" not in fi
    assert "fat32_set_entry" not in fi
    assert "fat32_file_bytes" not in fi
    assert "fat32_ok" not in fi
    assert "CONFIG  TXT" not in fi
    assert "CMDLINE TXT" not in fi
    assert "ISSUE   TXT" not in fi
    assert "OVERLAYS   " not in fi
    assert "AETHER  TMP" not in fi
    assert "int           kernel_sdfi_selftest(void);" in support
    assert 'uartPuts("runtime v113: FAT32 FSInfo\\n")' in app
    assert 'uartPuts("sdfi ok=")' in app
    assert "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 113" not in app
    assert "func printSdfi()" in shell
    assert 'shellBufferSliceEquals(commandStart, commandLen, "sdfi")' in shell
    assert ",sdbc,sdfi" in shell
    assert 'grep -qa "sdfi ok=1 version=113 lead="' in iterate
    assert 'probe_shell "sdfi" "^sdfi ok=1 version=113 lead="' in iterate
    assert "ping -c 2" in iterate
    assert "SOCK_STREAM" in iterate
    assert "sched12" in iterate
    assert "kernel_sdfi_selftest" not in scheduler
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
    assert "kernel_sdfi_selftest" in after

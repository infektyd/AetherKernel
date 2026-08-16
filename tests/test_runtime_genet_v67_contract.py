"""EPIC F V67: GENET register probe. No TX/RX/DMA. No IRQ MMIO."""

import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_genet_probe_is_boot_time_fail_closed() -> None:
    genet = read_repo("Sources/Support/kernel_genet.c")
    support = read_repo("Sources/Support/include/Support.h")
    app = read_repo("Sources/Application/Application.swift")
    shell = read_repo("Sources/Application/UARTShell.swift")
    iterate = read_repo("scripts/netboot/net-iterate.sh")
    scheduler = read_repo("Sources/Support/kernel_scheduler.c")

    assert "0xFD580000UL" in genet
    assert "SYS_REV_CTRL" in genet
    assert "UMAC_MDIO_CMD" in genet
    assert "int          kernel_genet_selftest(void);" in support
    assert 'uartPuts("runtime v67: GENET register probe\\n")' in app
    assert 'uartPuts("genet ok=")' in app
    assert "func printGenet()" in shell
    assert 'shellBufferSliceEquals(commandStart, commandLen, "genet")' in shell
    assert 'grep -qa "genet ok=1 version=67 rev=.* mdio=.* link="' in iterate
    assert 'grep -qa "runtime v67: GENET register probe"' in iterate
    assert 'probe_shell "genet" "^genet ok=1 version=67 rev=.* mdio=.* link="' in iterate
    assert 'grep -qa "kbd ok=[01] version=66 keycode=.* char="' in iterate
    assert "sched12" in iterate
    assert "kernel_genet_selftest" not in scheduler
    assert "G32(" not in scheduler

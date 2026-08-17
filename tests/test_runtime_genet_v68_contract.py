"""EPIC F V68: UMAC MAC + leftover RX_EN + MIB. No DMA. No CMD_RX_EN write."""

import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_genet2_is_read_only_mac_mib() -> None:
    genet = read_repo("Sources/Support/kernel_genet.c")
    support = read_repo("Sources/Support/include/Support.h")
    app = read_repo("Sources/Application/Application.swift")
    shell = read_repo("Sources/Application/UARTShell.swift")
    iterate = read_repo("scripts/netboot/net-iterate.sh")
    scheduler = read_repo("Sources/Support/kernel_scheduler.c")

    assert "UMAC_MAC0" in genet
    assert "UMAC_MAC1" in genet
    assert "UMAC_MIB_RX_POK" in genet
    assert "UMAC_MIB_RX_BYTES" in genet
    assert "CMD_RX_EN" in genet
    genet2 = genet.split("kernel_genet5_selftest")[0]
    assert "G32(UMAC_CMD) =" not in genet2
    assert "CMD_RX_EN)" in genet  # read leftover bit
    assert "int           kernel_genet2_selftest(void);" in support
    assert 'uartPuts("runtime v68: GENET UMAC MAC and RX MIB\\n")' in app
    assert 'uartPuts("genet2 ok=")' in app
    assert "func printGenet2()" in shell
    assert 'shellBufferSliceEquals(commandStart, commandLen, "genet2")' in shell
    assert ",xhci,genet,genet2" in shell
    assert 'grep -qa "genet2 ok=1 version=68 mac=.* rx=.* frames=.* bytes="' in iterate
    assert 'grep -qa "runtime v68: GENET UMAC MAC and RX MIB"' in iterate
    assert 'probe_shell "genet2" "^genet2 ok=1 version=68 mac=.* rx=.* frames=.* bytes="' in iterate
    assert 'grep -qa "genet ok=1 version=67 rev=.* mdio=.* link="' in iterate
    assert 'grep -qa "kbd ok=[01] version=66 keycode=.* char="' in iterate
    assert "sched12" in iterate
    assert "kernel_genet2_selftest" not in scheduler
    assert "G32(" not in scheduler
    # HDMI / S69 lesson: no boot-path 50ms CNTPCT spin after job_exec is live.
    assert "genet_udelay(50000)" not in genet
    assert "200000" not in genet
    # ok=1 is leftover RX + live MIB. Do not require a non-zero UMAC MAC.
    assert "mac == 0UL" not in genet

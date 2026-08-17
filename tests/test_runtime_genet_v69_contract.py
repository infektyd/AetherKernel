"""EPIC F V69: mailbox station MAC. No UMAC write. No DMA. No CMD_RX_EN write."""

import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_genet3_is_read_only_mailbox_mac() -> None:
    genet = read_repo("Sources/Support/kernel_genet.c")
    mbox = read_repo("Sources/Support/kernel_vc_mbox.c")
    support = read_repo("Sources/Support/include/Support.h")
    app = read_repo("Sources/Application/Application.swift")
    shell = read_repo("Sources/Application/UARTShell.swift")
    iterate = read_repo("scripts/netboot/net-iterate.sh")
    scheduler = read_repo("Sources/Support/kernel_scheduler.c")

    assert "0x00010003" in mbox
    assert "kernel_vc_mbox_board_mac" in mbox
    assert "int           kernel_vc_mbox_board_mac(unsigned long *out);" in support
    assert "int           kernel_genet3_selftest(void);" in support
    assert "kernel_vc_mbox_board_mac" in genet
    genet3 = genet.split("kernel_genet5_selftest")[0]
    assert "G32(UMAC_MAC0) =" not in genet3
    assert "G32(UMAC_MAC1) =" not in genet3
    assert "G32(UMAC_CMD) =" not in genet3
    assert 'uartPuts("runtime v69: GENET mailbox station MAC\\n")' in app
    assert 'uartPuts("genet3 ok=")' in app
    assert "func printGenet3()" in shell
    assert 'shellBufferSliceEquals(commandStart, commandLen, "genet3")' in shell
    assert ",xhci,genet,genet2,genet3" in shell
    assert 'grep -qa "genet3 ok=1 version=69 mac=.* mbox=.* umac="' in iterate
    assert 'grep -qa "runtime v69: GENET mailbox station MAC"' in iterate
    assert 'probe_shell "genet3" "^genet3 ok=1 version=69 mac=.* mbox=1 umac="' in iterate
    assert 'grep -qa "genet2 ok=1 version=68 mac=.* rx=.* frames=.* bytes="' in iterate
    assert "sched12" in iterate
    assert "kernel_genet3_selftest" not in scheduler
    assert "200000" not in genet
    # DMA / RX-enable stay parked.
    assert "alloc_dma" not in genet
    assert "CMD_RX_EN)" in genet  # leftover read in V68 only

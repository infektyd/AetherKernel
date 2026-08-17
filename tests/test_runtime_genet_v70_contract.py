"""EPIC F V70: mailbox board serial. No UMAC write. No DMA. No CMD_RX_EN write."""

import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_genet4_is_read_only_mailbox_serial() -> None:
    genet = read_repo("Sources/Support/kernel_genet.c")
    mbox = read_repo("Sources/Support/kernel_vc_mbox.c")
    support = read_repo("Sources/Support/include/Support.h")
    app = read_repo("Sources/Application/Application.swift")
    shell = read_repo("Sources/Application/UARTShell.swift")
    iterate = read_repo("scripts/netboot/net-iterate.sh")
    scheduler = read_repo("Sources/Support/kernel_scheduler.c")

    assert "0x00010004" in mbox
    assert "kernel_vc_mbox_board_serial" in mbox
    assert "int           kernel_vc_mbox_board_serial(unsigned long *out);" in support
    assert "int           kernel_genet4_selftest(void);" in support
    assert "kernel_vc_mbox_board_serial" in genet
    assert "G32(UMAC_MAC0) =" not in genet
    assert "G32(UMAC_MAC1) =" not in genet
    assert "G32(UMAC_CMD) =" not in genet
    assert 'uartPuts("runtime v70: GENET mailbox board serial\\n")' in app
    assert 'uartPuts("genet4 ok=")' in app
    assert "func printGenet4()" in shell
    assert 'shellBufferSliceEquals(commandStart, commandLen, "genet4")' in shell
    assert ",xhci,genet,genet2,genet3,genet4" in shell
    assert 'grep -qa "genet4 ok=1 version=70 serial=.* mbox=.* mac="' in iterate
    assert 'grep -qa "runtime v70: GENET mailbox board serial"' in iterate
    assert 'probe_shell "genet4" "^genet4 ok=1 version=70 serial=.* mbox=1 mac="' in iterate
    assert 'grep -qa "genet3 ok=1 version=69 mac=.* mbox=.* umac="' in iterate
    assert "sched12" in iterate
    assert "kernel_genet4_selftest" not in scheduler
    assert "200000" not in genet
    # DMA / RX-enable stay parked. MDIO PHYID is not this slice.
    assert "alloc_dma" not in genet
    assert "CMD_RX_EN)" in genet  # leftover read in V68 only
    assert "PHYID" not in genet

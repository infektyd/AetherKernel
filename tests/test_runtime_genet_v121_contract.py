"""EPIC F V121: originate mDNS query after GENET. No EL0. Not a clone."""

import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_genet17_originate_mdns_without_el0() -> None:
    genet = read_repo("Sources/Support/kernel_genet.c")
    support = read_repo("Sources/Support/include/Support.h")
    app = read_repo("Sources/Application/Application.swift")
    shell = read_repo("Sources/Application/UARTShell.swift")
    iterate = read_repo("scripts/netboot/net-iterate.sh")
    scheduler = read_repo("Sources/Support/kernel_scheduler.c")
    listener = read_repo("scripts/netboot/aether-mdns-v121.py")

    assert "kernel_genet17_selftest" in genet
    g17 = genet.split("kernel_genet17_selftest", 1)[1]
    assert "0x0a2a0001" in g17
    assert "0xA121" in g17 or "0xa121" in g17
    assert "5353" in g17 or "0x14E9" in g17
    assert "224.0.0.251" not in g17
    assert "0xe00000fb" in g17 or "0xE00000FB" in g17
    assert "aether-v121" in g17
    assert "genet10_park" in g17
    assert "genet10_unpark" in g17
    assert "kernel_enter_el0_and_wait" not in g17
    assert "kernel_event_emit" not in g17
    assert "watchdog_reset_now" not in g17
    assert "200000" not in genet
    assert "static unsigned long alloc_dma" not in genet
    assert "int           kernel_genet17_selftest(void);" in support
    assert 'uartPuts("runtime v121: GENET originate mDNS\\n")' in app
    assert 'uartPuts("genet17 ok=")' in app
    assert "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 121" not in app
    assert "func printGenet17()" in shell
    assert 'shellBufferSliceEquals(commandStart, commandLen, "genet17")' in shell
    assert ",genet16,genet17" in shell
    assert 'grep -qa "genet17 ok=1 version=121 mdns="' in iterate
    assert 'probe_shell "genet17" "^genet17 ok=1 version=121 mdns="' in iterate
    assert "aether-mdns-v121" in iterate
    assert "aether-v121" in listener
    assert "5353" in listener
    assert "CONFIG.TXT" not in g17
    assert "AETHER.TMP" not in g17
    assert "ping -c 2" in iterate
    assert "SOCK_STREAM" in iterate
    assert "sched12" in iterate
    assert "kernel_genet17_selftest" not in scheduler
    after = app.split("kernel_genet12_selftest", 1)[1]
    assert "kernel_enter_el0_and_wait" not in after
    assert "kernel_socket_selftest" not in after
    assert "kernel_genet17_selftest" in after

"""EPIC F V123: originate SNTP client after GENET. No EL0. Not a clone."""

import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_genet19_originate_sntp_without_el0() -> None:
    genet = read_repo("Sources/Support/kernel_genet.c")
    support = read_repo("Sources/Support/include/Support.h")
    app = read_repo("Sources/Application/Application.swift")
    shell = read_repo("Sources/Application/UARTShell.swift")
    iterate = read_repo("scripts/netboot/net-iterate.sh")
    scheduler = read_repo("Sources/Support/kernel_scheduler.c")
    listener = read_repo("scripts/netboot/aether-sntp-v123.py")

    assert "kernel_genet19_selftest" in genet
    g19 = genet.split("kernel_genet19_selftest", 1)[1]
    assert "0x0a2a0001" in g19
    assert "0xA123" in g19 or "0xa123" in g19
    assert "0x23" in g19
    assert "0xA1230001" in g19 or "0xa1230001" in g19
    assert "genet10_park" in g19
    assert "genet10_unpark" in g19
    assert "kernel_enter_el0_and_wait" not in g19
    assert "kernel_event_emit" not in g19
    assert "watchdog_reset_now" not in g19
    assert "200000" not in genet
    assert "static unsigned long alloc_dma" not in genet
    assert "int           kernel_genet19_selftest(void);" in support
    assert 'uartPuts("runtime v123: GENET originate SNTP\\n")' in app
    assert 'uartPuts("genet19 ok=")' in app
    assert "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 123" not in app
    assert "func printGenet19()" in shell
    assert 'shellBufferSliceEquals(commandStart, commandLen, "genet19")' in shell
    assert ",genet18,genet19" in shell
    assert 'grep -qa "genet19 ok=1 version=123 sntp="' in iterate
    assert 'probe_shell "genet19" "^genet19 ok=1 version=123 sntp="' in iterate
    assert "aether-sntp-v123" in iterate
    assert "41251" in listener or "0xA123" in listener
    assert "CONFIG.TXT" not in g19
    assert "AETHER.TMP" not in g19
    assert "ping -c 2" in iterate
    assert "SOCK_STREAM" in iterate
    assert "sched12" in iterate
    assert "kernel_genet19_selftest" not in scheduler
    after = app.split("kernel_genet12_selftest", 1)[1]
    assert "kernel_enter_el0_and_wait" not in after
    assert "kernel_socket_selftest" not in after
    assert "kernel_genet19_selftest" in after

"""EPIC F V126: TCP helper liveness. Not a protocol clone. No EL0."""

import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def _selftest_body(genet: str, name: str) -> str:
    key = f"int {name}(void) {{"
    chunk = genet.split(key, 1)[1]
    nxt = chunk.find("\nint kernel_genet")
    return chunk if nxt < 0 else chunk[:nxt]


def test_genet22_tcp_helper_liveness_without_el0() -> None:
    genet = read_repo("Sources/Support/kernel_genet.c")
    support = read_repo("Sources/Support/include/Support.h")
    app = read_repo("Sources/Application/Application.swift")
    shell = read_repo("Sources/Application/UARTShell.swift")
    iterate = read_repo("scripts/netboot/net-iterate.sh")
    scheduler = read_repo("Sources/Support/kernel_scheduler.c")
    listener = read_repo("scripts/netboot/aether-tcp-echo-v119.py")
    ready = read_repo("scripts/netboot/aether-tcp-ready-v126.py")

    assert "kernel_genet22_selftest" in genet
    g22 = genet.split("kernel_genet22_selftest", 1)[1]
    body22 = _selftest_body(genet, "kernel_genet22_selftest")
    assert "0x0a2a0001" in g22
    assert "0xA126" in g22 or "0xa126" in g22
    assert "0xA119" in g22 or "0xa119" in g22
    assert "0x12" in g22
    assert "0x18" in g22
    assert "genet10_park" in g22
    assert "genet10_unpark" in g22
    assert "kernel_genet13_selftest" in body22
    assert "kernel_genet15_selftest" not in g22
    assert "kernel_genet20_selftest" not in g22
    assert "kernel_genet21_selftest" not in g22
    assert "kernel_enter_el0_and_wait" not in g22
    assert "kernel_event_emit" not in g22
    assert "watchdog_reset_now" not in g22
    assert "200000" not in genet
    assert "static unsigned long alloc_dma" not in genet
    assert "int           kernel_genet22_selftest(void);" in support
    assert 'uartPuts("runtime v126: GENET TCP helper liveness\\n")' in app
    assert 'uartPuts("genet22 ok=")' in app
    assert "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 126" not in app
    assert "func printGenet22()" in shell
    assert 'shellBufferSliceEquals(commandStart, commandLen, "genet22")' in shell
    assert ",genet21,genet22" in shell
    assert 'grep -qa "genet22 ok=1 version=126 live="' in iterate
    assert 'probe_shell "genet22" "^genet22 ok=1 version=126 live="' in iterate
    assert "aether-tcp-ready-v126" in iterate
    assert "ensure_tcp_echo_v119" in iterate
    assert "41241" in ready
    assert "A126LIVE" in ready
    assert "settimeout" in listener
    assert "listen(16)" in listener
    assert "SOCK_STREAM" in listener
    assert "ping -c 2" in iterate
    assert "SOCK_STREAM" in iterate
    assert "sched12" in iterate
    assert "kernel_genet22_selftest" not in scheduler
    after = app.split("kernel_genet12_selftest", 1)[1]
    assert "kernel_enter_el0_and_wait" not in after
    assert "kernel_socket_selftest" not in after
    assert "kernel_genet22_selftest" in after

    g16 = _selftest_body(genet, "kernel_genet16_selftest")
    g17 = _selftest_body(genet, "kernel_genet17_selftest")
    g18 = _selftest_body(genet, "kernel_genet18_selftest")
    g19 = _selftest_body(genet, "kernel_genet19_selftest")
    g20 = _selftest_body(genet, "kernel_genet20_selftest")
    assert "kernel_genet13_selftest" in g16
    assert "kernel_genet15_selftest" not in g16
    assert "kernel_genet13_selftest" in g17
    assert "kernel_genet16_selftest" not in g17
    assert "kernel_genet13_selftest" in g18
    assert "kernel_genet17_selftest" not in g18
    assert "kernel_genet13_selftest" in g19
    assert "kernel_genet18_selftest" not in g19
    assert "kernel_genet13_selftest" in g20
    assert "kernel_genet19_selftest" not in g20
    g21 = _selftest_body(genet, "kernel_genet21_selftest")
    assert "kernel_genet20_selftest" in g21

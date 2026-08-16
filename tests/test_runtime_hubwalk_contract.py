"""EPIC E: hub downstream walk tokens. Do not require a keypress."""

import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_hubwalk_accessors_and_boot_line() -> None:
    support = read_repo("Sources/Support/include/Support.h")
    xhci = read_repo("Sources/Support/kernel_xhci.c")
    app = read_repo("Sources/Application/Application.swift")
    iterate = read_repo("scripts/netboot/net-iterate.sh")
    shell = read_repo("Sources/Application/UARTShell.swift")

    assert "int          kernel_hubwalk_ok(void);" in support
    assert "unsigned int kernel_hubwalk_ports(void);" in support
    assert "unsigned int kernel_hubwalk_connected(void);" in support
    assert "unsigned int kernel_hubwalk_hid(void);" in support
    assert "hubwalk_ok_val = 1;" in xhci
    assert "hubwalk_connected_val = connected;" in xhci
    assert 'uartPuts("hubwalk ok=")' in app
    assert "kernel_hubwalk_ports()" in app
    assert "kernel_hubwalk_connected()" in app
    assert "kernel_hubwalk_hid()" in app
    assert 'probe_shell "hubwalk"' not in iterate
    assert 'shellBufferSliceEquals(commandStart, commandLen, "hubwalk")' not in shell
    assert 'grep -qa "kbd ok=1 version=66"' not in iterate
    assert 'grep -qa "kbd ok=[01] version=66 keycode=.* char="' in iterate
    assert 'grep -qa "hubwalk ok=[01] version=66 ports=.* connected=.* hid="' in iterate
    assert 'grep -qa "hubwalk ok=1 version=66' not in iterate

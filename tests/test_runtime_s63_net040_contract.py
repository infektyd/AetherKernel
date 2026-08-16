"""S63 NET-040: xhci_run boot grep requires always-present ports_connected= atom."""

import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]

# Always-present boot-line atoms from Application.swift xhci_run uartPuts (~612-615).
XHCI_RUN_BOOT_GREP = 'grep -qa "xhci_run ok=1 version=64 ports_connected=.*"'
XHCI_RUN_BOOT_GREP_DOCTOR = 'grep -q "xhci_run ok=1 version=64 ports_connected=.*"'

# NET-040 rejects ok/version-only weak greps (ports_connected= logged-then-orphaned).
XHCI_RUN_WEAK_GREP = 'grep -qa "xhci_run ok=1 version=64"'

XHCI_RUN_UART_ATOMS = (
    'uartPuts("xhci_run ok=")',
    'uartPuts(" version=64 ports_connected=")',
)


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_s63_net040_net_iterate_xhci_run_schema_grep() -> None:
    net_iterate = read_repo("scripts/netboot/net-iterate.sh")

    assert XHCI_RUN_BOOT_GREP in net_iterate


def test_s63_net040_netboot_doctor_xhci_run_schema_grep() -> None:
    doctor = read_repo("scripts/netboot/netboot-doctor.sh")

    assert XHCI_RUN_BOOT_GREP_DOCTOR in doctor


def test_s63_net040_rejects_ok_only_xhci_run_grep() -> None:
    """Metal greps must not stop at ok=1 version=64 (ports_connected= is always emitted)."""
    net_iterate = read_repo("scripts/netboot/net-iterate.sh")

    assert XHCI_RUN_WEAK_GREP not in net_iterate
    for line in read_repo("scripts/netboot/netboot-doctor.sh").splitlines():
        if "xhci_run ok=" in line:
            assert "ports_connected=" in line
            assert not line.rstrip("\\").endswith('version=64"')


def test_s63_net040_application_xhci_run_uart_atoms() -> None:
    app = read_repo("Sources/Application/Application.swift")

    for marker in XHCI_RUN_UART_ATOMS:
        assert marker in app, f"missing boot uart atom: {marker}"

    assert "let xhci_run_ok_boot = kernel_xhci_run_selftest()" in app
    assert "kernel_xhci_ports_connected()" in app

"""S44 NET-027/028: boot greps extend metal floor through runtime v64–v66."""

import pathlib

from tests.netiterate_boot_gate import extract_net_iterate_success_boot_gate


ROOT = pathlib.Path(__file__).resolve().parents[1]

# Boot serial greps added after v63 xhci ok= (NET-027). usb_enum/kbd ok=[01]:
# unattended netboot may have no downstream USB device or keyboard (NET-029).
BOOT_GREPS_V64_V66 = (
    'grep -qa "runtime v64: xHCI controller init"',
    'grep -qa "xhci_run ok=1 version=64 ports_connected=.*"',
    'grep -qa "runtime v65: USB device enumeration"',
    'grep -qa "usb_enum ok=[01] version=65 addr=.* vendor=.* product=.* class=.* stage=.* portsc=.* portscR=.* slot_raw=.* ad_raw="',
    'grep -qa "runtime v66: HID boot-protocol keyboard"',
    'grep -qa "kbd ok=[01] version=66 keycode=.* char="',
)

# Prior ceiling through v63 — must not regress when extending to v66.
BOOT_GREPS_V46_V63_SAMPLE = (
    'grep -qa "runtime v46: kernel/user address-space split (isolated page tables)"',
    'grep -qa "runtime v63: xHCI capability register probe"',
    'grep -qa "xhci ok=1 version=63"',
)

# NET-028: boot-only writers; UARTShell dispatch ends at xhci (no print* handlers).
SKIPPED_PROBE_SHELL = ("xhci_run", "usb_enum", "kbd")


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_s44_net027_boot_greps_v64_v66_present() -> None:
    net_iterate = read_repo("scripts/netboot/net-iterate.sh")

    for marker in BOOT_GREPS_V64_V66:
        assert marker in net_iterate, f"missing boot grep: {marker}"


def test_s44_net027_boot_grep_ceiling_after_v63_xhci() -> None:
    net_iterate = read_repo("scripts/netboot/net-iterate.sh")
    xhci_idx = net_iterate.index('grep -qa "xhci ok=1 version=63"')
    v64_idx = net_iterate.index('grep -qa "runtime v64: xHCI controller init"')
    assert xhci_idx < v64_idx, "v64–v66 greps must follow v63 xhci ok= grep"


def test_s44_net027_v46_v63_grep_ceiling_unchanged() -> None:
    net_iterate = read_repo("scripts/netboot/net-iterate.sh")
    boot_gate = extract_net_iterate_success_boot_gate(net_iterate)

    for marker in BOOT_GREPS_V46_V63_SAMPLE:
        assert marker in boot_gate, f"v46–v63 grep regressed: {marker}"


def test_s44_net028_no_probe_shell_for_boot_only_usb_markers() -> None:
    """No UARTShell print* for xhci_run/usb_enum/kbd — boot greps only."""
    net_iterate = read_repo("scripts/netboot/net-iterate.sh")
    shell = read_repo("Sources/Application/UARTShell.swift")

    for cmd in SKIPPED_PROBE_SHELL:
        assert f'probe_shell "{cmd}"' not in net_iterate, (
            f"unexpected probe_shell for boot-only {cmd}"
        )
        assert f'"{cmd}"' not in shell.split("shell ready commands=")[1].split("\n")[0]
        assert f'shellBufferSliceEquals(commandStart, commandLen, "{cmd}")' not in shell

    assert 'shellBufferSliceEquals(commandStart, commandLen, "xhci")' in shell


def test_s44_net027_application_boot_markers() -> None:
    app = read_repo("Sources/Application/Application.swift")

    for marker in (
        "runtime v64: xHCI controller init",
        "let xhci_run_ok_boot = kernel_xhci_run_selftest()",
        "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 64,",
        "xhci_run ok=",
        "runtime v65: USB device enumeration",
        "let usb_enum_ok_boot = kernel_usb_enum_selftest()",
        "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 65,",
        "usb_enum ok=",
        "runtime v66: HID boot-protocol keyboard",
        "let kbd_ok_boot = kernel_kbd_selftest()",
        "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 66,",
        "kbd ok=",
    ):
        assert marker in app


def test_s44_net029_kbd_not_required_ok1_in_boot_greps() -> None:
    net_iterate = read_repo("scripts/netboot/net-iterate.sh")
    assert 'grep -qa "kbd ok=[01] version=66 keycode=.* char="' in net_iterate
    assert 'grep -qa "kbd ok=1 version=66"' not in net_iterate


# DOC-s44-docs: operator docs must name the v64–v66 boot ceiling net-iterate greps.
DOC_MARKERS_V64_V66 = (
    "runtime v64: xHCI controller init",
    "xhci_run ok=1 version=64 ports_connected=.*",
    "runtime v65: USB device enumeration",
    (
        "usb_enum ok=[01] version=65 addr=.* vendor=.* product=.* class=.* "
        "stage=.* portsc=.* portscR=.* slot_raw=.* ad_raw="
    ),
    "runtime v66: HID boot-protocol keyboard",
    "kbd ok=[01] version=66 keycode=.* char=",
)


def test_s44_doc_s44_docs_readme_runbook_roadmap() -> None:
    readme = read_repo("README.md")
    runbook = read_repo("docs/RUNBOOK.md")
    roadmap = read_repo("docs/ROADMAP.md")

    for marker in DOC_MARKERS_V64_V66:
        assert marker in readme, f"README missing {marker!r}"
        assert marker in runbook, f"RUNBOOK missing {marker!r}"

    assert "Where we are now (V66)" in roadmap
    assert "xhci_run ok=1 version=64 ports_connected=.*" in roadmap
    assert (
        "usb_enum ok=[01] version=65 addr=.* vendor=.* product=.* class=.* "
        "stage=.* portsc=.* portscR=.* slot_raw=.* ad_raw="
    ) in roadmap
    assert "kbd ok=[01] version=66 keycode=.* char=" in roadmap


def test_s44_doc_s44_docs_concurrency_boot_ceiling() -> None:
    design = read_repo("docs/CONCURRENCY_DESIGN.md")

    for marker in DOC_MARKERS_V64_V66:
        assert marker in design, f"CONCURRENCY_DESIGN missing {marker!r}"

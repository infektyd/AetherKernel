"""S60 NET-036: usb_enum/kbd boot greps require always-present telemetry schema atoms."""

import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]

# Always-present boot-line atoms from Application.swift usb_enum/kbd uartPuts (~618-649).
USB_ENUM_BOOT_GREP = (
    'grep -qa "usb_enum ok=[01] version=65 addr=.* vendor=.* product=.* class=.* '
    'stage=.* portsc=.* portscR=.* slot_raw=.* ad_raw="'
)
KBD_BOOT_GREP = 'grep -qa "kbd ok=[01] version=66 keycode=.* char="'

USB_ENUM_BOOT_GREP_DOCTOR = (
    'grep -q "usb_enum ok=[01] version=65 addr=.* vendor=.* product=.* class=.* '
    'stage=.* portsc=.* portscR=.* slot_raw=.* ad_raw="'
)
KBD_BOOT_GREP_DOCTOR = 'grep -q "kbd ok=[01] version=66 keycode=.* char="'

# NET-029 floor: ok=[01] not ok=1; NET-036 rejects ok-only weak greps.
USB_ENUM_WEAK_GREP = 'grep -qa "usb_enum ok=[01] version=65"'
KBD_WEAK_GREP = 'grep -qa "kbd ok=[01] version=66"'

USB_ENUM_UART_ATOMS = (
    'uartPuts("usb_enum ok=")',
    'uartPuts(" version=65 addr=")',
    'uartPuts(" vendor=")',
    'uartPuts(" product=")',
    'uartPuts(" class=")',
    'uartPuts(" stage=")',
    'uartPuts(" portsc=")',
    'uartPuts(" portscR=")',
    'uartPuts(" slot_raw=")',
    'uartPuts(" ad_raw=")',
)

KBD_UART_ATOMS = (
    'uartPuts("kbd ok=")',
    'uartPuts(" version=66 keycode=")',
    'uartPuts(" char=")',
)


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_s60_net036_net_iterate_usb_enum_kbd_schema_greps() -> None:
    net_iterate = read_repo("scripts/netboot/net-iterate.sh")

    assert USB_ENUM_BOOT_GREP in net_iterate
    assert KBD_BOOT_GREP in net_iterate


def test_s60_net036_netboot_doctor_usb_enum_kbd_schema_greps() -> None:
    doctor = read_repo("scripts/netboot/netboot-doctor.sh")

    assert USB_ENUM_BOOT_GREP_DOCTOR in doctor
    assert KBD_BOOT_GREP_DOCTOR in doctor


def test_s60_net036_rejects_ok_only_usb_enum_kbd_greps() -> None:
    """Metal greps must not stop at ok=[01] version= (logged-then-orphaned fields)."""
    net_iterate = read_repo("scripts/netboot/net-iterate.sh")

    assert USB_ENUM_WEAK_GREP not in net_iterate
    assert KBD_WEAK_GREP not in net_iterate


def test_s60_net036_application_usb_enum_kbd_uart_atoms() -> None:
    app = read_repo("Sources/Application/Application.swift")

    for marker in USB_ENUM_UART_ATOMS + KBD_UART_ATOMS:
        assert marker in app, f"missing boot uart atom: {marker}"

    assert "let usb_enum_ok_boot = kernel_usb_enum_selftest()" in app
    assert "let kbd_ok_boot = kernel_kbd_selftest()" in app


def test_s60_net036_kbd_still_allows_ok0_not_required_ok1() -> None:
    """NET-029: unattended netboot may report kbd ok=0; gate must not require ok=1."""
    net_iterate = read_repo("scripts/netboot/net-iterate.sh")

    assert 'grep -qa "kbd ok=1 version=66"' not in net_iterate
    assert "keycode=.* char=" in net_iterate

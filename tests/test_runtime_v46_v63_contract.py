"""DOC-010: host contract tests lock net-iterate boot greps v46–v63 to Application.swift emitters."""

from __future__ import annotations

import pathlib
from dataclasses import dataclass
from typing import Sequence

import pytest

from tests.netiterate_boot_gate import extract_net_iterate_success_boot_gate


ROOT = pathlib.Path(__file__).resolve().parents[1]


@dataclass(frozen=True)
class VersionBootContract:
    version: int
    runtime_banner_app: str
    runtime_banner_grep: str
    ok_app_markers: Sequence[str]
    ok_grep: str


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


# Locked to the success boot gate slice above and matching Application.swift uartPuts
# boot emitters. Doctor uses grep -q with the same quoted patterns (grep -qa → grep -q).
VERSION_BOOT_CONTRACTS: tuple[VersionBootContract, ...] = (
    VersionBootContract(
        46,
        'uartPuts("runtime v46: kernel/user address-space split (isolated page tables)\\n")',
        'grep -qa "runtime v46: kernel/user address-space split (isolated page tables)"',
        ('uartPuts("asplit ok=")', 'uartPuts(" version=46\\n")'),
        'grep -qa "asplit ok=1 version=46"',
    ),
    VersionBootContract(
        47,
        'uartPuts("runtime v47: EL0 entry/exit and context save/restore\\n")',
        'grep -qa "runtime v47: EL0 entry/exit and context save/restore"',
        ('uartPuts("el0 ok=")', 'uartPuts(" version=47\\n")'),
        'grep -qa "el0 ok=1 version=47"',
    ),
    VersionBootContract(
        48,
        'uartPuts("runtime v48: syscall ABI via SVC from EL0\\n")',
        'grep -qa "runtime v48: syscall ABI via SVC from EL0"',
        (
            'uartPuts("syscall ok=")',
            'uartPuts(" version=48 abi=")',
            'uartPuts(" dispatched=")',
            'uartPuts(" num=")',
            'uartPuts(" ret=")',
            "kernel_syscall_last_dispatched_read()",
            "kernel_syscall_last_num_read()",
            "kernel_syscall_last_ret_read()",
        ),
        (
            'grep -qa "syscall ok=1 version=48 abi=48 table=1 '
            'dispatched=1 num=1 ret=0x0000000000482026"'
        ),
    ),
    VersionBootContract(
        49,
        'uartPuts("runtime v49: fault-safe copy_from_user / copy_to_user\\n")',
        'grep -qa "runtime v49: fault-safe copy_from_user / copy_to_user"',
        ('uartPuts("uaccess ok=")', 'uartPuts(" version=49\\n")'),
        'grep -qa "uaccess ok=1 version=49"',
    ),
    VersionBootContract(
        50,
        'uartPuts("runtime v50: EPIC A capstone — EL0 syscall + user fault containment\\n")',
        'grep -qa "runtime v50: EPIC A capstone"',
        (
            'uartPuts("usermode ok=")',
            'uartPuts(" version=50 fault_contained=")',
        ),
        'grep -qa "usermode ok=1 version=50 fault_contained=1"',
    ),
    VersionBootContract(
        51,
        'uartPuts("runtime v51: process abstraction (address space + lifecycle)\\n")',
        'grep -qa "runtime v51: process abstraction"',
        ('uartPuts("process ok=")', 'uartPuts(" version=51\\n")'),
        'grep -qa "process ok=1 version=51"',
    ),
    VersionBootContract(
        52,
        'uartPuts("runtime v52: user binary loader (flat blob + sys_write)\\n")',
        'grep -qa "runtime v52: user binary loader"',
        ('uartPuts("processes ok=")', 'uartPuts(" version=52\\n")'),
        'grep -qa "processes ok=1 version=52"',
    ),
    VersionBootContract(
        53,
        'uartPuts("runtime v53: multi-process user execution (per-core EL0 + 3x isolation)\\n")',
        'grep -qa "runtime v53: multi-process user execution"',
        ('uartPuts("multiprocess ok=")', 'uartPuts(" version=53\\n")'),
        'grep -qa "multiprocess ok=1 version=53"',
    ),
    VersionBootContract(
        54,
        'uartPuts("runtime v54: BCM2711 EMMC2/SDHCI register probe\\n")',
        'grep -qa "runtime v54: BCM2711 EMMC2/SDHCI register probe"',
        ('uartPuts("sdhci ok=")', 'uartPuts(" version=54")'),
        'grep -qa "sdhci ok=1 version=54"',
    ),
    VersionBootContract(
        55,
        'uartPuts("runtime v55: SD card identification (CMD0/CMD8/ACMD41/CMD2/CMD3)\\n")',
        'grep -qa "runtime v55: SD card identification (CMD0/CMD8/ACMD41/CMD2/CMD3)"',
        ('uartPuts("card ok=")', 'uartPuts(" version=55 rca=")'),
        'grep -qa "card ok=1 version=55"',
    ),
    VersionBootContract(
        56,
        'uartPuts("runtime v56: single block read CMD17 + MBR 0x55AA verification\\n")',
        'grep -qa "runtime v56: single block read CMD17 + MBR 0x55AA verification"',
        ('uartPuts("block ok=")', 'uartPuts(" version=56 mbr=")'),
        'grep -qa "block ok=1 version=56"',
    ),
    VersionBootContract(
        57,
        'uartPuts("runtime v57: FAT32 file read (config.txt bytes + checksum)\\n")',
        'grep -qa "runtime v57: FAT32 file read (config.txt bytes + checksum)"',
        (
            'uartPuts("fat32 ok=")',
            'uartPuts(" version=57 file=config.txt bytes=")',
        ),
        'grep -qa "fat32 ok=1 version=57"',
    ),
    VersionBootContract(
        58,
        'uartPuts("runtime v58: VideoCore mailbox property interface (firmware revision)\\n")',
        'grep -qa "runtime v58: VideoCore mailbox property interface (firmware revision)"',
        ('uartPuts("mailbox ok=")', 'uartPuts(" version=58 fw_rev=")'),
        'grep -qa "mailbox ok=1 version=58"',
    ),
    VersionBootContract(
        59,
        'uartPuts("runtime v59: VideoCore framebuffer allocation (width/height/pitch/addr)\\n")',
        'grep -qa "runtime v59: VideoCore framebuffer allocation (width/height/pitch/addr)"',
        ('uartPuts("framebuf ok=")', 'uartPuts(" version=59 width=")'),
        'grep -qa "framebuf ok=1 version=59"',
    ),
    VersionBootContract(
        60,
        'uartPuts("runtime v60: text console (8x8 font blit + readback proof)\\n")',
        'grep -qa "runtime v60: text console (8x8 font blit + readback proof)"',
        ('uartPuts("console ok=")', 'uartPuts(" version=60 rows=")'),
        'grep -qa "console ok=1 version=60"',
    ),
    VersionBootContract(
        61,
        'uartPuts("runtime v61: BCM2711 PCIe RC bring-up\\n")',
        'grep -qa "runtime v61: BCM2711 PCIe RC bring-up"',
        ('uartPuts("pcie ok=")', 'uartPuts(" version=61 link=")'),
        'grep -qa "pcie ok=1 version=61"',
    ),
    VersionBootContract(
        62,
        'uartPuts("runtime v62: VL805 USB 3.0 xHCI config-space probe\\n")',
        'grep -qa "runtime v62: VL805 USB 3.0 xHCI config-space probe"',
        ('uartPuts("vl805 ok=")', 'uartPuts(" version=62 vendor=")'),
        'grep -qa "vl805 ok=1 version=62"',
    ),
    VersionBootContract(
        63,
        'uartPuts("runtime v63: xHCI capability register probe\\n")',
        'grep -qa "runtime v63: xHCI capability register probe"',
        ('uartPuts("xhci ok=")', 'uartPuts(" version=63 hciversion=")'),
        'grep -qa "xhci ok=1 version=63"',
    ),
)


def _doctor_grep(net_iterate_grep: str) -> str:
    return net_iterate_grep.replace("grep -qa ", "grep -q ", 1)


@pytest.mark.parametrize(
    "contract",
    VERSION_BOOT_CONTRACTS,
    ids=lambda c: f"v{c.version}",
)
def test_runtime_v46_v63_application_boot_emitters(contract: VersionBootContract) -> None:
    app = read_repo("Sources/Application/Application.swift")

    assert contract.runtime_banner_app in app, (
        f"v{contract.version} missing Application.swift runtime banner"
    )
    for marker in contract.ok_app_markers:
        assert marker in app, f"v{contract.version} missing boot uart atom: {marker!r}"


@pytest.mark.parametrize(
    "contract",
    VERSION_BOOT_CONTRACTS,
    ids=lambda c: f"v{c.version}",
)
def test_runtime_v46_v63_net_iterate_boot_greps(contract: VersionBootContract) -> None:
    net_iterate = read_repo("scripts/netboot/net-iterate.sh")
    boot_gate = extract_net_iterate_success_boot_gate(net_iterate)

    assert contract.runtime_banner_grep in boot_gate, (
        f"v{contract.version} missing net-iterate runtime banner grep in success boot gate"
    )
    assert contract.ok_grep in boot_gate, (
        f"v{contract.version} missing net-iterate ok= boot grep in success boot gate"
    )


@pytest.mark.parametrize(
    "contract",
    VERSION_BOOT_CONTRACTS,
    ids=lambda c: f"v{c.version}",
)
def test_runtime_v46_v63_doctor_boot_greps(contract: VersionBootContract) -> None:
    doctor = read_repo("scripts/netboot/netboot-doctor.sh")

    runtime_doctor = _doctor_grep(contract.runtime_banner_grep)
    ok_doctor = _doctor_grep(contract.ok_grep)

    assert runtime_doctor in doctor, (
        f"v{contract.version} missing doctor runtime banner grep"
    )
    assert ok_doctor in doctor, f"v{contract.version} missing doctor ok= boot grep"


def test_runtime_v46_v63_net_iterate_banner_block_precedes_ok_greps() -> None:
    """Runtime v46–v63 banner greps must precede their ok= greps in the boot gate."""
    net_iterate = read_repo("scripts/netboot/net-iterate.sh")
    boot_gate = extract_net_iterate_success_boot_gate(net_iterate)

    for contract in VERSION_BOOT_CONTRACTS:
        banner_idx = boot_gate.index(contract.runtime_banner_grep)
        ok_idx = boot_gate.index(contract.ok_grep)
        assert banner_idx < ok_idx, (
            f"v{contract.version}: runtime banner grep must precede ok= grep in boot gate"
        )


def test_runtime_v46_v63_banner_greps_require_success_boot_gate_not_whole_file() -> None:
    """Each v46–v63 banner grep appears ~7× in net-iterate (stale-SD classifiers).

    Locks must target the TFTP-verified success boot gate only; a whole-file substring
    check would still pass if only the primary boot-gate line (~249) were deleted.
    """
    net_iterate = read_repo("scripts/netboot/net-iterate.sh")
    boot_gate = extract_net_iterate_success_boot_gate(net_iterate)

    for contract in VERSION_BOOT_CONTRACTS:
        assert boot_gate.count(contract.runtime_banner_grep) == 1, (
            f"v{contract.version}: expected exactly one banner grep in success boot gate"
        )
        assert net_iterate.count(contract.runtime_banner_grep) > 1, (
            f"v{contract.version}: stale-SD duplicates must not satisfy the boot-gate lock"
        )


def test_runtime_v46_v63_deleting_primary_boot_gate_banner_fails_lock() -> None:
    """Regression: removing only the boot-gate banner line must fail the contract."""
    net_iterate = read_repo("scripts/netboot/net-iterate.sh")
    boot_gate = extract_net_iterate_success_boot_gate(net_iterate)
    contract = VERSION_BOOT_CONTRACTS[0]  # v46 — primary boot-gate line ~249

    boot_gate_without_primary = boot_gate.replace(
        contract.runtime_banner_grep + " \\",
        "",
        1,
    )

    assert contract.runtime_banner_grep in net_iterate
    assert contract.runtime_banner_grep not in boot_gate_without_primary
    with pytest.raises(AssertionError):
        assert contract.runtime_banner_grep in boot_gate_without_primary

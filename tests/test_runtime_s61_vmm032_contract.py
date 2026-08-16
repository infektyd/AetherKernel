"""S61 VMM-032: boot syscall dispatched=/num=/ret= atoms locked by metal boot grep."""

import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]

# Application.swift boot uartPuts after kernel_syscall_selftest() success (ok=1):
# abi=48 table=1 dispatched=1 num=1 (SYS_PING) ret=0x482026 (full 64-bit hex).
SYSCALL_BOOT_GREP = (
    'grep -qa "syscall ok=1 version=48 abi=48 table=1 '
    'dispatched=1 num=1 ret=0x0000000000482026"'
)

SYSCALL_DOCTOR_GREP = (
    'grep -q "syscall ok=1 version=48 abi=48 table=1 '
    'dispatched=1 num=1 ret=0x0000000000482026"'
)

# DOC-syscall-short: operator docs must name the S61 boot grep schema atoms.
SYSCALL_BOOT_MARKER = (
    "syscall ok=1 version=48 abi=48 table=1 "
    "dispatched=1 num=1 ret=0x0000000000482026"
)
SYSCALL_SHORT_README_MARKER = "`syscall ok=1 version=48`"
DOC_PATHS = (
    "README.md",
    "docs/RUNBOOK.md",
    "docs/ROADMAP.md",
    "docs/CONCURRENCY_DESIGN.md",
)


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_s61_vmm032_boot_grep_requires_dispatch_atoms() -> None:
    net_iterate = read_repo("scripts/netboot/net-iterate.sh")
    doctor = read_repo("scripts/netboot/netboot-doctor.sh")

    assert SYSCALL_BOOT_GREP in net_iterate
    assert SYSCALL_DOCTOR_GREP in doctor
    assert 'grep -qa "syscall ok=1 version=48"' not in net_iterate
    assert 'grep -q "syscall ok=1 version=48"' not in doctor


def test_s61_vmm032_boot_emitter_schema() -> None:
    app = read_repo("Sources/Application/Application.swift")

    boot_block = app.split("uartPuts(\"syscall ok=\")", 1)[1].split(
        "uartPuts(\"uaccess ok=\")", 1
    )[0]
    for atom in (
        'uartPuts(" version=48 abi=")',
        'uartPuts(" table=")',
        'uartPuts(" dispatched=")',
        'uartPuts(" num=")',
        'uartPuts(" ret=")',
        "kernel_syscall_last_dispatched_read()",
        "kernel_syscall_last_num_read()",
        "kernel_syscall_last_ret_read()",
        "uartPutHex(UInt64(kernel_syscall_last_ret_read()))",
    ):
        assert atom in boot_block, f"missing boot emitter atom: {atom}"


def test_s61_vmm032_print_syscall_stops_at_table() -> None:
    """Shell reprint omits boot-only dispatch telemetry; probe_shell stays soft."""
    shell = read_repo("Sources/Application/UARTShell.swift")
    net_iterate = read_repo("scripts/netboot/net-iterate.sh")

    fn = shell.split("func printSyscall()", 1)[1].split("func printUaccess()", 1)[0]
    assert 'uartPuts(" table=")' in fn
    assert "dispatched=" not in fn
    assert " num=" not in fn
    assert " ret=" not in fn
    assert 'probe_shell "syscall" "^syscall ok=1 version=48 .*"' in net_iterate


def test_s64_doc_syscall_readme_full_schema() -> None:
    """README V48 row must match net-iterate boot grep (DOC-syscall-short)."""
    readme = read_repo("README.md")
    assert SYSCALL_BOOT_MARKER in readme
    assert SYSCALL_SHORT_README_MARKER not in readme


def test_s64_doc_syscall_short_form_absent_from_operator_docs() -> None:
    """No operator doc may snapshot the pre-S61 short syscall boot marker."""
    for path in DOC_PATHS:
        text = read_repo(path)
        assert SYSCALL_SHORT_README_MARKER not in text, f"{path} still has short syscall marker"
        if "syscall ok=1 version=48" in text:
            assert SYSCALL_BOOT_MARKER in text, f"{path} mentions syscall v48 without full schema"

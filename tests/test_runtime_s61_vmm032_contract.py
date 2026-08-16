"""S61 VMM-032: boot syscall dispatched=/num=/ret= atoms locked by metal boot grep."""

import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]

# Application.swift boot uartPuts after kernel_syscall_selftest() success (ok=1):
# abi=48 table=1 dispatched=1 num=1 (SYS_PING) ret=0x482026 (full 64-bit hex).
SYSCALL_BOOT_GREP = (
    'grep -qa "syscall ok=1 version=48 abi=48 table=1 '
    'dispatched=1 num=1 ret=0x00000000000482026"'
)

SYSCALL_DOCTOR_GREP = (
    'grep -q "syscall ok=1 version=48 abi=48 table=1 '
    'dispatched=1 num=1 ret=0x00000000000482026"'
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

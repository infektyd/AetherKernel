"""S69 VER-sched12: package version 46 is not the sched12 feature token."""

import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_s69_support_package_version_is_46() -> None:
    support = read_repo("Sources/Support/include/Support.h")
    assert "#define KERNEL_SCHEDULER_VERSION 46U" in support


def test_s69_sched12_emits_feature_token_44_not_46() -> None:
    shell = read_repo("Sources/Application/UARTShell.swift")
    sched12 = shell.split("func printScheduler12()")[1].split("func ")[0]

    assert 'uartPuts(" version=44")' in sched12
    assert "version=46" not in sched12


def test_s69_application_schedselftest_emits_version_44() -> None:
    app = read_repo("Sources/Application/Application.swift")
    assert 'uartPuts("schedselftest ok=1 version=44\\n")' in app


def test_s69_netiterate_sched12_probe_stays_version_44() -> None:
    net_iterate = read_repo("scripts/netboot/net-iterate.sh")
    assert (
        'probe_shell "sched12" "^sched12 ok=1 version=44 .*concurrency=1 .*rounds=3 '
        '.*completions=3 .*failures=0 .*dispatches=[1-9][0-9]* .*total=0 .*capacity=8 '
        '.*soak_core1=[1-9][0-9]* .*soak_core2=[1-9][0-9]* .*soak_core3=[1-9][0-9]* '
        '.*selftest=1"'
    ) in net_iterate


def test_s69_asplit_version_46_is_vmm_not_scheduler() -> None:
    shell = read_repo("Sources/Application/UARTShell.swift")
    asplit = shell.split("func printASplit()")[1].split("func ")[0]
    net_iterate = read_repo("scripts/netboot/net-iterate.sh")

    assert "kernel_vmm_boot_asplit_proven()" in asplit
    assert "KERNEL_SCHEDULER_VERSION" not in asplit
    assert 'uartPuts(" version=46\\n")' in asplit
    assert 'probe_shell "asplit" "^asplit ok=1 version=46"' in net_iterate


def test_s69_v44_contract_still_requires_version_44() -> None:
    v44 = read_repo("tests/test_runtime_v44_contract.py")
    for marker in (
        '" version=44"',
        "schedselftest ok=1 version=44",
        "^sched12 ok=1 version=44",
    ):
        assert marker in v44

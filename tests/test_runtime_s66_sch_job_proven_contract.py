"""S66 SCH-JOB-PROVEN-WEAK: job_proven must cover selftest static predicates."""

import pathlib
import re


ROOT = pathlib.Path(__file__).resolve().parents[1]

SELFTEST_FN = "int kernel_scheduler_secondary_job_selftest(void)"
PROVEN_FN = "int kernel_scheduler_secondary_job_proven(void)"

# Live selftest growth checks; proven uses the snapshot-static equivalents below.
GROWTH_CHECKS = (
    "kernel_scheduler_secondary_job_execution_count(1) > exec_before[1]",
    "kernel_scheduler_secondary_job_execution_count(2) > exec_before[2]",
    "kernel_scheduler_secondary_job_execution_count(3) > exec_before[3]",
    "kernel_scheduler_secondary_job_completion_count(1) > compl_before[1]",
    "kernel_scheduler_secondary_job_completion_count(2) > compl_before[2]",
    "kernel_scheduler_secondary_job_completion_count(3) > compl_before[3]",
)

STATIC_PER_CORE_CHECKS = (
    "kernel_scheduler_secondary_job_execution_count(1) > 0",
    "kernel_scheduler_secondary_job_execution_count(2) > 0",
    "kernel_scheduler_secondary_job_execution_count(3) > 0",
    "kernel_scheduler_secondary_job_completion_count(1) > 0",
    "kernel_scheduler_secondary_job_completion_count(2) > 0",
    "kernel_scheduler_secondary_job_completion_count(3) > 0",
)


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def _function_body(source: str, signature: str, next_signature: str | None = None) -> str:
    start = source.index(signature)
    if next_signature is not None:
        end = source.index(next_signature, start + len(signature))
    else:
        end = len(source)
    return source[start:end]


def _return_predicate_lines(body: str) -> list[str]:
    return_start = body.rindex("return ")
    return_end = body.index(" ? 1 : 0;", return_start)
    return_block = body[return_start:return_end]
    lines = []
    for raw in return_block.split("\n"):
        line = raw.strip().rstrip(" &&")
        if not line or line == "return":
            continue
        lines.append(line)
    return lines


def test_s66_job_proven_includes_selftest_static_predicates() -> None:
    scheduler = read_repo("Sources/Support/kernel_scheduler.c")

    proven_body = _function_body(
        scheduler,
        PROVEN_FN,
        "int kernel_scheduler_secondary_wake_proven(void)",
    )
    selftest_body = _function_body(
        scheduler,
        SELFTEST_FN,
        "int kernel_scheduler_secondary_wake_selftest(void)",
    )

    proven_predicates = _return_predicate_lines(proven_body)
    selftest_predicates = _return_predicate_lines(selftest_body)

    growth = set(GROWTH_CHECKS)
    static_selftest = [p for p in selftest_predicates if p not in growth]
    for predicate in static_selftest:
        assert predicate in proven_predicates, f"proven missing selftest predicate: {predicate}"

    for predicate in STATIC_PER_CORE_CHECKS:
        assert predicate in proven_predicates, f"proven missing per-core static check: {predicate}"


def test_s66_bootcert_uses_job_proven_not_selftest() -> None:
    shell = read_repo("Sources/Application/UARTShell.swift")
    bootcert = shell.split("func printBootcert()")[1].split("func ")[0]
    substrate = shell.split("func printSubstrateCertificate()")[1].split("func ")[0]

    assert "let jobExec = kernel_scheduler_secondary_job_proven()" in bootcert
    assert "let jobExec = kernel_scheduler_secondary_job_proven()" in substrate
    assert "kernel_scheduler_secondary_job_selftest()" not in bootcert
    assert "kernel_scheduler_secondary_job_selftest()" not in substrate


def test_s66_sched5_still_uses_live_job_selftest() -> None:
    shell = read_repo("Sources/Application/UARTShell.swift")
    sched5 = shell.split("func printScheduler5()")[1].split("func ")[0]

    assert "let jobExec = kernel_scheduler_secondary_job_selftest()" in sched5
    assert "kernel_scheduler_secondary_job_proven()" not in sched5


def test_s66_job_proven_gap_noops_settle_matches_selftest() -> None:
    scheduler = read_repo("Sources/Support/kernel_scheduler.c")

    proven_body = _function_body(
        scheduler,
        PROVEN_FN,
        "int kernel_scheduler_secondary_wake_proven(void)",
    )
    selftest_body = _function_body(
        scheduler,
        SELFTEST_FN,
        "int kernel_scheduler_secondary_wake_selftest(void)",
    )

    settle = re.compile(
        r"unsigned long noops = kernel_scheduler_secondary_job_noop_total\(\);\s+"
        r"unsigned long gap = kernel_scheduler_secondary_job_completion_gap\(\);\s+"
        r"if \(gap != noops\) \{\s+"
        r"unsigned long deadline = kernel_timer_now\(\) \+ 540000UL;.*?gap == noops",
        re.S,
    )
    assert settle.search(proven_body), "proven must share selftest gap==noops settle path"
    assert settle.search(selftest_body), "selftest gap==noops settle path must remain"

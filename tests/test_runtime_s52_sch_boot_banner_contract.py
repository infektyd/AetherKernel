"""S52/S53 scheduler boot proof: v31–v44 require post-selftest schedselftest tokens."""

import pathlib
import re


ROOT = pathlib.Path(__file__).resolve().parents[1]

SCHED_VERSIONS = tuple(range(31, 45))

ENABLE_BANNERS = {
    31: "runtime v31: preemptive scheduler substrate",
    32: "runtime v32: smp secondary-core bring-up",
    33: "runtime v33: atomics spinlocks per-core run queues",
    34: "runtime v34: timer-driven smp scheduler dispatch",
    35: "runtime v35: secondary-owned scheduler workers",
    36: "runtime v36: timer-fed secondary scheduler workers",
    37: "runtime v37: timer-fed secondary C scheduler jobs",
    38: "runtime v38: secondary scheduler wake protocol",
    39: "runtime v39: secondary scheduler handoff protocol",
    40: "runtime v40: scheduler backpressure protocol",
    41: "runtime v41: secondary scheduler work stealing",
    42: "runtime v42: secondary scheduler load balancing",
    43: "runtime v43: secondary scheduler priority preemption",
    44: "runtime v44: bounded smp concurrency soak",
}

PROVEN_TOKENS = {
    version: f'schedselftest ok=1 version={version}'
    for version in SCHED_VERSIONS
}


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def _boot_proof_schedselftest_greps(net_iterate: str) -> list[str]:
    """Contiguous schedselftest boot-proof greps (net-iterate main success gate)."""
    return [
        line
        for line in net_iterate.splitlines()
        if 'grep -qa "schedselftest ok=1 version=' in line
    ]


def _doctor_health_schedselftest_greps(doctor: str) -> list[str]:
    """schedselftest greps in netboot-doctor bring-up verified health chain."""
    return [
        line
        for line in doctor.splitlines()
        if 'grep -q "schedselftest ok=1 version=' in line
    ]


def _doctor_health_enable_banner_greps(doctor: str) -> list[str]:
    """Enable-banner greps for v31–v44 inside doctor health chain (must be absent)."""
    return [
        line
        for line in doctor.splitlines()
        if 'grep -q "runtime v' in line and any(f"runtime v{v}:" in line for v in SCHED_VERSIONS)
    ]


def _stale_sd_scheduler_banner_greps(net_iterate: str) -> list[str]:
    """v31–v44 enable-banner greps in stale-SD fallback classifiers (not boot proof)."""
    banners = ENABLE_BANNERS.values()
    return [
        line
        for line in net_iterate.splitlines()
        if 'grep -qa "runtime v' in line and any(b in line for b in banners)
    ]


def test_s52_netiterate_boot_proof_uses_schedselftest_not_enable_banners() -> None:
    net_iterate = read_repo("scripts/netboot/net-iterate.sh")
    proof_greps = _boot_proof_schedselftest_greps(net_iterate)
    proof_blob = "\n".join(proof_greps)

    assert len(proof_greps) == len(SCHED_VERSIONS), (
        "expected one schedselftest boot-proof grep per v31–v44"
    )

    for version in SCHED_VERSIONS:
        token = PROVEN_TOKENS[version]
        banner = ENABLE_BANNERS[version]
        assert token in proof_blob, f"v{version} post-selftest token missing from boot proof gate"
        assert banner not in proof_blob, (
            f"v{version} enable banner must not be boot proof grep: {banner!r}"
        )


def test_s52_application_emits_schedselftest_only_after_pass() -> None:
    app = read_repo("Sources/Application/Application.swift")

    for version in SCHED_VERSIONS:
        token = f'schedselftest ok=1 version={version}'
        assert token in app, f"missing UART token {token!r} in Application.swift"
        # Each token must be guarded by a non-zero selftest result.
        pattern = (
            rf"if runtimeV{version} != 0 \{{\s*\n\s*uartPuts\(\"schedselftest ok=1 version={version}\\n\"\)"
        )
        assert re.search(pattern, app), (
            f"schedselftest v{version} not guarded by runtimeV{version} != 0"
        )


def test_s52_enable_banners_remain_telemetry() -> None:
    app = read_repo("Sources/Application/Application.swift")

    for banner in ENABLE_BANNERS.values():
        assert banner in app, f"enable banner removed from telemetry: {banner!r}"


def test_s53_doctor_health_requires_schedselftest_not_enable_banners() -> None:
    doctor = read_repo("scripts/netboot/netboot-doctor.sh")
    health_greps = _doctor_health_schedselftest_greps(doctor)
    health_blob = "\n".join(health_greps)
    banner_greps = _doctor_health_enable_banner_greps(doctor)

    assert len(health_greps) == len(SCHED_VERSIONS), (
        "expected one schedselftest health grep per v31–v44 in netboot-doctor"
    )
    assert not banner_greps, (
        "netboot-doctor v31–v44 health must not grep enable banners: "
        + ", ".join(banner_greps[:3])
    )

    for version in SCHED_VERSIONS:
        token = PROVEN_TOKENS[version]
        banner = ENABLE_BANNERS[version]
        assert token in health_blob, f"v{version} schedselftest token missing from doctor health"
        assert banner not in health_blob, (
            f"v{version} enable banner must not be doctor health grep: {banner!r}"
        )


def test_s53_stale_sd_scheduler_banners_are_classifiers_not_proof() -> None:
    net_iterate = read_repo("scripts/netboot/net-iterate.sh")
    stale_banner_greps = _stale_sd_scheduler_banner_greps(net_iterate)

    assert stale_banner_greps, "expected stale-SD v31–v44 enable-banner classifiers"
    assert "fingerprint banner-only SD fallback" in net_iterate, (
        "stale-SD block must document banner greps are classifiers only"
    )

    proof_greps = _boot_proof_schedselftest_greps(net_iterate)
    proof_blob = "\n".join(proof_greps)
    for version in SCHED_VERSIONS:
        banner = ENABLE_BANNERS[version]
        assert banner not in proof_blob, (
            f"v{version} enable banner must not be boot proof grep: {banner!r}"
        )

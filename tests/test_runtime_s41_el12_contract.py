import pathlib
import re


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_event_log_emit_takes_irq_save_and_smp_spinlock() -> None:
    source = read_repo("Sources/Support/kernel_event_log.c")

    assert "kernel_spinlock_t event_log_lock" in source
    assert "kernel_spinlock_lock(&event_log_lock)" in source
    assert "kernel_spinlock_unlock(&event_log_lock)" in source

    lock_helper = source.split("static void event_log_lock_irq", 1)[1]
    lock_helper = lock_helper.split("static void event_log_unlock_irq", 1)[0]
    assert "irq_save()" in lock_helper
    assert "kernel_spinlock_lock(&event_log_lock)" in lock_helper
    assert "kernel_spinlock_init" not in lock_helper
    assert lock_helper.index("irq_save()") < lock_helper.index(
        "kernel_spinlock_lock(&event_log_lock)"
    )

    emit_fn = source.split("void kernel_event_emit(", 1)[1].split("\n}\n", 1)[0]
    assert "event_log_lock_irq(&flags)" in emit_fn
    assert "event_log_unlock_irq(flags)" in emit_fn


def test_event_log_spinlock_init_site() -> None:
    source = read_repo("Sources/Support/kernel_event_log.c")
    lock_path = read_repo("Sources/Support/kernel_lock.c")

    lock_helper = source.split("static void event_log_lock_irq", 1)[1]
    lock_helper = lock_helper.split("static void event_log_unlock_irq", 1)[0]
    assert "kernel_spinlock_init" not in lock_helper

    init_fn = source.split("void kernel_event_log_init(", 1)[1].split("\n}\n", 1)[0]
    # BSS-zero is a valid unlocked lock (kernel_spinlock_init stores state=0).
    assert "kernel_spinlock_init" not in init_fn
    assert "lock_initialized" not in source
    assert "kernel_atomic_store_u32(&lock->state, 0)" in lock_path


def test_event_log_readers_hold_same_smp_lock() -> None:
    source = read_repo("Sources/Support/kernel_event_log.c")

    for fn_name in (
        "kernel_event_count",
        "kernel_event_lost_count",
        "kernel_event_sequence",
        "kernel_event_kind",
        "kernel_event_ticks",
        "kernel_event_seq",
        "kernel_event_arg0",
        "kernel_event_arg1",
        "kernel_event_arg2",
    ):
        fn_body = source.split(f"unsigned ", 1)[0]  # reset — parse per signature
        if fn_name == "kernel_event_count":
            fn_body = source.split("unsigned int kernel_event_count(", 1)[1].split("\n}\n", 1)[0]
        elif fn_name == "kernel_event_lost_count":
            fn_body = source.split("unsigned long kernel_event_lost_count(", 1)[1].split("\n}\n", 1)[0]
        elif fn_name == "kernel_event_sequence":
            fn_body = source.split("unsigned long kernel_event_sequence(", 1)[1].split("\n}\n", 1)[0]
        else:
            pattern = rf"{fn_name}\(unsigned int index\)"
            assert re.search(pattern, source), fn_name
            fn_body = source.split(f"{fn_name}(unsigned int index)", 1)[1].split("\n}\n", 1)[0]

        assert "event_log_lock_irq(&flags)" in fn_body, fn_name
        assert "event_log_unlock_irq(flags)" in fn_body, fn_name


def test_event_log_reuses_kernel_spinlock_not_new_lock_type() -> None:
    source = read_repo("Sources/Support/kernel_event_log.c")
    support = read_repo("Sources/Support/include/Support.h")

    assert "kernel_spinlock_t" in support
    assert "void kernel_spinlock_lock(kernel_spinlock_t *lock);" in support
    assert "ticket" not in source.lower()
    assert "mutex" not in source.lower()


def test_event_log_selftest_full_ring_checks_lost_count_before_pass() -> None:
    """EL-18 (S43): full-ring selftest path must observe lost_count before return 1."""
    source = read_repo("Sources/Support/kernel_event_log.c")
    selftest = source.split("int kernel_event_log_selftest(", 1)[1].split("\n}\n", 1)[0]

    assert "kernel_event_lost_count" in selftest
    assert "lost_before" in selftest

    full_ring = selftest.split("Full ring: read-only walk", 1)[1]
    lost_gate = full_ring.split("if (lost != expected_lost)", 1)[0]
    assert "unsigned long lost = kernel_event_lost_count()" in lost_gate
    assert "expected_lost" in full_ring
    assert full_ring.index("unsigned long lost = kernel_event_lost_count()") < full_ring.index(
        "if (lost != expected_lost)"
    )

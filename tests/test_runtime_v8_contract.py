import pathlib
import re


ROOT = pathlib.Path(__file__).resolve().parents[1]


COMMANDS_V8 = (
    "commands=help,status,heap,queues,tasks,tasks2,kobjects,mailboxes,sendtest,diag,irqs,timers,memcheck,"
    "faults,retained,retained-clear,memmap,frames,heapcheck,framecheck,"
    "stress,frameprobe,bootcheck,soak,heap-invalid-free-test,heap-double-free-test,panic-test,fault-test,reboot"
)


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_support_declares_runtime_v8_heap_and_frame_guard_api() -> None:
    support = read_repo("Sources/Support/include/Support.h")

    for symbol in (
        "HEAP_GUARD_OK",
        "HEAP_GUARD_INVALID_FREE",
        "HEAP_GUARD_DOUBLE_FREE",
        "heap_guard_last_error",
        "heap_invalid_free_count",
        "heap_double_free_count",
        "heap_corruption_count",
        "heap_guard_selftest",
        "KERNEL_FRAME_ERROR_NONE",
        "KERNEL_FRAME_ERROR_BAD_FREE",
        "KERNEL_FRAME_ERROR_DOUBLE_FREE",
        "kernel_frame_last_error",
        "kernel_frame_bad_free_count",
        "kernel_frame_double_free_count",
        "kernel_frame_allocator_stress_selftest",
    ):
        assert symbol in support


def test_heap_allocator_has_validation_before_mutation_and_poisoning() -> None:
    alloc = read_repo("Sources/Support/alloc.c")

    for marker in (
        "HEAP_FREE_POISON",
        "heap_validate_allocation_unsafe",
        "heap_record_guard_error",
        "heap_panic_invalid_free",
        "heap_panic_double_free",
        "heap_poison_payload",
        "heap_guard_last_error",
        "heap_guard_selftest",
        "heap_invalid_free_count",
        "heap_double_free_count",
        "heap_corruption_count",
    ):
        assert marker in alloc

    assert alloc.index("heap_validate_allocation_unsafe(ptr") < alloc.index("block_header *next_H")
    assert alloc.index("heap_validate_allocation_unsafe(ptr") < alloc.index("void *new_ptr = malloc(size)")
    assert "malloc(" not in alloc[alloc.index("int heap_guard_selftest"):alloc.index("void *swift_slowAlloc")]


def test_heap_free_accepts_8_byte_aligned_posix_memalign_payloads() -> None:
    alloc = read_repo("Sources/Support/alloc.c")
    payload_range_body = alloc[
        alloc.index("static int heap_payload_in_range"):
        alloc.index("static void heap_poison_payload")
    ]
    posix_body = alloc[
        alloc.index("int posix_memalign"):
        alloc.index("void *calloc")
    ]

    assert "(addr & 0x7UL) == 0" in payload_range_body
    assert "alignment < sizeof(void *)" in posix_body


def test_heap_integrity_check_reports_stable_reason_codes() -> None:
    alloc = read_repo("Sources/Support/alloc.c")

    for marker in (
        "HEAP_GUARD_SENTINEL",
        "HEAP_GUARD_BLOCK_SIZE",
        "HEAP_GUARD_BLOCK_FOOTER",
        "HEAP_GUARD_FREE_RANGE",
        "HEAP_GUARD_FREE_ALLOCATED",
        "HEAP_GUARD_FREE_FOOTER",
        "HEAP_GUARD_FREE_DUP",
        "heap_record_corruption",
    ):
        assert marker in alloc


def test_heap_panic_paths_preserve_specific_guard_reason() -> None:
    alloc = read_repo("Sources/Support/alloc.c")

    for marker in (
        "HEAP_FREE_CONTEXT_DIRECT",
        "HEAP_FREE_CONTEXT_SWIFT_SLOW_DEALLOC",
        "heap_free_context",
        "heap_free_context_align_mask",
        "heap_free_context_return_address",
    ):
        assert marker in alloc

    for helper in ("heap_panic_invalid_free", "heap_panic_double_free"):
        match = re.search(rf"static void {helper}\(void \*ptr\) \{{(?P<body>.*?)\n\}}", alloc, re.S)
        assert match is not None
        assert "heap_record_guard_error" not in match.group("body")
        assert "kernel_panic_with_detail" in match.group("body")
        assert "heap_free_context" in match.group("body")
        assert "heap_free_context_return_address" in match.group("body")
        assert "(unsigned long)ptr" in match.group("body")


def test_free_records_direct_caller_return_address_for_guard_panics() -> None:
    alloc = read_repo("Sources/Support/alloc.c")
    body = alloc[
        alloc.index("void free(void *ptr)"):
        alloc.index("int posix_memalign")
    ]

    assert "unsigned long caller = (unsigned long)__builtin_return_address(0);" in body
    assert "if (heap_free_context_return_address == 0)" in body
    assert "heap_free_context_return_address = caller;" in body
    assert body.index("heap_free_context_return_address = caller;") < body.index("heap_validate_allocation_unsafe")
    assert "heap_free_context_return_address = 0;" in body


def test_swift_slow_alloc_routes_unknown_alignment_through_aligned_path() -> None:
    alloc = read_repo("Sources/Support/alloc.c")
    body = alloc[
        alloc.index("void *swift_slowAlloc"):
        alloc.index("void swift_slowDealloc")
    ]

    assert "alignMask == (size_t)-1" in body
    assert "alignment = 16" in body
    assert "posix_memalign" in body
    assert "alignMask == 0 || alignMask == (size_t)-1 || alignMask <= 15" not in body


def test_swift_slow_dealloc_sets_heap_free_context_around_free() -> None:
    alloc = read_repo("Sources/Support/alloc.c")
    body = alloc[
        alloc.index("void swift_slowDealloc"):
        alloc.index("unsigned long heap_total_bytes")
    ]

    assert "unsigned long flags = irq_save();" in body
    assert "heap_free_context = HEAP_FREE_CONTEXT_SWIFT_SLOW_DEALLOC;" in body
    assert "heap_free_context_align_mask = alignMask;" in body
    assert "heap_free_context_return_address = (unsigned long)__builtin_return_address(0);" in body
    assert body.index("heap_free_context = HEAP_FREE_CONTEXT_SWIFT_SLOW_DEALLOC;") < body.index("free(ptr);")
    assert body.index("free(ptr);") < body.index("heap_free_context = HEAP_FREE_CONTEXT_DIRECT;")
    assert body.index("heap_free_context = HEAP_FREE_CONTEXT_DIRECT;") < body.index("irq_restore(flags);")


def test_frame_allocator_has_bad_free_counts_and_stress_selftest() -> None:
    memory_map = read_repo("Sources/Support/memory_map.c")

    for marker in (
        "KERNEL_FRAME_ERROR_NONE",
        "KERNEL_FRAME_ERROR_BAD_FREE",
        "KERNEL_FRAME_ERROR_DOUBLE_FREE",
        "frame_bad_frees",
        "frame_double_frees",
        "kernel_frame_last_error",
        "kernel_frame_bad_free_count",
        "kernel_frame_double_free_count",
        "kernel_frame_allocator_stress_selftest",
        "unsigned long frames[4]",
    ):
        assert marker in memory_map

    assert "malloc(" not in memory_map
    assert " free(" not in memory_map


def test_uart_shell_v8_heapcheck_and_framecheck_commands_exist() -> None:
    shell = read_repo("Sources/Application/UARTShell.swift")

    for marker in (
        COMMANDS_V8,
        "heapcheck ok=",
        " invalid_frees=",
        " double_frees=",
        " corruptions=",
        "framecheck ok=",
        " bad_frees=",
        " stress=",
        'shellBufferEquals("heapcheck")',
        'shellBufferEquals("framecheck")',
    ):
        assert marker in shell


def test_runtime_v8_boot_marker_and_netboot_gates_exist() -> None:
    app = read_repo("Sources/Application/Application.swift")
    net_iterate = read_repo("net-iterate.sh")
    doctor = read_repo("netboot-doctor.sh")

    for source in (app, net_iterate, doctor):
        assert "runtime v8: allocator guardrails" in source

    for source in (net_iterate, doctor):
        assert COMMANDS_V8 in source

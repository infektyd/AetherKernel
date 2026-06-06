import re
import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def read_hex_macro(source: str, name: str) -> int:
    match = re.search(rf"#define\s+{name}\s+(0x[0-9A-Fa-f]+)UL", source)
    assert match, f"missing hex macro {name}"
    return int(match.group(1), 16)


def read_dec_macro(source: str, name: str) -> int:
    match = re.search(rf"#define\s+{name}\s+([0-9]+)UL", source)
    assert match, f"missing decimal macro {name}"
    return int(match.group(1), 10)


def test_support_declares_runtime_v7_memory_map_and_frame_api() -> None:
    support = read_repo("Sources/Support/include/Support.h")

    for symbol in (
        "KERNEL_PAGE_SIZE",
        "KERNEL_FRAME_BASE",
        "KERNEL_FRAME_LIMIT",
        "kernel_memory_init",
        "kernel_memory_region_count",
        "kernel_memory_region_start",
        "kernel_memory_region_end",
        "kernel_memory_region_kind",
        "kernel_memory_region_name_len",
        "kernel_memory_region_name_byte",
        "kernel_memory_reserved_bytes",
        "kernel_memory_map_valid",
        "kernel_frame_base",
        "kernel_frame_limit",
        "kernel_frame_total_count",
        "kernel_frame_free_count",
        "kernel_frame_used_count",
        "kernel_frame_reserved_count",
        "kernel_frame_alloc",
        "kernel_frame_free",
        "kernel_frame_allocator_selftest",
    ):
        assert symbol in support


def test_memory_map_uses_fixed_non_overlapping_regions_and_no_allocation() -> None:
    path = ROOT / "Sources/Support/memory_map.c"
    assert path.exists(), "missing fixed-storage Runtime V7 memory map support"
    memory_map = path.read_text()

    for marker in (
        "KERNEL_REGION_FIRMWARE_LOW",
        "KERNEL_REGION_BOOT_STACK",
        "KERNEL_REGION_KERNEL_IMAGE_MMU",
        "KERNEL_REGION_KERNEL_SPARE",
        "KERNEL_REGION_RETAINED",
        "KERNEL_REGION_HEAP",
        "KERNEL_REGION_FRAMES",
        "kernel_memory_check_invariants",
        "kernel_panic(\"memory-map-overlap\")",
        "if (memory_initialized)",
        "KERNEL_RETAINED_RECORD_ADDR",
        "0x00400000UL",
        "0x00800000UL",
    ):
        assert marker in memory_map

    assert "malloc(" not in memory_map
    assert " free(" not in memory_map


def test_frame_allocator_uses_4k_bitmap_above_heap_and_selftest() -> None:
    path = ROOT / "Sources/Support/memory_map.c"
    assert path.exists(), "missing fixed-storage Runtime V7 memory map support"
    memory_map = path.read_text()

    for marker in (
        "KERNEL_PAGE_SIZE != 4096UL",
        "KERNEL_FRAME_BASE",
        "KERNEL_FRAME_LIMIT",
        "frame_bitmap",
        "kernel_frame_alloc",
        "kernel_frame_free",
        "kernel_frame_allocator_selftest",
        "irq_save()",
        "irq_restore(flags)",
    ):
        assert marker in memory_map


def test_runtime_v7_geometry_is_locked_to_current_heap_and_frame_window() -> None:
    support = read_repo("Sources/Support/include/Support.h")
    memory_map = read_repo("Sources/Support/memory_map.c")

    page_size = read_dec_macro(support, "KERNEL_PAGE_SIZE")
    frame_base = read_hex_macro(support, "KERNEL_FRAME_BASE")
    frame_limit = read_hex_macro(support, "KERNEL_FRAME_LIMIT")

    assert page_size == 4096
    assert frame_base == 0x00800000
    assert frame_limit == 0x04000000
    assert (frame_limit - frame_base) // page_size == 14336

    for marker in (
        "#define KERNEL_HEAP_BASE            0x00400000UL",
        "#define KERNEL_HEAP_LIMIT           0x00800000UL",
        "#define KERNEL_RETAINED_LIMIT       0x00400000UL",
        "if (regions[i].kind != KERNEL_MEMORY_REGION_KIND_FRAMES)",
    ):
        assert marker in memory_map


def test_uart_shell_v7_memmap_and_frames_commands_exist() -> None:
    shell = read_repo("Sources/Application/UARTShell.swift")

    for marker in (
        "commands=help,protocol,status,heap,queues,tasks,tasks2,kobjects,drivers,drivercheck,mailboxes,sendtest,supervisor,health,capcheck,events,runtime,agent,certificate,diag,irqs,timers,memcheck,faults,retained,retained-clear,memmap,mmu,pools,poolcheck,heapfrag,poolstats,frames,heapcheck,framecheck,stress,frameprobe,bootcert,canceltest,taskcheck,channeltest,bootcheck,soak,heap-invalid-free-test,heap-double-free-test,panic-test,fault-test,reboot",
        "memmap valid=",
        " region index=",
        " name=",
        " kind=",
        "frames total=",
        " free=",
        " used=",
        " selftest=",
    ):
        assert marker in shell


def test_runtime_v7_boot_marker_and_netboot_gates_exist() -> None:
    app = read_repo("Sources/Application/Application.swift")
    net_iterate = read_repo("net-iterate.sh")
    doctor = read_repo("netboot-doctor.sh")

    for source in (app, net_iterate, doctor):
        assert "runtime v7: memory map + frame allocator" in source

import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]


COMMANDS_V10 = (
    "commands=help,protocol,status,heap,queues,tasks,tasks2,kobjects,drivers,drivercheck,mailboxes,sendtest,supervisor,health,capcheck,events,runtime,agent,certificate,sched,cores,diag,irqs,timers,memcheck,"
    "faults,retained,retained-clear,memmap,mmu,pools,poolcheck,heapfrag,poolstats,frames,heapcheck,framecheck,"
    "stress,frameprobe,bootcert,canceltest,taskcheck,channeltest,bootcheck,soak,heap-invalid-free-test,"
    "heap-double-free-test,panic-test,fault-test,reboot"
)


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_support_declares_runtime_v10_guard_probe_api() -> None:
    support = read_repo("Sources/Support/include/Support.h")

    for symbol in (
        "heap_guard_invalid_free_test",
        "heap_guard_double_free_test",
        "kernel_frame_guard_probe_selftest",
        "kernel_frame_guard_probe_last_ok",
    ):
        assert symbol in support


def test_heap_guard_panic_tests_are_explicit_destructive_paths() -> None:
    alloc = read_repo("Sources/Support/alloc.c")

    for marker in (
        "void heap_guard_invalid_free_test(void)",
        "void heap_guard_double_free_test(void)",
        "free((void *)0x123450UL)",
        "kernel_panic(\"heap-double-free-test-alloc\")",
    ):
        assert marker in alloc

    double_free_body = alloc[
        alloc.index("void heap_guard_double_free_test(void)"):
        alloc.index("void *swift_slowAlloc")
    ]
    assert double_free_body.count("free(p);") == 2


def test_frame_guard_probe_is_non_destructive_and_counts_errors() -> None:
    memory_map = read_repo("Sources/Support/memory_map.c")

    for marker in (
        "kernel_frame_guard_probe_selftest",
        "frame_guard_probe_last_ok",
        "before_bad",
        "before_double",
        "kernel_frame_free(KERNEL_FRAME_BASE - KERNEL_PAGE_SIZE)",
        "kernel_frame_free(frame)",
    ):
        assert marker in memory_map

    body = memory_map[
        memory_map.index("int kernel_frame_guard_probe_selftest(void)"):
        memory_map.index("unsigned int kernel_frame_guard_probe_last_ok")
    ]
    assert "kernel_panic" not in body
    assert "malloc(" not in body


def test_uart_shell_v10_guard_commands_exist() -> None:
    shell = read_repo("Sources/Application/UARTShell.swift")

    for marker in (
        COMMANDS_V10,
        "frameprobe ok=",
        " bad_frees=",
        " double_frees=",
        " error=",
        "shell heap-invalid-free-test reason=command",
        "shell heap-double-free-test reason=command",
        'shellBufferSliceEquals(commandStart, commandLen, "frameprobe")',
        'shellBufferSliceEquals(commandStart, commandLen, "heap-invalid-free-test")',
        'shellBufferSliceEquals(commandStart, commandLen, "heap-double-free-test")',
    ):
        assert marker in shell


def test_runtime_v10_boot_marker_and_netboot_gates_exist() -> None:
    app = read_repo("Sources/Application/Application.swift")
    net_iterate = read_repo("net-iterate.sh")
    doctor = read_repo("netboot-doctor.sh")

    for source in (app, net_iterate, doctor):
        assert "runtime v10: explicit guard probes" in source

    for source in (net_iterate, doctor):
        assert COMMANDS_V10 in source

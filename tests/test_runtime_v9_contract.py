import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]


COMMANDS_V9 = (
    "commands=help,protocol,status,heap,queues,tasks,tasks2,kobjects,drivers,drivercheck,mailboxes,sendtest,supervisor,health,capcheck,events,runtime,agent,certificate,sched,sched2,sched3,sched4,sched5,sched6,sched7,cores,locks,runqueues,diag,irqs,timers,memcheck,"
    "faults,retained,retained-clear,memmap,mmu,pools,poolcheck,heapfrag,poolstats,frames,heapcheck,framecheck,"
    "stress,frameprobe,bootcert,canceltest,taskcheck,channeltest,bootcheck,soak,heap-invalid-free-test,heap-double-free-test,panic-test,fault-test,reboot"
)


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_support_declares_runtime_v9_pressure_api() -> None:
    support = read_repo("Sources/Support/include/Support.h")

    for symbol in (
        "heap_pressure_selftest",
        "heap_pressure_last_peak_bytes",
        "heap_pressure_last_leak_bytes",
        "kernel_frame_pressure_selftest",
        "kernel_frame_pressure_last_peak_count",
        "kernel_frame_pressure_last_leak_count",
    ):
        assert symbol in support


def test_heap_pressure_selftest_is_bounded_and_restores_heap() -> None:
    alloc = read_repo("Sources/Support/alloc.c")

    for marker in (
        "HEAP_PRESSURE_BLOCK_COUNT",
        "heap_pressure_selftest",
        "heap_pressure_last_peak",
        "heap_pressure_last_leak",
        "heap_integrity_check()",
        "before_free",
        "after_free",
    ):
        assert marker in alloc

    body = alloc[
        alloc.index("int heap_pressure_selftest(void)"):
        alloc.index("unsigned long heap_pressure_last_peak_bytes")
    ]
    assert "malloc(" in body
    assert "free(" in body
    assert "while true" not in body


def test_frame_pressure_selftest_is_bounded_and_restores_frames() -> None:
    memory_map = read_repo("Sources/Support/memory_map.c")

    for marker in (
        "FRAME_PRESSURE_COUNT",
        "kernel_frame_pressure_selftest",
        "frame_pressure_last_peak",
        "frame_pressure_last_leak",
        "before_free",
        "after_free",
    ):
        assert marker in memory_map

    body = memory_map[
        memory_map.index("int kernel_frame_pressure_selftest(void)"):
        memory_map.index("unsigned long kernel_frame_pressure_last_peak_count")
    ]
    assert "kernel_frame_alloc()" in body
    assert "kernel_frame_free(" in body
    assert "malloc(" not in body
    assert " free(" not in body


def test_uart_shell_v9_stress_command_exists() -> None:
    shell = read_repo("Sources/Application/UARTShell.swift")

    for marker in (
        COMMANDS_V9,
        "stress ok=",
        " heap=",
        " frames=",
        " heap_peak=",
        " frame_peak=",
        " heap_leak=",
        " frame_leak=",
        'shellBufferSliceEquals(commandStart, commandLen, "stress")',
    ):
        assert marker in shell


def test_runtime_v9_boot_marker_and_netboot_gates_exist() -> None:
    app = read_repo("Sources/Application/Application.swift")
    net_iterate = read_repo("net-iterate.sh")
    doctor = read_repo("netboot-doctor.sh")

    for source in (app, net_iterate, doctor):
        assert "runtime v9: bounded memory pressure self-tests" in source

    for source in (net_iterate, doctor):
        assert COMMANDS_V9 in source

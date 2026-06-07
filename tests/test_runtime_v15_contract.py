import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]

COMMANDS_V15 = (
    "commands=help,protocol,status,heap,queues,tasks,tasks2,kobjects,drivers,drivercheck,mailboxes,sendtest,"
    "supervisor,health,capcheck,events,runtime,agent,certificate,sched,sched2,sched3,sched4,sched5,sched6,sched7,sched8,sched9,sched10,sched11,cores,locks,runqueues,diag,irqs,timers,memcheck,faults,retained,"
    "retained-clear,memmap,mmu,pools,poolcheck,heapfrag,poolstats,frames,heapcheck,framecheck,stress,frameprobe,"
    "bootcert,canceltest,taskcheck,channeltest,bootcheck,soak,heap-invalid-free-test,heap-double-free-test,panic-test,"
    "fault-test,reboot"
)


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_support_declares_runtime_v15_handle_api() -> None:
    support = read_repo("Sources/Support/include/Support.h")

    for symbol in (
        "KERNEL_OBJECT_CAP_INSPECT",
        "KERNEL_OBJECT_CAP_CONTROL",
        "KERNEL_OBJECT_CAP_SEND",
        "KERNEL_OBJECT_CAP_RECEIVE",
        "KERNEL_OBJECT_CAP_SUPERVISE",
        "KERNEL_OBJECT_LOOKUP_OK",
        "KERNEL_OBJECT_LOOKUP_BAD_HANDLE",
        "KERNEL_OBJECT_LOOKUP_STALE",
        "KERNEL_OBJECT_LOOKUP_CAP_DENIED",
        "KERNEL_OBJECT_HANDLE_INVALID",
        "kernel_object_caps",
        "kernel_object_generation",
        "kernel_object_make_handle",
        "kernel_object_handle_index",
        "kernel_object_handle_generation",
        "kernel_object_handle_caps",
        "kernel_object_lookup_id",
        "kernel_object_unregister_handle",
        "kernel_object_handle_last_error",
        "kernel_object_handle_selftest",
        "kernel_object_capcheck_selftest",
    ):
        assert symbol in support


def test_kernel_registry_v15_generation_and_capability_checks_exist() -> None:
    registry = read_repo("Sources/Support/kernel_registry.c")

    for marker in (
        "generation",
        "caps",
        "next_generation",
        "KERNEL_OBJECT_LOOKUP_STALE",
        "KERNEL_OBJECT_LOOKUP_CAP_DENIED",
        "kernel_object_make_handle",
        "kernel_object_lookup_id",
        "kernel_object_unregister_handle",
        "kernel_object_handle_selftest",
        "kernel_object_capcheck_selftest",
    ):
        assert marker in registry


def test_runtime_v15_boot_marker_and_selftests_are_wired() -> None:
    app = read_repo("Sources/Application/Application.swift")

    assert "runtime v15: capability-tagged kernel handles" in app
    assert "kernel_object_handle_selftest()" in app
    assert "kernel_object_capcheck_selftest()" in app


def test_uart_shell_v15_capcheck_command_and_handle_details_exist() -> None:
    shell = read_repo("Sources/Application/UARTShell.swift")

    for marker in (
        COMMANDS_V15,
        "handle_selftest=",
        "cap_selftest=",
        " handle=",
        " generation=",
        " caps=",
        "capcheck ok=",
        " inspect=",
        " denied=",
        " stale=",
        " last_error=",
        'shellBufferSliceEquals(commandStart, commandLen, "capcheck")',
    ):
        assert marker in shell


def test_runtime_v15_netboot_gates_and_shell_probe_exist() -> None:
    net_iterate = read_repo("net-iterate.sh")
    doctor = read_repo("netboot-doctor.sh")

    for source in (net_iterate, doctor):
        assert "runtime v15: capability-tagged kernel handles" in source
        assert COMMANDS_V15 in source

    for marker in (
        "probe shell: capcheck",
        "^capcheck ok=1 .*denied=1 .*stale=1",
        "^kobjects count=.* active=.* handle_selftest=1 .*cap_selftest=1",
        "stale pre-V43 SD fallback",
    ):
        assert marker in net_iterate

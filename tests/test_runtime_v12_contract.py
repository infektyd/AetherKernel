import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]

COMMANDS_V12 = (
    "commands=help,protocol,status,heap,queues,tasks,tasks2,kobjects,drivers,drivercheck,mailboxes,sendtest,supervisor,health,capcheck,events,runtime,agent,certificate,sched,sched2,sched3,sched4,sched5,sched6,cores,locks,runqueues,diag,irqs,timers,"
    "memcheck,faults,retained,retained-clear,memmap,mmu,pools,poolcheck,heapfrag,poolstats,frames,heapcheck,"
    "framecheck,stress,frameprobe,bootcert,canceltest,taskcheck,channeltest,bootcheck,soak,heap-invalid-free-test,"
    "heap-double-free-test,panic-test,fault-test,reboot"
)


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_support_declares_runtime_v12_kernel_object_and_task_registry_api() -> None:
    support = read_repo("Sources/Support/include/Support.h")

    for symbol in (
        "KERNEL_OBJECT_KIND_TASK",
        "KERNEL_OBJECT_KIND_DRIVER",
        "KERNEL_OBJECT_KIND_RUNTIME",
        "KERNEL_OBJECT_FLAG_ACTIVE",
        "KERNEL_TASK_STATE_RUNNING",
        "KERNEL_TASK_STATE_WAITING",
        "KERNEL_TASK_STATE_IDLE",
        "kernel_object_registry_init",
        "kernel_object_register",
        "kernel_object_count",
        "kernel_object_capacity",
        "kernel_object_active_count",
        "kernel_object_kind",
        "kernel_object_flags",
        "kernel_object_name_len",
        "kernel_object_name_byte",
        "kernel_task_registry_init",
        "kernel_task_register",
        "kernel_task_mark_state",
        "kernel_task_record_tick",
        "kernel_task_count",
        "kernel_task_capacity",
        "kernel_task_object_id",
        "kernel_task_state",
        "kernel_task_tick_count",
        "kernel_task_name_len",
        "kernel_task_name_byte",
        "kernel_task_registry_selftest",
        "kernel_object_registry_selftest",
    ):
        assert symbol in support


def test_runtime_v12_boot_marker_and_registry_init_exist() -> None:
    app = read_repo("Sources/Application/Application.swift")

    assert "runtime v12: kernel object table + task registry" in app
    assert "kernel_object_registry_init()" in app
    assert "kernel_task_registry_init()" in app
    assert app.index("kernel_memory_init()") < app.index("kernel_object_registry_init()")
    assert app.index("kernel_object_registry_init()") < app.index("spawnAetherTask(TASK_SHELL_ID")


def test_uart_shell_v12_commands_and_machine_checkable_responses_exist() -> None:
    shell = read_repo("Sources/Application/UARTShell.swift")

    for marker in (
        COMMANDS_V12,
        "kobjects count=",
        " capacity=",
        " active=",
        " object index=",
        " kind=",
        " flags=",
        " name=",
        "tasks2 count=",
        " task index=",
        " object=",
        " state=",
        " ticks=",
        "period_ms=",
        'shellBufferSliceEquals(commandStart, commandLen, "kobjects")',
        'shellBufferSliceEquals(commandStart, commandLen, "tasks2")',
    ):
        assert marker in shell


def test_runtime_v12_demo_tasks_record_registry_ticks() -> None:
    app = read_repo("Sources/Application/Application.swift")

    for marker in (
        "TASK_FAST_ID",
        "TASK_SLOW_ID",
        "TASK_LONG_ID",
        "kernel_task_record_tick(TASK_FAST_ID",
        "kernel_task_record_tick(TASK_SLOW_ID",
        "kernel_task_record_tick(TASK_LONG_ID",
        "kernel_task_mark_state(TASK_FAST_ID, KERNEL_TASK_STATE_WAITING)",
        "kernel_task_mark_state(TASK_SLOW_ID, KERNEL_TASK_STATE_WAITING)",
        "kernel_task_mark_state(TASK_LONG_ID, KERNEL_TASK_STATE_WAITING)",
    ):
        assert marker in app


def test_runtime_v12_netboot_gates_and_shell_probes_exist() -> None:
    net_iterate = read_repo("net-iterate.sh")
    doctor = read_repo("netboot-doctor.sh")

    for source in (net_iterate, doctor):
        assert "runtime v12: kernel object table + task registry" in source
        assert COMMANDS_V12 in source

    for marker in (
        "probe shell: kobjects",
        "probe shell: tasks2",
        "^kobjects count=.* active=",
        "^tasks2 count=.* task index=.*fast",
    ):
        assert marker in net_iterate

import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]

COMMANDS_V19 = (
    "commands=help,protocol,status,heap,queues,tasks,tasks2,kobjects,drivers,drivercheck,mailboxes,sendtest,"
    "supervisor,health,capcheck,events,runtime,agent,certificate,sched,sched2,cores,locks,runqueues,diag,irqs,timers,memcheck,faults,retained,"
    "retained-clear,memmap,mmu,pools,poolcheck,heapfrag,poolstats,frames,heapcheck,framecheck,stress,frameprobe,"
    "bootcert,canceltest,taskcheck,channeltest,bootcheck,soak,heap-invalid-free-test,"
    "heap-double-free-test,panic-test,fault-test,reboot"
)


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_support_declares_runtime_v19_task_spawn_metadata_api() -> None:
    support = read_repo("Sources/Support/include/Support.h")

    for symbol in (
        "KERNEL_TASK_ROOT_PARENT",
        "kernel_task_register_with_parent",
        "kernel_task_set_parent",
        "kernel_task_parent_id",
        "kernel_task_handle",
        "kernel_task_record_spawn",
        "kernel_task_record_completion",
        "kernel_task_spawn_count",
        "kernel_task_completion_count",
    ):
        assert symbol in support


def test_aether_task_swift_wrapper_exists_and_owns_task_spawn() -> None:
    wrapper = read_repo("Sources/Application/AetherTask.swift")
    app = read_repo("Sources/Application/Application.swift")

    for marker in (
        "func registerAetherTask(",
        "func spawnAetherTask(",
        "@escaping @Sendable () async -> Void",
        "kernel_task_register_with_parent",
        "kernel_task_record_spawn",
        "kernel_task_record_completion",
        "Task {",
    ):
        assert marker in wrapper

    for marker in (
        'spawnAetherTask(TASK_FAST_ID',
        'spawnAetherTask(TASK_SLOW_ID',
        'spawnAetherTask(TASK_LONG_ID',
        'spawnAetherTask(TASK_MAIL_TX_ID',
        'spawnAetherTask(TASK_MAIL_RX_ID',
        'spawnAetherTask(TASK_SHELL_ID',
    ):
        assert marker in app

    assert "Task { await fastHeartbeat() }" not in app


def test_runtime_v19_boot_marker_and_task_event_are_wired() -> None:
    app = read_repo("Sources/Application/Application.swift")

    assert "runtime v19: structured aether task spawn" in app
    assert "kernel_event_emit(KERNEL_EVENT_KIND_BOOT, 34, 0, 0)" in app
    assert "kernel_event_emit(KERNEL_EVENT_KIND_TASK, 19" in app


def test_uart_shell_v19_taskcheck_and_tasks2_metadata_exist() -> None:
    shell = read_repo("Sources/Application/UARTShell.swift")

    for marker in (
        COMMANDS_V19,
        "func printTaskcheck()",
        "taskcheck ok=",
        " spawns=",
        " completions=",
        " root_parent=",
        " parent=",
        " handle=",
        "kernel_task_spawn_count(",
        "kernel_task_completion_count(",
        "kernel_task_parent_id(",
        "kernel_task_handle(",
        'shellBufferSliceEquals(commandStart, commandLen, "taskcheck")',
    ):
        assert marker in shell


def test_runtime_v19_bootcert_includes_task_spawn_health() -> None:
    shell = read_repo("Sources/Application/UARTShell.swift")
    net_iterate = read_repo("net-iterate.sh")

    assert " version=29" in shell
    assert " taskspawns=" in shell
    assert "^bootcert ok=1 version=34 .*preemptive=1 .*smp_scheduler=1 .*atomics=1 .*locks=1 .*queues=1 .*smp=1 .*scheduler=1 .*certificate=1 .*agent=1 .*runtime=1 .*events_lost=0" in net_iterate


def test_runtime_v19_netboot_gates_and_shell_probe_exist() -> None:
    net_iterate = read_repo("net-iterate.sh")
    doctor = read_repo("netboot-doctor.sh")

    for source in (net_iterate, doctor):
        assert "runtime v19: structured aether task spawn" in source
        assert COMMANDS_V19 in source

    for marker in (
        "probe shell: taskcheck",
        "^taskcheck ok=1 .*spawns=",
        "stale pre-V34 SD fallback",
    ):
        assert marker in net_iterate

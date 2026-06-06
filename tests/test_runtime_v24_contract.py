import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]

COMMANDS_V24 = (
    "commands=help,protocol,status,heap,queues,tasks,tasks2,kobjects,drivers,drivercheck,"
    "mailboxes,sendtest,supervisor,health,capcheck,events,runtime,agent,certificate,sched,sched2,sched3,cores,locks,runqueues,diag,irqs,timers,"
    "memcheck,faults,retained,retained-clear,memmap,mmu,pools,poolcheck,"
    "heapfrag,poolstats,frames,heapcheck,framecheck,stress,frameprobe,bootcert,"
    "canceltest,taskcheck,channeltest,bootcheck,soak,heap-invalid-free-test,"
    "heap-double-free-test,panic-test,fault-test,reboot"
)


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_runtime_v24_driver_registry_api_exists() -> None:
    support = read_repo("Sources/Support/include/Support.h")
    driver = read_repo("Sources/Support/kernel_driver.c")

    for marker in (
        "KERNEL_DRIVER_ID_UART0",
        "KERNEL_DRIVER_ID_CNTP",
        "KERNEL_DRIVER_ID_GIC",
        "KERNEL_DRIVER_ID_WATCHDOG",
        "KERNEL_DRIVER_STATE_READY",
        "kernel_driver_registry_init",
        "kernel_driver_count",
        "kernel_driver_capacity",
        "kernel_driver_object_id",
        "kernel_driver_handle",
        "kernel_driver_name_len",
        "kernel_driver_name_byte",
        "kernel_driver_state",
        "kernel_driver_intid",
        "kernel_driver_base",
        "kernel_driver_caps",
        "kernel_driver_irq_count",
        "kernel_driver_error_count",
        "kernel_driver_operation_count",
        "kernel_driver_registry_selftest",
    ):
        assert marker in support

    for marker in (
        "Runtime V24 fixed driver registry",
        "#define KERNEL_DRIVER_CAPACITY_VALUE 4U",
        "driver_register_unsafe(KERNEL_DRIVER_ID_UART0",
        "driver_register_unsafe(KERNEL_DRIVER_ID_CNTP",
        "driver_register_unsafe(KERNEL_DRIVER_ID_GIC",
        "driver_register_unsafe(KERNEL_DRIVER_ID_WATCHDOG",
        "kernel_object_register(KERNEL_OBJECT_KIND_DRIVER",
        "kernel_driver_registry_selftest",
        "irq_save()",
    ):
        assert marker in driver


def test_runtime_v24_watchdog_stats_api_exists() -> None:
    support = read_repo("Sources/Support/include/Support.h")
    watchdog = read_repo("Sources/Support/watchdog.c")

    for marker in (
        "watchdog_reset_count",
        "watchdog_arm_count",
        "watchdog_pet_count",
        "watchdog_disable_count",
    ):
        assert marker in support
        assert marker in watchdog


def test_runtime_v24_application_shell_and_bootcert_surface_exist() -> None:
    app = read_repo("Sources/Application/Application.swift")
    shell = read_repo("Sources/Application/UARTShell.swift")

    for marker in (
        "Runtime V24 adds a minimal fixed driver registry.",
        "kernel_driver_registry_init()",
        "kernel_event_emit(KERNEL_EVENT_KIND_BOOT, 35, 0, 0)",
        "runtime v24: fixed driver registry",
        "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 24, UInt(kernel_driver_registry_selftest()), UInt(kernel_driver_count()))",
    ):
        assert marker in app

    for marker in (
        "V24 adds a fixed driver registry",
        COMMANDS_V24,
        "func printDrivers()",
        "func printDrivercheck()",
        "drivers count=",
        " driver index=",
        " name=",
        " state=ready",
        " intid=",
        " base=",
        " irq_count=",
        " errors=",
        " ops=",
        "drivercheck ok=",
        " uart_irq=",
        " timer_irq=",
        " gic_total=",
        " watchdog_resets=",
        'shellBufferSliceEquals(commandStart, commandLen, "drivers")',
        'shellBufferSliceEquals(commandStart, commandLen, "drivercheck")',
        " version=29",
        " drivers=",
    ):
        assert marker in shell


def test_runtime_v24_netboot_gates_and_probes_exist() -> None:
    net_iterate = read_repo("net-iterate.sh")
    doctor = read_repo("netboot-doctor.sh")

    for source in (net_iterate, doctor):
        assert "runtime v24: fixed driver registry" in source
        assert COMMANDS_V24 in source

    for marker in (
        "probe shell: drivercheck",
        "^drivercheck ok=1 .*uart_irq=.*timer_irq=.*watchdog_resets=",
        "probe shell: drivers",
        "^drivers count=4 capacity=4 selftest=1",
        "^bootcert ok=1 version=35 .*secondary_workers=1 .*preemptive=1 .*smp_scheduler=1 .*atomics=1 .*locks=1 .*queues=1 .*smp=1 .*scheduler=1 .*certificate=1 .*agent=1 .*runtime=1 .*events_lost=0",
        "stale pre-V35 SD fallback",
    ):
        assert marker in net_iterate


def test_runtime_v24_docs_are_updated_after_hardware_proof() -> None:
    readme = read_repo("README.md")
    runbook = read_repo("RUNBOOK.md")
    design = read_repo("CONCURRENCY_DESIGN.md")

    for source in (readme, runbook, design):
        assert "Runtime V24 fixed driver registry" in source
        assert "drivercheck ok=1" in source
        assert "drivers count=4 capacity=4 selftest=1" in source
        assert "bootcert ok=1 version=29" in source

import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]


COMMANDS_V33 = (
    "commands=help,protocol,status,heap,queues,tasks,tasks2,kobjects,drivers,drivercheck,"
    "mailboxes,sendtest,supervisor,health,capcheck,events,runtime,agent,certificate,sched,sched2,sched3,sched4,sched5,cores,locks,runqueues,diag,irqs,timers,"
    "memcheck,faults,retained,retained-clear,memmap,mmu,pools,poolcheck,"
    "heapfrag,poolstats,frames,heapcheck,framecheck,stress,frameprobe,bootcert,"
    "canceltest,taskcheck,channeltest,bootcheck,soak,heap-invalid-free-test,"
    "heap-double-free-test,panic-test,fault-test,reboot"
)


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_runtime_v33_atomic_spinlock_c_substrate_contract_exists() -> None:
    support = read_repo("Sources/Support/include/Support.h")
    source_path = ROOT / "Sources/Support/kernel_lock.c"

    assert source_path.exists(), "Sources/Support/kernel_lock.c missing"
    source = source_path.read_text()

    for marker in (
        "Runtime V33 atomic and spinlock substrate",
        "#define KERNEL_ATOMIC_VERSION 33U",
        "#define KERNEL_LOCK_VERSION 33U",
        "typedef struct kernel_spinlock {",
        "} kernel_spinlock_t;",
        "void kernel_atomic_full_barrier(void);",
        "unsigned int kernel_atomic_load_u32(unsigned int *ptr);",
        "void kernel_atomic_store_u32(unsigned int *ptr, unsigned int value);",
        "unsigned int kernel_atomic_fetch_add_u32(unsigned int *ptr, unsigned int value);",
        "unsigned long kernel_atomic_fetch_add_u64(unsigned long *ptr, unsigned long value);",
        "unsigned int kernel_atomic_compare_exchange_u32(unsigned int *ptr, unsigned int expected, unsigned int desired);",
        "int kernel_atomic_selftest(void);",
        "void kernel_spinlock_init(kernel_spinlock_t *lock);",
        "unsigned int kernel_spinlock_try_lock(kernel_spinlock_t *lock);",
        "void kernel_spinlock_lock(kernel_spinlock_t *lock);",
        "void kernel_spinlock_unlock(kernel_spinlock_t *lock);",
        "unsigned long kernel_spinlock_acquisition_count(const kernel_spinlock_t *lock);",
        "unsigned long kernel_spinlock_contention_count(const kernel_spinlock_t *lock);",
        "int kernel_spinlock_selftest(void);",
    ):
        assert marker in support

    for marker in (
        "Runtime V33 atomic and spinlock substrate",
        "__atomic_compare_exchange_n",
        "__atomic_fetch_add",
        "__ATOMIC_ACQUIRE",
        "__ATOMIC_RELEASE",
        "__ATOMIC_SEQ_CST",
        "kernel_spinlock_lock",
        "kernel_spinlock_selftest",
    ):
        assert marker in source


def test_runtime_v33_scheduler_per_core_runqueue_contract_exists() -> None:
    support = read_repo("Sources/Support/include/Support.h")
    scheduler = read_repo("Sources/Support/kernel_scheduler.c")

    for marker in (
        "Runtime V34 timer-driven SMP scheduler dispatch",
        "#define KERNEL_SCHEDULER_VERSION 37U",
        "#define KERNEL_SCHEDULER_CORE_CAPACITY 4U",
        "#define KERNEL_SCHEDULER_RUNQUEUE_CAPACITY 8U",
        "int kernel_scheduler_enqueue(unsigned int core_id, unsigned int token);",
        "int kernel_scheduler_dequeue(unsigned int core_id, unsigned int *out_token);",
        "unsigned int kernel_scheduler_runqueue_head(unsigned int core_id);",
        "unsigned int kernel_scheduler_runqueue_tail(unsigned int core_id);",
        "int kernel_scheduler_runqueue_selftest(void);",
    ):
        assert marker in support

    for marker in (
        "Runtime V34 timer-driven SMP scheduler dispatch",
        "kernel_spinlock_t lock;",
        "kernel_spinlock_init(&cores[i].lock);",
        "kernel_spinlock_lock(&cores[core_id].lock);",
        "kernel_spinlock_unlock(&cores[core_id].lock);",
        "kernel_scheduler_enqueue",
        "kernel_scheduler_dequeue",
        "kernel_scheduler_runqueue_selftest",
        "0x33U",
    ):
        assert marker in scheduler


def test_runtime_v33_application_boot_marker_and_selftest_exist() -> None:
    app = read_repo("Sources/Application/Application.swift")

    for marker in (
        "Runtime V33 adds atomics, spinlocks, and per-core run queues.",
        "kernel_event_emit(KERNEL_EVENT_KIND_BOOT, 37, 0, 0)",
        "runtime v33: atomics spinlocks per-core run queues",
        "let runtimeV33 = kernel_atomic_selftest() != 0 && kernel_spinlock_selftest() != 0 && kernel_scheduler_runqueue_selftest() != 0 ? 1 : 0",
        "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 33, UInt(runtimeV33), UInt(kernel_scheduler_core_count()))",
    ):
        assert marker in app


def test_runtime_v33_shell_locks_runqueues_and_certificate_surface_exist() -> None:
    shell = read_repo("Sources/Application/UARTShell.swift")

    for marker in (
        COMMANDS_V33,
        "func printLocks()",
        "locks ok=",
        " version=33",
        " atomics=",
        " spinlocks=",
        " acquisitions=",
        " contentions=",
        " selftest=",
        "func printRunQueues()",
        "runqueues ok=",
        " cores=",
        " capacity=",
        " total=",
        " core0=",
        " core1=",
        " core2=",
        " core3=",
        " enqueues0=",
        " dequeues0=",
        'shellBufferSliceEquals(commandStart, commandLen, "locks")',
        'shellBufferSliceEquals(commandStart, commandLen, "runqueues")',
    ):
        assert marker in shell

    for marker in (
        "let atomics = kernel_atomic_selftest()",
        "let locks = kernel_spinlock_selftest()",
        "let queues = kernel_scheduler_runqueue_selftest()",
        "version=34",
        " atomics=",
        " locks=",
        " queues=",
        "atomics != 0 && locks != 0 && queues != 0",
        "printLocks()",
        "printRunQueues()",
    ):
        assert marker in shell


def test_runtime_v33_netboot_gates_and_probes_exist() -> None:
    net_iterate = read_repo("net-iterate.sh")
    doctor = read_repo("netboot-doctor.sh")

    for source in (net_iterate, doctor):
        assert "runtime v33: atomics spinlocks per-core run queues" in source
        assert COMMANDS_V33 in source

    for marker in (
        "probe shell: locks",
        "^locks ok=1 version=33 .*atomics=1 .*spinlocks=1 .*selftest=1",
        "probe shell: runqueues",
        "^runqueues ok=1 version=33 .*cores=4 .*capacity=[1-9][0-9]* .*total=0 .*core0=0 .*core1=0 .*core2=0 .*core3=0 .*selftest=1",
        "probe shell: req-locks",
        "^resp id=33 ok=1 cmd=locks end",
        "probe shell: req-runqueues",
        "^resp id=34 ok=1 cmd=runqueues end",
        "^bootcert ok=1 version=37 .*job_exec=1 .*worker_feed=1 .*secondary_workers=1 .*preemptive=1 .*smp_scheduler=1 .*atomics=1 .*locks=1 .*queues=1 .*smp=1 .*scheduler=1 .*certificate=1 .*agent=1 .*runtime=1 .*events_lost=0",
        "^certificate ok=1 version=37 substrate=1 .*bootcert=1 .*job_exec=1 .*worker_feed=1 .*secondary_workers=1 .*preemptive=1 .*smp_scheduler=1 .*atomics=1 .*locks=1 .*queues=1 .*smp=1 .*scheduler=1 .*agent=1 .*runtime=1 .*memory=1 .*objects=1 .*tasks=1 .*mailboxes=1 .*supervisor=1 .*handles=1 .*events=1 .*cancellations=1 .*channels=1 .*drivers=1 .*pressure=1 .*pools=1 .*mmu=1 .*events_lost=0",
        "stale pre-V37 SD fallback",
    ):
        assert marker in net_iterate


def test_runtime_v33_docs_are_updated_after_hardware_proof() -> None:
    readme = read_repo("README.md")
    runbook = read_repo("RUNBOOK.md")
    design = read_repo("CONCURRENCY_DESIGN.md")

    for source in (readme, runbook, design):
        assert "Runtime V33 atomics, spinlocks, and per-core run queues" in source
        assert "bootcert ok=1 version=33" in source
        assert "atomics=1" in source
        assert "locks ok=1 version=33" in source
        assert "runqueues ok=1 version=33" in source

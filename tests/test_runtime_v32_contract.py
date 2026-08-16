import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]


COMMANDS_V32 = (
    "commands=help,protocol,status,heap,queues,tasks,tasks2,kobjects,drivers,drivercheck,"
    "mailboxes,sendtest,supervisor,health,capcheck,events,runtime,agent,certificate,sched,sched2,sched3,sched4,sched5,sched6,sched7,sched8,sched9,sched10,sched11,sched12,cores,locks,runqueues,diag,irqs,timers,"
    "memcheck,faults,retained,retained-clear,memmap,mmu,pools,poolcheck,"
    "heapfrag,poolstats,frames,heapcheck,framecheck,stress,frameprobe,bootcert,"
    "canceltest,taskcheck,channeltest,bootcheck,soak,heap-invalid-free-test,"
    "heap-double-free-test,panic-test,fault-test,reboot"
)


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_runtime_v32_smp_c_substrate_contract_exists() -> None:
    support = read_repo("Sources/Support/include/Support.h")
    source_path = ROOT / "Sources/Support/kernel_smp.c"

    assert source_path.exists(), "Sources/Support/kernel_smp.c missing"
    source = source_path.read_text()

    for marker in (
        "Runtime V32 SMP secondary-core bring-up substrate",
        "#define KERNEL_SMP_VERSION 32U",
        "#define KERNEL_SMP_CORE_CAPACITY 4U",
        "#define KERNEL_SMP_SECONDARY_MASK 0xeU",
        "#define KERNEL_SMP_STACK_BYTES",
        "void kernel_smp_init(void);",
        "void kernel_smp_note_primary(unsigned long mpidr);",
        "void kernel_smp_secondary_entry(unsigned int core_id, unsigned long mpidr);",
        "unsigned int kernel_smp_core_capacity(void);",
        "unsigned int kernel_smp_online_count(void);",
        "unsigned int kernel_smp_online_mask(void);",
        "unsigned int kernel_smp_primary_core_id(void);",
        "unsigned int kernel_smp_core_online(unsigned int core_id);",
        "unsigned long kernel_smp_core_mpidr(unsigned int core_id);",
        "unsigned long kernel_smp_core_entry_count(unsigned int core_id);",
        "unsigned long kernel_smp_core_heartbeat(unsigned int core_id);",
        "unsigned int kernel_smp_release_map(void);",
        "int kernel_smp_selftest(void);",
    ):
        assert marker in support

    for marker in (
        "Runtime V32 SMP secondary-core bring-up substrate",
        "KERNEL_SMP_SECONDARY_MASK",
        "kernel_smp_secondary_entry",
        "kernel_smp_core_heartbeat",
    ):
        assert marker in source


def test_runtime_v32_boot_assembly_releases_secondaries_to_c_substrate() -> None:
    boot = read_repo("Sources/Support/boot.S")

    for marker in (
        "Runtime V32 secondary-core release",
        ".global _kernel_smp_core_stacks",
        "ARMSTUB_SPIN_CPU1",
        "ARMSTUB_SPIN_CPU2",
        "ARMSTUB_SPIN_CPU3",
        "KERNEL_SMP_STACK_BYTES",
        "secondary_el1_entry:",
        "secondary_halt:",
        "bl      _kernel_smp_init",
        "bl      _kernel_smp_note_primary",
        "bl      _kernel_smp_secondary_entry",
        "_kernel_smp_core_stacks:",
    ):
        assert marker in boot

    assert "park secondaries" not in boot.lower()


def test_runtime_v32_application_boot_marker_and_selftest_exist() -> None:
    app = read_repo("Sources/Application/Application.swift")

    for marker in (
        "Runtime V32 adds SMP secondary-core bring-up accounting.",
        "kernel_event_emit(KERNEL_EVENT_KIND_BOOT, 64, 0, 0)",
        "runtime v32: smp secondary-core bring-up",
        "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 32, UInt(kernel_smp_selftest()), UInt(kernel_smp_online_count()))",
    ):
        assert marker in app


def test_runtime_v32_shell_cores_and_certificate_surface_exist() -> None:
    shell = read_repo("Sources/Application/UARTShell.swift")

    for marker in (
        COMMANDS_V32,
        "func printCores()",
        "cores ok=",
        " version=32",
        " capacity=",
        " online=",
        " mask=",
        " primary=",
        " release=",
        " selftest=",
        " core0=",
        " core1=",
        " core2=",
        " core3=",
        " heartbeat0=",
        " heartbeat1=",
        " heartbeat2=",
        " heartbeat3=",
        'shellBufferSliceEquals(commandStart, commandLen, "cores")',
    ):
        assert marker in shell

    for marker in (
        "let smp = kernel_smp_proven()",
        "version=34",
        " smp=",
        "smp != 0",
        "printCores()",
    ):
        assert marker in shell


def test_runtime_v32_netboot_gates_and_cores_probe_exist() -> None:
    net_iterate = read_repo("scripts/netboot/net-iterate.sh")
    doctor = read_repo("scripts/netboot/netboot-doctor.sh")

    for source in (net_iterate, doctor):
        assert "runtime v32: smp secondary-core bring-up" in source
        assert COMMANDS_V32 in source

    for marker in (
        "probe shell: cores",
        "^cores ok=1 version=32 .*capacity=4 .*online=4 .*mask=0xf .*primary=0 .*release=0xe .*selftest=1 .*core0=1 .*core1=1 .*core2=1 .*core3=1",
        "probe shell: req-cores",
        "^resp id=32 ok=1 cmd=cores end",
        "^bootcert ok=1 version=66 .*concurrency=1 .*priority=1 .*fairness=1 .*stealing=[01] .*backpressure=[01] .*handoff=[01] .*wake=[01] .*job_exec=[01] .*worker_feed=[01] .*secondary_workers=[01] .*preemptive=[01] .*smp_scheduler=[01] .*atomics=1 .*locks=1 .*queues=1 .*smp=1 .*scheduler=1 .*certificate=1 .*agent=1 .*runtime=1 .*syscall=1 .*uaccess=1 .*usermode=1 .*process=1 .*loader=1 .*multiprocess=1 .*sdhci=1 .*card=1 .*block=1 .*fat32=1 .*mailbox=1 .*framebuf=1 .*console=1 .*pcie=1 .*vl805=1 .*xhci=1 .*kbd=[01] .*swift=6.3.2 .*events_lost=0",
        "^certificate ok=1 version=63 substrate=1 .*bootcert=[01] .*concurrency=1 .*priority=1 .*fairness=1 .*stealing=[01] .*backpressure=[01] .*handoff=[01] .*wake=[01] .*job_exec=[01] .*worker_feed=[01] .*secondary_workers=[01] .*preemptive=[01] .*smp_scheduler=[01] .*atomics=1 .*locks=1 .*queues=1 .*smp=1 .*scheduler=1 .*agent=1 .*runtime=1 .*memory=1 .*objects=1 .*tasks=1 .*mailboxes=1 .*supervisor=1 .*handles=1 .*events=[0-9]+ .*cancellations=1 .*channels=1 .*drivers=1 .*pressure=1 .*pools=1 .*mmu=1 .*vmm=1 .*asplit=1 .*el0=1 .*syscall=1 .*uaccess=1 .*usermode=1 .*process=1 .*loader=1 .*multiprocess=1 .*sdhci=1 .*card=1 .*block=1 .*fat32=1 .*mailbox=1 .*framebuf=1 .*console=1 .*pcie=1 .*vl805=1 .*xhci=1 .*swift=6.3.2 .*events_lost=0",
        "stale pre-V43 SD fallback",
    ):
        assert marker in net_iterate


def test_runtime_v32_docs_are_updated_after_hardware_proof() -> None:
    readme = read_repo("README.md")
    runbook = read_repo("docs/RUNBOOK.md")
    design = read_repo("docs/CONCURRENCY_DESIGN.md")

    for source in (readme, runbook, design):
        assert "Runtime V32 SMP secondary-core bring-up" in source
        assert "bootcert ok=1 version=32" in source
        assert "smp=1" in source
        assert "cores ok=1 version=32" in source

import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]

COMMANDS_V21 = (
    "commands=help,protocol,status,heap,queues,tasks,tasks2,kobjects,drivers,drivercheck,mailboxes,sendtest,"
    "supervisor,health,capcheck,events,runtime,agent,certificate,sched,sched2,sched3,sched4,sched5,sched6,sched7,sched8,sched9,sched10,cores,locks,runqueues,diag,irqs,timers,memcheck,faults,retained,"
    "retained-clear,memmap,mmu,pools,poolcheck,heapfrag,poolstats,frames,heapcheck,framecheck,stress,frameprobe,"
    "bootcert,canceltest,taskcheck,channeltest,bootcheck,soak,heap-invalid-free-test,"
    "heap-double-free-test,panic-test,fault-test,reboot"
)


def read_repo(path: str) -> str:
    return (ROOT / path).read_text()


def test_runtime_v21_mmu_ownership_note_is_primary_sourced() -> None:
    note = read_repo("MMU_OWNERSHIP.md")

    for marker in (
        "Runtime V21",
        "EL1 stage-1",
        "identity map",
        "TTBR0_EL1",
        "TCR_EL1",
        "MAIR_EL1",
        "TLBI",
        "Low Peripheral",
        "0x0_FC00_0000",
        "0x0_FF80_0000",
        "no dynamic remap in V21",
        "https://developer.arm.com/-/media/Arm%20Developer%20Community/PDF/Learn%20the%20Architecture/LearnTheArchitecture-MemoryManagement-101811_0100_00_en.pdf",
        "https://datasheets.raspberrypi.com/bcm2711/bcm2711-peripherals.pdf",
    ):
        assert marker in note


def test_runtime_v21_mmu_readonly_support_api_exists() -> None:
    support = read_repo("Sources/Support/include/Support.h")
    mmu = read_repo("Sources/Support/mmu.c")

    for marker in (
        "KERNEL_MMU_REGION_KIND_NORMAL",
        "KERNEL_MMU_REGION_KIND_DEVICE",
        "KERNEL_MMU_REGION_KIND_FAULT",
        "kernel_mmu_l1_entry_count",
        "kernel_mmu_block_size",
        "kernel_mmu_region_count",
        "kernel_mmu_region_va_base",
        "kernel_mmu_region_pa_base",
        "kernel_mmu_region_size",
        "kernel_mmu_region_kind",
        "kernel_mmu_tcr_value",
        "kernel_mmu_mair_value",
        "kernel_mmu_selftest",
    ):
        assert marker in support

    for marker in (
        "#define KERNEL_MMU_L1_ENTRY_COUNT 512U",
        "#define KERNEL_MMU_BLOCK_SIZE 0x40000000UL",
        "typedef struct kernel_mmu_region",
        "static const kernel_mmu_region mmu_regions",
        "KERNEL_MMU_REGION_KIND_NORMAL",
        "KERNEL_MMU_REGION_KIND_DEVICE",
        "KERNEL_MMU_REGION_KIND_FAULT",
        "kernel_mmu_selftest",
    ):
        assert marker in mmu

    assert "kernel_mmu_map_page" not in support
    assert "kernel_mmu_map_page" not in mmu


def test_runtime_v21_application_shell_and_bootcert_surface_exist() -> None:
    app = read_repo("Sources/Application/Application.swift")
    shell = read_repo("Sources/Application/UARTShell.swift")

    assert "runtime v21: mmu ownership boundary" in app
    assert "kernel_event_emit(KERNEL_EVENT_KIND_BOOT, 42, 0, 0)" in app
    assert "kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 21, UInt(kernel_mmu_selftest()), 0)" in app

    for marker in (
        COMMANDS_V21,
        "func printMMU()",
        "mmu ok=",
        " regions=",
        " block_size=",
        " tcr=",
        " mair=",
        " region index=",
        " va=",
        " pa=",
        " size=",
        " kind=",
        "kernel_mmu_selftest()",
        'shellBufferSliceEquals(commandStart, commandLen, "mmu")',
        " version=40",
        " mmu=",
    ):
        assert marker in shell


def test_runtime_v21_netboot_gates_and_probe_exist() -> None:
    net_iterate = read_repo("net-iterate.sh")
    doctor = read_repo("netboot-doctor.sh")

    for source in (net_iterate, doctor):
        assert "runtime v21: mmu ownership boundary" in source
        assert COMMANDS_V21 in source

    for marker in (
        "probe shell: mmu",
        "^mmu ok=1 .*regions=4 .*block_size=0x40000000",
        "^bootcert ok=1 version=42 .*fairness=1 .*stealing=1 .*backpressure=1 .*handoff=1 .*wake=1 .*job_exec=1 .*worker_feed=1 .*secondary_workers=1 .*preemptive=1 .*smp_scheduler=1 .*atomics=1 .*locks=1 .*queues=1 .*smp=1 .*scheduler=1 .*certificate=1 .*agent=1 .*runtime=1 .*events_lost=0",
        "stale pre-V41 SD fallback",
    ):
        assert marker in net_iterate

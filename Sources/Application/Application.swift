//===----------------------------------------------------------------------===//
// AetherKernel entry point.
//
// boot.S releases secondary cores into a fixed C-only SMP accounting loop, drops
// EL2->EL1, enables FP/SIMD (CPACR) and the MMU (Normal cacheable RAM, required
// for the concurrency runtime's atomics), sets the core-0 stack + EL1 vectors,
// and `bl _main` into this @main. From here we run real Swift async/await tasks
// on a custom cooperative executor.
//===----------------------------------------------------------------------===//
import Support
import _Concurrency

nonisolated(unsafe) var runtimeFastCount: UInt64 = 0
nonisolated(unsafe) var runtimeSlowCount: UInt64 = 0
nonisolated(unsafe) var runtimeLongCount: UInt64 = 0
nonisolated(unsafe) var runtimeMailboxSent: UInt64 = 0
nonisolated(unsafe) var runtimeMailboxReceived: UInt64 = 0

let TASK_FAST_ID: UInt32 = 0
let TASK_SLOW_ID: UInt32 = 1
let TASK_LONG_ID: UInt32 = 2
let TASK_SHELL_ID: UInt32 = 3
let TASK_MAIL_TX_ID: UInt32 = 4
let TASK_MAIL_RX_ID: UInt32 = 5
let TASK_CANCEL_ID: UInt32 = 6

let MAILBOX_DEMO_ID: UInt32 = 0
let MAILBOX_SELFTEST_ID: UInt32 = 1

func registerKernelTask(_ taskID: UInt32, _ name: StaticString, _ periodMS: UInt32) {
  _ = kernel_task_register(taskID, name.utf8Start, UInt32(name.utf8CodeUnitCount), periodMS)
}

func registerKernelMailbox(_ mailboxID: UInt32, _ name: StaticString) {
  _ = kernel_mailbox_register(mailboxID, name.utf8Start, UInt32(name.utf8CodeUnitCount))
}

@main
struct Application {
  static func fastHeartbeat() async {
    var n: UInt64 = 0
    while true {
      kernel_task_mark_state(TASK_FAST_ID, KERNEL_TASK_STATE_RUNNING)
      runtimeFastCount = n
      kernel_task_record_tick(TASK_FAST_ID)
      kernel_supervisor_heartbeat(TASK_FAST_ID)
      kernel_supervisor_check()
      if n == 0 {
        kernel_event_emit(KERNEL_EVENT_KIND_TASK, UInt(TASK_FAST_ID), UInt(n), 0)
        kernel_event_emit(KERNEL_EVENT_KIND_TIMER, UInt(TASK_FAST_ID), UInt(timerFrequency()), 0)
      }
      uartPuts("rtv2 fast ")
      uartPutHex(n)
      uartPuts("\n")
      kernel_task_mark_state(TASK_FAST_ID, KERNEL_TASK_STATE_WAITING)
      await timerSleepMillis(250)
      uartPuts("rtv2 fast woke ")
      uartPutHex(n)
      uartPuts("\n")
      n &+= 1
    }
  }

  static func slowHeartbeat() async {
    var n: UInt64 = 0
    while true {
      kernel_task_mark_state(TASK_SLOW_ID, KERNEL_TASK_STATE_RUNNING)
      runtimeSlowCount = n
      kernel_task_record_tick(TASK_SLOW_ID)
      kernel_supervisor_heartbeat(TASK_SLOW_ID)
      if n == 0 {
        kernel_event_emit(KERNEL_EVENT_KIND_TASK, UInt(TASK_SLOW_ID), UInt(n), 0)
      }
      uartPuts("rtv2 slow ")
      uartPutHex(n)
      uartPuts("\n")
      kernel_task_mark_state(TASK_SLOW_ID, KERNEL_TASK_STATE_WAITING)
      await timerSleepSeconds(1)
      uartPuts("rtv2 slow woke ")
      uartPutHex(n)
      uartPuts("\n")
      n &+= 1
    }
  }

  static func longHeartbeat() async {
    var n: UInt64 = 0
    while true {
      kernel_task_mark_state(TASK_LONG_ID, KERNEL_TASK_STATE_RUNNING)
      runtimeLongCount = n
      kernel_task_record_tick(TASK_LONG_ID)
      kernel_supervisor_heartbeat(TASK_LONG_ID)
      if n == 0 {
        kernel_event_emit(KERNEL_EVENT_KIND_TASK, UInt(TASK_LONG_ID), UInt(n), 0)
      }
      uartPuts("rtv2 long ")
      uartPutHex(n)
      uartPuts("\n")
      kernel_task_mark_state(TASK_LONG_ID, KERNEL_TASK_STATE_WAITING)
      await timerSleepSeconds(2)
      uartPuts("rtv2 long woke ")
      uartPutHex(n)
      uartPuts("\n")
      n &+= 1
    }
  }

  static func registerRuntimeTasks() {
    registerAetherTask(TASK_FAST_ID, "fast", 250, KERNEL_TASK_ROOT_PARENT, 1000)
    registerAetherTask(TASK_SLOW_ID, "slow", 1000, KERNEL_TASK_ROOT_PARENT, 3000)
    registerAetherTask(TASK_LONG_ID, "long", 2000, KERNEL_TASK_ROOT_PARENT, 5000)
    registerAetherTask(TASK_SHELL_ID, "shell", 0, KERNEL_TASK_ROOT_PARENT, 0)
    registerAetherTask(TASK_MAIL_TX_ID, "mail-tx", 750, KERNEL_TASK_ROOT_PARENT, 3000)
    registerAetherTask(TASK_MAIL_RX_ID, "mail-rx", 0, KERNEL_TASK_ROOT_PARENT, 3000)
    registerAetherTask(TASK_CANCEL_ID, "cancel", 0, KERNEL_TASK_ROOT_PARENT, 0)
  }

  static func registerRuntimeMailboxes() {
    registerKernelMailbox(MAILBOX_DEMO_ID, "demo")
    registerKernelMailbox(MAILBOX_SELFTEST_ID, "selftest")
  }

  static func registerRuntimeSupervisor() {
    kernel_event_emit(KERNEL_EVENT_KIND_SUPERVISOR, UInt(kernel_supervisor_count()), 0, 0)
  }

  static func mailboxProducer() async {
    var n: UInt64 = 0
    let channel = AetherChannelU64(mailboxID: MAILBOX_DEMO_ID)
    while true {
      kernel_task_mark_state(TASK_MAIL_TX_ID, KERNEL_TASK_STATE_RUNNING)
      if channel.send(n) {
        runtimeMailboxSent = n
        kernel_task_record_tick(TASK_MAIL_TX_ID)
        kernel_supervisor_heartbeat(TASK_MAIL_TX_ID)
        if n == 0 {
          kernel_event_emit(KERNEL_EVENT_KIND_MAILBOX, UInt(MAILBOX_DEMO_ID), UInt(n), 1)
        }
        uartPuts("rtv13 mail tx ")
        uartPutHex(n)
        uartPuts("\n")
        n &+= 1
      }
      kernel_task_mark_state(TASK_MAIL_TX_ID, KERNEL_TASK_STATE_WAITING)
      await timerSleepMillis(750)
    }
  }

  static func mailboxConsumer() async {
    let channel = AetherChannelU64(mailboxID: MAILBOX_DEMO_ID)
    while true {
      kernel_task_mark_state(TASK_MAIL_RX_ID, KERNEL_TASK_STATE_WAITING)
      let value = await channel.receive()
      kernel_task_mark_state(TASK_MAIL_RX_ID, KERNEL_TASK_STATE_RUNNING)
      runtimeMailboxReceived = value
      kernel_task_record_tick(TASK_MAIL_RX_ID)
      kernel_supervisor_heartbeat(TASK_MAIL_RX_ID)
      if value == 0 {
        kernel_event_emit(KERNEL_EVENT_KIND_MAILBOX, UInt(MAILBOX_DEMO_ID), UInt(value), 2)
      }
      uartPuts("rtv13 mail rx ")
      uartPutHex(value)
      uartPuts("\n")
    }
  }

  static func main() {
    uartInit()
    uartPuts("\n=== AetherKernel ===\n")
    uartPuts("Embedded Swift 6.3.2 - bare-metal Raspberry Pi 4B (BCM2711)\n")
    uartPuts("PL011 UART0 @ 0xFE201000 online. Hello from the metal!\n")

    // Prove which exception level the firmware dropped us into.
    // CurrentEL holds the level in bits [3:2]; EL1 reads back as 0x4.
    uartPuts("CurrentEL = "); uartPutHex(UInt64(read_currentel())); uartPuts("\n")
    uartPuts("CNTFRQ = "); uartPutHex(UInt64(timerFrequency())); uartPuts(" Hz (generic timer)\n")

    // Runtime V2: multiple Swift async tasks sleep on the same CNTP timer
    // arbiter. Runtime V4 adds IRQ-backed UART RX for the shell. Runtime V5
    // exposes diagnostics, Runtime V6 retains panic/fault records across
    // watchdog reset, Runtime V7 makes low-memory ownership explicit, Runtime
    // V8 adds allocator guardrails, Runtime V9 adds bounded pressure tests,
    // Runtime V10 adds explicit guard probes, and Runtime V11 adds boot/soak
    // invariant checks. Runtime V12 adds fixed kernel object/task registries.
    // Runtime V13 adds bounded mailbox message queues. Runtime V14 adds a
    // deterministic cooperative task supervisor. Runtime V15 adds
    // capability-tagged kernel object handles. Runtime V16 adds a fixed event
    // log for kernel/agent observability. Runtime V17 adds a deterministic boot
    // certificate for host proof loops. Runtime V18 adds fixed cooperative
    // cancellation tokens. Runtime V19 adds the Aether-owned task spawn wrapper.
    // Runtime V20 adds Swift-facing async channels over fixed mailboxes. Runtime
    // V21 exposes the current MMU ownership boundary without dynamic remaps.
    // Runtime V22 adds guarded C-owned typed pools beside the heap.
    // Runtime V23 adds allocator/pool pressure telemetry.
    // Runtime V24 adds a minimal fixed driver registry.
    // Runtime V25 adds a scriptable ASCII command protocol v2.
    // Runtime V27 adds panic/fault taxonomy and symbolic retained records.
    // Runtime V28 adds a Swift runtime dependency audit.
    // Runtime V31 adds a preemptive scheduler tick substrate.
    // Runtime V32 adds SMP secondary-core bring-up accounting.
    // Runtime V33 adds atomics, spinlocks, and per-core run queues.
    // Runtime V34 adds timer-driven SMP scheduler dispatch accounting.
    // Runtime V35 adds C-only secondary scheduler workers.
    // Runtime V36 adds timer-fed secondary scheduler workers.
    // Runtime V37 adds timer-fed secondary C scheduler jobs.
    // Runtime V38 adds SEV/WFE secondary scheduler wakeups.
    // Runtime V39 adds secondary scheduler handoff acknowledgements.
    // Runtime V40 adds bounded scheduler backpressure proof.
    // Runtime V44 adds bounded SMP concurrency soak under active scheduler load.
    // Runtime V46 adds kernel/user address-space split (isolated page tables).
    // Runtime V50 adds EPIC A capstone: EL0 syscall round-trip + user fault containment.
    // Runtime V49 adds fault-safe copy_from_user / copy_to_user.
    // Runtime V48 adds syscall ABI via SVC from EL0.
    // Runtime V47 adds EL0 entry/exit and context save/restore.
    // Runtime V45 adds dynamic virtual memory (page table allocator + 4KiB map/unmap + TLB maintenance on live EL1 tables).
    // Runtime V43 adds bounded secondary scheduler priority lanes.
    // Runtime V42 adds bounded secondary scheduler load balancing.
    // Runtime V41 adds bounded secondary scheduler work stealing.
    kernel_memory_init()
    kernel_pool_init()
    kernel_cancel_init()
    kernel_event_log_init()
    kernel_scheduler_init()
    kernel_event_emit(KERNEL_EVENT_KIND_BOOT, 64, 0, 0)
    kernel_object_registry_init()
    kernel_driver_registry_init()
    kernel_task_registry_init()
    kernel_supervisor_init()
    kernel_mailbox_registry_init()
    registerRuntimeMailboxes()
    registerRuntimeTasks()
    registerRuntimeSupervisor()
    uart_rx_irq_init()
    gicInitRuntimeIRQs()
    kernel_scheduler_start(timerFrequency() / 20)
    kernel_scheduler_enable_smp_dispatch()
    kernel_scheduler_enable_secondary_workers()
    kernel_scheduler_enable_secondary_job_execution()
    kernel_scheduler_enable_secondary_wake_signals()
    kernel_scheduler_enable_secondary_handoffs()
    kernel_scheduler_enable_secondary_work_stealing()
    kernel_scheduler_enable_load_balancing()
    kernel_scheduler_enable_priority_lanes()
    kernel_scheduler_enable_timer_worker_feed()
    kernel_scheduler_enable_concurrency_soak()
    uart_rx_irq_enable()
    uartPuts("runtime v2: shared CNTP timer arbiter, multi-task async sleep\n")
    uartPuts("runtime v4: irq-backed uart shell\n")
    uartPuts("runtime v5: diagnostics shell\n")
    uartPuts("runtime v6: retained panic/fault records\n")
    uartPuts("runtime v7: memory map + frame allocator\n")
    uartPuts("runtime v8: allocator guardrails\n")
    uartPuts("runtime v9: bounded memory pressure self-tests\n")
    uartPuts("runtime v10: explicit guard probes\n")
    uartPuts("runtime v11: boot and soak invariants\n")
    uartPuts("runtime v12: kernel object table + task registry\n")
    uartPuts("runtime v13: bounded mailbox message queues\n")
    uartPuts("runtime v14: deterministic task supervisor\n")
    uartPuts("runtime v15: capability-tagged kernel handles\n")
    uartPuts("runtime v16: kernel event log ring\n")
    uartPuts("runtime v17: deterministic boot certificate\n")
    uartPuts("runtime v18: cooperative cancellation tokens\n")
    uartPuts("runtime v19: structured aether task spawn\n")
    uartPuts("runtime v20: bounded async channels\n")
    uartPuts("runtime v21: mmu ownership boundary\n")
    uartPuts("runtime v22: guarded typed pools\n")
    uartPuts("runtime v23: allocator and pool pressure telemetry\n")
    uartPuts("runtime v24: fixed driver registry\n")
    uartPuts("runtime v25: scriptable command protocol v2\n")
    uartPuts("runtime v27: panic taxonomy and symbolic retained records\n")
    uartPuts("runtime v28: swift runtime dependency audit\n")
    uartPuts("runtime v29: agent-oriented control session\n")
    uartPuts("runtime v30: swift-native kernel substrate certificate\n")
    uartPuts("runtime v31: preemptive scheduler substrate\n")
    uartPuts("runtime v32: smp secondary-core bring-up\n")
    uartPuts("runtime v33: atomics spinlocks per-core run queues\n")
    uartPuts("runtime v34: timer-driven smp scheduler dispatch\n")
    uartPuts("runtime v35: secondary-owned scheduler workers\n")
    uartPuts("runtime v36: timer-fed secondary scheduler workers\n")
    uartPuts("runtime v37: timer-fed secondary C scheduler jobs\n")
    uartPuts("runtime v38: secondary scheduler wake protocol\n")
    uartPuts("runtime v39: secondary scheduler handoff protocol\n")
    uartPuts("runtime v40: scheduler backpressure protocol\n")
    uartPuts("runtime v41: secondary scheduler work stealing\n")
    uartPuts("runtime v42: secondary scheduler load balancing\n")
    uartPuts("runtime v43: secondary scheduler priority preemption\n")
    uartPuts("runtime v44: bounded smp concurrency soak\n")
    uartPuts("runtime v45: dynamic virtual memory (page tables + TLB)\n")
    uartPuts("runtime v46: kernel/user address-space split (isolated page tables)\n")
    uartPuts("runtime v47: EL0 entry/exit and context save/restore\n")
    uartPuts("runtime v48: syscall ABI via SVC from EL0\n")
    uartPuts("runtime v49: fault-safe copy_from_user / copy_to_user\n")
    uartPuts("runtime v50: EPIC A capstone — EL0 syscall + user fault containment\n")
    uartPuts("runtime v51: process abstraction (address space + lifecycle)\n")
    uartPuts("runtime v52: user binary loader (flat blob + sys_write)\n")
    uartPuts("runtime v53: multi-process user execution (per-core EL0 + 3x isolation)\n")
    uartPuts("runtime v54: BCM2711 EMMC2/SDHCI register probe\n")
    uartPuts("runtime v55: SD card identification (CMD0/CMD8/ACMD41/CMD2/CMD3)\n")
    uartPuts("runtime v56: single block read CMD17 + MBR 0x55AA verification\n")
    uartPuts("runtime v57: FAT32 file read (config.txt bytes + checksum)\n")
    uartPuts("runtime v58: VideoCore mailbox property interface (firmware revision)\n")
    uartPuts("runtime v59: VideoCore framebuffer allocation (width/height/pitch/addr)\n")
    uartPuts("runtime v60: text console (8x8 font blit + readback proof)\n")
    // SELFTEST id 18: boot-only Runtime V18 cancel selftest snapshot (arg1=0).
    // Shell canceltest reruns emit KERNEL_EVENT_KIND_SHELL id 18 — not this slot.
    kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 18, UInt(kernel_cancel_selftest()), 0)
    kernel_event_emit(KERNEL_EVENT_KIND_TASK, 19, UInt(aetherTaskSpawnSelftest()), UInt(kernel_task_count()))
    kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 20, UInt(aetherChannelSelftest()), UInt(kernel_mailbox_count()))
    kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 21, UInt(kernel_mmu_selftest()), 0)
    let pt_ok = kernel_vmm_pt_alloc_selftest()
    let vmm_ok = kernel_vmm_vmm_selftest()
    let asplit_ok = kernel_vmm_asplit_selftest()
    let el0_ok = kernel_vmm_el0_selftest()
    kernel_vmm_boot_snapshot_seal(pt_ok, vmm_ok, asplit_ok, el0_ok)
    kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 45, UInt(pt_ok), UInt(vmm_ok))
    kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 46, UInt(asplit_ok), 0)
    let syscall_ok_boot = kernel_syscall_selftest()
    kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 48, UInt(syscall_ok_boot), UInt(kernel_syscall_abi_version()))
    let uaccess_ok_boot = kernel_uaccess_selftest()
    kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 49, UInt(uaccess_ok_boot), 0)
    kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 47, UInt(el0_ok), 0)
    uartPuts("vmmcheck ok=")
    uartPutDec(UInt64((pt_ok != 0 && vmm_ok != 0) ? 1 : 0))
    uartPuts(" pt=")
    uartPutDec(UInt64(pt_ok))
    uartPuts(" vmm=")
    uartPutDec(UInt64(vmm_ok))
    uartPuts("\n")
    uartPuts("asplit ok=")
    uartPutDec(UInt64(asplit_ok))
    uartPuts(" version=46\n")
    uartPuts("el0 ok=")
    uartPutDec(UInt64(el0_ok))
    uartPuts(" version=47\n")
    uartPuts("syscall ok=")
    uartPutDec(UInt64(syscall_ok_boot))
    uartPuts(" version=48 abi=")
    uartPutDec(UInt64(kernel_syscall_abi_version()))
    uartPuts(" table=")
    uartPutDec(UInt64(kernel_syscall_table_valid()))
    uartPuts(" dispatched=")
    uartPutDec(UInt64(kernel_syscall_last_dispatched_read()))
    uartPuts(" num=")
    uartPutDec(UInt64(kernel_syscall_last_num_read()))
    uartPuts(" ret=")
    uartPutHex(UInt64(kernel_syscall_last_ret_read()))
    uartPuts("\n")
    uartPuts("uaccess ok=")
    uartPutDec(UInt64(uaccess_ok_boot))
    uartPuts(" version=49\n")
    let usermode_ok_boot = kernel_usermode_selftest()
    let fault_contained_boot = kernel_el0_fault_contained_read()
    kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 50, UInt(usermode_ok_boot), UInt(fault_contained_boot))
    uartPuts("usermode ok=")
    uartPutDec(UInt64(usermode_ok_boot))
    uartPuts(" version=50 fault_contained=")
    uartPutDec(UInt64(fault_contained_boot))
    uartPuts("\n")
    let process_ok_boot = kernel_process_selftest()
    kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 51, UInt(process_ok_boot), 0)
    uartPuts("process ok=")
    uartPutDec(UInt64(process_ok_boot))
    uartPuts(" version=51\n")
    let loader_ok_boot = kernel_loader_selftest()
    kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 52, UInt(loader_ok_boot), 0)
    uartPuts("processes ok=")
    uartPutDec(UInt64(loader_ok_boot))
    uartPuts(" version=52\n")

    let multi_ok_boot = kernel_multiprocess_selftest()
    kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 53, UInt(multi_ok_boot), 0)
    uartPuts("multiprocess ok=")
    uartPutDec(UInt64(multi_ok_boot))
    uartPuts(" version=53\n")

    let sdhci_ok_boot = kernel_sdhci_probe_selftest()
    kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 54, UInt(sdhci_ok_boot), 0)
    let sdhci_cap = kernel_sdhci_probe_cap0()
    let sdhci_hostver = kernel_sdhci_probe_host_version()
    uartPuts("sdhci ok=")
    uartPutDec(UInt64(sdhci_ok_boot))
    uartPuts(" version=54")
    uartPuts(" host_version=")
    uartPutDec(UInt64(sdhci_hostver))
    uartPuts(" cap=")
    uartPutHex(UInt64(sdhci_cap))
    uartPuts("\n")

    let card_ok_boot = kernel_sdhci_card_init()
    kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 55, UInt(card_ok_boot), UInt(kernel_sdhci_card_rca()))
    let card_rca_boot = kernel_sdhci_card_rca()
    let card_ocr_boot = kernel_sdhci_card_ocr()
    uartPuts("card ok=")
    uartPutDec(UInt64(card_ok_boot))
    uartPuts(" version=55 rca=")
    uartPutHexCompact(UInt64(card_rca_boot))
    uartPuts(" ocr=")
    uartPutHex(UInt64(card_ocr_boot))
    uartPuts("\n")

    let block_ok_boot = kernel_sdhci_block_read()
    kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 56, UInt(block_ok_boot), UInt(kernel_sdhci_mbr_magic()))
    let block_mbr_boot = kernel_sdhci_mbr_magic()
    uartPuts("block ok=")
    uartPutDec(UInt64(block_ok_boot))
    uartPuts(" version=56 mbr=")
    uartPutHexCompact(UInt64(block_mbr_boot))
    uartPuts("\n")

    let fat32_ok_boot = kernel_sdhci_fat32_read()
    kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 57, UInt(fat32_ok_boot), UInt(kernel_sdhci_fat32_bytes()))
    let fat32_bytes_boot = kernel_sdhci_fat32_bytes()
    let fat32_cksum_boot = kernel_sdhci_fat32_checksum()
    uartPuts("fat32 ok=")
    uartPutDec(UInt64(fat32_ok_boot))
    uartPuts(" version=57 file=config.txt bytes=")
    uartPutDec(UInt64(fat32_bytes_boot))
    uartPuts(" checksum=")
    uartPutHexCompact(UInt64(fat32_cksum_boot))
    uartPuts(" step=")
    uartPutDec(UInt64(kernel_sdhci_fat32_step()))
    uartPuts("\n")

    let mbox_ok_boot = kernel_vc_mbox_probe()
    kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 58, UInt(mbox_ok_boot), UInt(kernel_vc_mbox_fw_rev()))
    let mbox_fw_boot = kernel_vc_mbox_fw_rev()
    uartPuts("mailbox ok=")
    uartPutDec(UInt64(mbox_ok_boot))
    uartPuts(" version=58 fw_rev=")
    uartPutHexCompact(UInt64(mbox_fw_boot))
    uartPuts("\n")

    let fb_ok_boot = kernel_vc_mbox_fb_alloc()
    kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 59, UInt(fb_ok_boot), UInt(kernel_vc_mbox_fb_addr()))
    uartPuts("framebuf ok=")
    uartPutDec(UInt64(fb_ok_boot))
    uartPuts(" version=59 width=")
    uartPutDec(UInt64(kernel_vc_mbox_fb_width()))
    uartPuts(" height=")
    uartPutDec(UInt64(kernel_vc_mbox_fb_height()))
    uartPuts(" depth=")
    uartPutDec(UInt64(kernel_vc_mbox_fb_depth()))
    uartPuts(" pitch=")
    uartPutDec(UInt64(kernel_vc_mbox_fb_pitch()))
    uartPuts(" addr=")
    uartPutHexCompact(UInt64(kernel_vc_mbox_fb_addr()))
    uartPuts("\n")
    if fb_ok_boot == 0 {
      uartPuts("framebuf_diag call_ok=")
      uartPutDec(UInt64(bitPattern: Int64(kernel_vc_mbox_fb_last_call_ok())))
      uartPuts(" resp=0x")
      uartPutHexCompact(UInt64(kernel_vc_mbox_fb_last_resp()))
      uartPuts(" qw=")
      uartPutDec(UInt64(kernel_vc_mbox_fb_query_w()))
      uartPuts(" qh=")
      uartPutDec(UInt64(kernel_vc_mbox_fb_query_h()))
      uartPuts(" raw_addr=0x")
      uartPutHexCompact(UInt64(kernel_vc_mbox_fb_raw_addr()))
      uartPuts(" raw_pitch=0x")
      uartPutHexCompact(UInt64(kernel_vc_mbox_fb_raw_pitch()))
      uartPuts("\n")
    }

    let console_ok_boot = kernel_vc_console_selftest()
    kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 60, UInt(console_ok_boot), UInt(kernel_vc_console_glyphs()))
    uartPuts("console ok=")
    uartPutDec(UInt64(console_ok_boot))
    uartPuts(" version=60 rows=")
    uartPutDec(UInt64(kernel_vc_console_rows()))
    uartPuts(" cols=")
    uartPutDec(UInt64(kernel_vc_console_cols()))
    uartPuts(" glyphs=")
    uartPutDec(UInt64(kernel_vc_console_glyphs()))
    uartPuts(" counter=")
    uartPutDec(UInt64(kernel_vc_console_counter()))
    uartPuts(" display=0\n")

    // Runtime V61: BCM2711 PCIe RC bring-up
    uartPuts("runtime v61: BCM2711 PCIe RC bring-up\n")
    let pcie_ok_boot = kernel_pcie_selftest()
    kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 61, UInt(pcie_ok_boot), UInt(kernel_pcie_speed()))
    uartPuts("pcie ok=")
    uartPutDec(UInt64(pcie_ok_boot))
    uartPuts(" version=61 link=")
    uartPuts(pcie_ok_boot != 0 ? "up" : "down")
    uartPuts(" speed=")
    uartPutDec(UInt64(kernel_pcie_speed()))
    uartPuts(" width=")
    uartPutDec(UInt64(kernel_pcie_width()))
    uartPuts(" win0_lo=")
    uartPutHexCompact(UInt64(kernel_pcie_win0_lo()))
    uartPuts(" misc_ctrl=")
    uartPutHexCompact(UInt64(kernel_pcie_misc_ctrl()))
    uartPuts(" pwr_ok=")
    uartPutDec(UInt64(bitPattern: Int64(kernel_vc_mbox_pwr_state_result())))
    uartPuts(" pwr_resp=")
    uartPutHexCompact(UInt64(kernel_vc_mbox_pwr_state_response()))
    uartPuts(" pcie_reset_ok=")
    uartPutDec(UInt64(bitPattern: Int64(kernel_vc_mbox_pcie_reset_result())))
    uartPuts(" pcie_reset_resp=")
    uartPutHexCompact(UInt64(kernel_vc_mbox_pcie_reset_response()))
    uartPuts(" mmio_pre=")
    uartPutHexCompact(UInt64(kernel_pcie_mmio_pre_reset()))
    uartPuts(" bar0_pre=")
    uartPutHexCompact(UInt64(kernel_pcie_bar0_pre_reset()))
    uartPuts(" cm_pre=")
    uartPutHexCompact(UInt64(kernel_pcie_cm_pcie_pre()))
    uartPuts(" cm_post=")
    uartPutHexCompact(UInt64(kernel_pcie_cm_pcie_post()))
    uartPuts(" cm_l0=")
    uartPutHexCompact(UInt64(kernel_pcie_cm_pcie_at_l0()))
    uartPuts(" inherited=")
    uartPutDec(UInt64(bitPattern: Int64(kernel_pcie_path_inherited())))
    uartPuts(" inherit_ms=")
    uartPutDec(UInt64(kernel_pcie_link_inherit_ms()))
    uartPuts(" rc_cmd=")
    uartPutHexCompact(UInt64(kernel_pcie_rc_cmd()))
    uartPuts(" priv1_pre=")
    uartPutHexCompact(UInt64(kernel_pcie_priv1_lnkcap_pre()))
    uartPuts(" priv1_post=")
    uartPutHexCompact(UInt64(kernel_pcie_priv1_lnkcap_post()))
    uartPuts(" lnkctl2_pre=")
    uartPutHexCompact(UInt64(kernel_pcie_lnkctl2_pre()))
    uartPuts(" lnkctl2_post=")
    uartPutHexCompact(UInt64(kernel_pcie_lnkctl2_post()))
    uartPuts(" rgr1_pi=")
    uartPutHexCompact(UInt64(kernel_pcie_rgr1_pi()))
    uartPuts(" preperst_val=")
    uartPutHexCompact(UInt64(kernel_pcie_mmio_pre_perst()))
    uartPuts(" preperst_ticks=")
    uartPutDec(UInt64(kernel_pcie_mmio_pre_perst_ticks()))
    uartPuts(" postperst_val=")
    uartPutHexCompact(UInt64(kernel_pcie_mmio_post_perst()))
    uartPuts(" postperst_ticks=")
    uartPutDec(UInt64(kernel_pcie_mmio_post_perst_ticks()))
    uartPuts(" win0_bl_l0=")
    uartPutHexCompact(UInt64(kernel_pcie_win0_bl_at_l0()))
    uartPuts(" win0_bhi_l0=")
    uartPutHexCompact(UInt64(kernel_pcie_win0_bhi_at_l0()))
    uartPuts(" win0_lhi_l0=")
    uartPutHexCompact(UInt64(kernel_pcie_win0_lhi_at_l0()))
    uartPuts(" at_l0_val=")
    uartPutHexCompact(UInt64(kernel_pcie_mmio_at_l0()))
    uartPuts(" at_l0_ticks=")
    uartPutDec(UInt64(kernel_pcie_mmio_at_l0_ticks()))
    uartPuts(" postlink_val=")
    uartPutHexCompact(UInt64(kernel_pcie_mmio_post_link()))
    uartPuts(" postlink_ticks=")
    uartPutDec(UInt64(kernel_pcie_mmio_post_link_ticks()))
    uartPuts(" lnkctl=")
    uartPutHexCompact(UInt64(kernel_pcie_lnkctl()))
    uartPuts(" hdbg_post=")
    uartPutHexCompact(UInt64(kernel_pcie_hard_debug_post()))
    uartPuts(" misc_post=")
    uartPutHexCompact(UInt64(kernel_pcie_misc_ctrl_post()))
    uartPuts(" imm_l0_val=")
    uartPutHexCompact(UInt64(kernel_pcie_mmio_imm_l0()))
    uartPuts(" imm_l0_ticks=")
    uartPutDec(UInt64(kernel_pcie_mmio_imm_l0_ticks()))
    uartPuts("\n")

    // Runtime V62: VL805 USB 3.0 controller discovery
    uartPuts("runtime v62: VL805 USB 3.0 xHCI config-space probe\n")
    let vl805_ok_boot = kernel_vl805_selftest()
    kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 62, UInt(vl805_ok_boot), UInt(kernel_vl805_device()))
    uartPuts("vl805 ok=")
    uartPutDec(UInt64(vl805_ok_boot))
    uartPuts(" version=62 vendor=")
    uartPutHexCompact(UInt64(kernel_vl805_vendor()))
    uartPuts(" device=")
    uartPutHexCompact(UInt64(kernel_vl805_device()))
    uartPuts(" notify_ms=")
    uartPutDec(UInt64(kernel_vl805_mmio_early()))
    uartPuts(" mmio_ticks=")
    uartPutDec(UInt64(kernel_vl805_mmio_early_ticks()))
    uartPuts(" mmio_ms=")
    uartPutDec(UInt64(kernel_vl805_mmio_poll_ms()))
    uartPuts(" rgr1=")
    uartPutHexCompact(UInt64(kernel_vl805_rgr1()))
    uartPuts(" fw_ver_pre=")
    uartPutHexCompact(UInt64(kernel_vl805_fw_ver_pre()))
    uartPuts(" fw_ver_post=")
    uartPutHexCompact(UInt64(kernel_vl805_rom_status()))
    uartPuts("\n")

    // Runtime V63: xHCI capability register probe
    uartPuts("runtime v63: xHCI capability register probe\n")
    let xhci_ok_boot = kernel_xhci_selftest()
    kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 63, UInt(xhci_ok_boot), UInt(kernel_xhci_ports()))
    uartPuts("xhci ok=")
    uartPutDec(UInt64(xhci_ok_boot))
    uartPuts(" version=63 hciversion=")
    uartPutHexCompact(UInt64(kernel_xhci_hciversion()))
    uartPuts(" ports=")
    uartPutDec(UInt64(kernel_xhci_ports()))
    uartPuts(" slots=")
    uartPutDec(UInt64(kernel_xhci_slots()))
    uartPuts(" scratch=")
    uartPutDec(UInt64(kernel_xhci_scratch()))
    uartPuts("\n")

    // Runtime V64: xHCI controller init (DCBAA + rings + RUN + port-connect detect)
    uartPuts("runtime v64: xHCI controller init\n")
    let xhci_run_ok_boot = kernel_xhci_run_selftest()
    kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 64, UInt(xhci_run_ok_boot), UInt(kernel_xhci_ports_connected()))
    // NOTE: xhci_run ok= line printed AFTER usb_enum to minimize time between
    // xhci_run's PR=1 (which achieves PED=1) and usb_enum's ENABLE_SLOT.
    // VL805 EEPROM firmware drops the port if no ENABLE_SLOT follows within ~100ms of PED=1.

    // Runtime V65: USB device enumeration — called immediately after xhci_run returns
    // (xhci_run ok= and runtime v65 header printed after to not add UART latency before ENABLE_SLOT)
    let usb_enum_ok_boot = kernel_usb_enum_selftest()
    // Now print v64 and v65 results (timing no longer critical)
    uartPuts("xhci_run ok=")
    uartPutDec(UInt64(xhci_run_ok_boot))
    uartPuts(" version=64 ports_connected=")
    uartPutDec(UInt64(kernel_xhci_ports_connected()))
    uartPuts("\nruntime v65: USB device enumeration\n")
    kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 65, UInt(usb_enum_ok_boot), UInt(kernel_usb_enum_addr()))
    uartPuts("usb_enum ok=")
    uartPutDec(UInt64(usb_enum_ok_boot))
    uartPuts(" version=65 addr=")
    uartPutDec(UInt64(kernel_usb_enum_addr()))
    uartPuts(" vendor=")
    uartPutHexCompact(UInt64(kernel_usb_enum_vendor()))
    uartPuts(" product=")
    uartPutHexCompact(UInt64(kernel_usb_enum_product()))
    uartPuts(" class=")
    uartPutHexCompact(UInt64(kernel_usb_enum_class()))
    uartPuts(" stage=")
    uartPutDec(UInt64(kernel_usb_enum_stage()))
    uartPuts(" portsc=")
    uartPutHex(UInt64(kernel_usb_enum_portsc()))
    uartPuts(" portscR=")
    uartPutHex(UInt64(kernel_usb_enum_portscR()))
    uartPuts(" slot_raw=")
    uartPutHexCompact(UInt64(kernel_usb_enum_slot_raw()))
    uartPuts(" ad_raw=")
    uartPutHexCompact(UInt64(kernel_usb_enum_ad_raw()))
    uartPuts("\n")

    // Runtime V66: HID boot-protocol keyboard
    uartPuts("runtime v66: HID boot-protocol keyboard\n")
    let kbd_ok_boot = kernel_kbd_selftest()
    kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 66, UInt(kbd_ok_boot), UInt(kernel_kbd_keycode()))
    uartPuts("kbd ok=")
    uartPutDec(UInt64(kbd_ok_boot))
    uartPuts(" version=66 keycode=")
    uartPutDec(UInt64(kernel_kbd_keycode()))
    uartPuts(" char=")
    uartPutDec(UInt64(kernel_kbd_char()))
    uartPuts("\n")
    uartPuts("hubwalk ok=")
    uartPutDec(UInt64(kernel_hubwalk_ok()))
    uartPuts(" version=66 ports=")
    uartPutDec(UInt64(kernel_hubwalk_ports()))
    uartPuts(" connected=")
    uartPutDec(UInt64(kernel_hubwalk_connected()))
    uartPuts(" hid=")
    uartPutDec(UInt64(kernel_hubwalk_hid()))
    uartPuts("\n")

    // Runtime V67: GENET register probe (boot-time MMIO only; no IRQ path)
    uartPuts("runtime v67: GENET register probe\n")
    let genet_ok_boot = kernel_genet_selftest()
    kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 67, UInt(genet_ok_boot), UInt(kernel_genet_rev()))
    uartPuts("genet ok=")
    uartPutDec(UInt64(genet_ok_boot))
    uartPuts(" version=67 rev=")
    uartPutHexCompact(UInt64(kernel_genet_rev()))
    uartPuts(" mdio=")
    uartPutDec(UInt64(kernel_genet_mdio()))
    uartPuts(" link=")
    uartPutDec(UInt64(kernel_genet_link()))
    uartPuts("\n")

    // Runtime V68: UMAC MAC + leftover RX_EN + MIB. No DMA, no CMD_RX_EN write.
    uartPuts("runtime v68: GENET UMAC MAC and RX MIB\n")
    let genet2_ok_boot = kernel_genet2_selftest()
    kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 68, UInt(genet2_ok_boot), UInt(kernel_genet2_frames()))
    uartPuts("genet2 ok=")
    uartPutDec(UInt64(genet2_ok_boot))
    uartPuts(" version=68 mac=")
    uartPutHexCompact(UInt64(kernel_genet2_mac()))
    uartPuts(" rx=")
    uartPutDec(UInt64(kernel_genet2_rx()))
    uartPuts(" frames=")
    uartPutDec(UInt64(kernel_genet2_frames()))
    uartPuts(" bytes=")
    uartPutDec(UInt64(kernel_genet2_bytes()))
    uartPuts("\n")

    // Runtime V69: firmware station MAC via mailbox. No UMAC write, no DMA.
    uartPuts("runtime v69: GENET mailbox station MAC\n")
    let genet3_ok_boot = kernel_genet3_selftest()
    kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 69, UInt(genet3_ok_boot), UInt(kernel_genet3_mbox()))
    uartPuts("genet3 ok=")
    uartPutDec(UInt64(genet3_ok_boot))
    uartPuts(" version=69 mac=")
    uartPutHexCompact(UInt64(kernel_genet3_mac()))
    uartPuts(" mbox=")
    uartPutDec(UInt64(kernel_genet3_mbox()))
    uartPuts(" umac=")
    uartPutHexCompact(UInt64(kernel_genet3_umac()))
    uartPuts("\n")

    // Runtime V70: firmware board serial via mailbox. No UMAC write, no DMA.
    uartPuts("runtime v70: GENET mailbox board serial\n")
    let genet4_ok_boot = kernel_genet4_selftest()
    uartPuts("genet4 ok=")
    uartPutDec(UInt64(genet4_ok_boot))
    uartPuts(" version=70 serial=")
    uartPutHexCompact(UInt64(kernel_genet4_serial()))
    uartPuts(" mbox=")
    uartPutDec(UInt64(kernel_genet4_mbox()))
    uartPuts(" mac=")
    uartPutHexCompact(UInt64(kernel_genet4_mac()))
    uartPuts("\n")

    // Runtime V71: GPIO register probe. UART token only — event ring is full.
    uartPuts("runtime v71: GPIO register probe\n")
    let gpio_ok_boot = kernel_gpio_selftest()
    uartPuts("gpio ok=")
    uartPutDec(UInt64(gpio_ok_boot))
    uartPuts(" version=71 fsel=")
    uartPutHexCompact(UInt64(kernel_gpio_fsel()))
    uartPuts(" pup=")
    uartPutHexCompact(UInt64(kernel_gpio_pup()))
    uartPuts(" uart=")
    uartPutDec(UInt64(kernel_gpio_uart()))
    uartPuts("\n")

    // Runtime V72: leftover-RX stop + NC RX ring. UART token only.
    uartPuts("runtime v72: GENET leftover-RX stop and NC RX ring\n")
    let genet5_ok_boot = kernel_genet5_selftest()
    uartPuts("genet5 ok=")
    uartPutDec(UInt64(genet5_ok_boot))
    uartPuts(" version=72 stop=")
    uartPutDec(UInt64(kernel_genet5_stop()))
    uartPuts(" ring=")
    uartPutDec(UInt64(kernel_genet5_ring()))
    uartPuts(" rx=")
    uartPutDec(UInt64(kernel_genet5_rx()))
    uartPuts(" frames=")
    uartPutDec(UInt64(kernel_genet5_frames()))
    uartPuts("\n")

    // Runtime V73: mailbox MAC into UMAC + own TX ARP. UART token only.
    uartPuts("runtime v73: GENET UMAC MAC and TX ARP\n")
    let genet6_ok_boot = kernel_genet6_selftest()
    uartPuts("genet6 ok=")
    uartPutDec(UInt64(genet6_ok_boot))
    uartPuts(" version=73 mac=")
    uartPutDec(UInt64(kernel_genet6_mac()))
    uartPuts(" tx=")
    uartPutDec(UInt64(kernel_genet6_tx()))
    uartPuts(" frames=")
    uartPutDec(UInt64(kernel_genet6_frames()))
    uartPuts("\n")

    // Runtime V74: Linux ring-16 + leftover RBUF reset. UART token only.
    uartPuts("runtime v74: GENET Linux ring-16 and TX CONS\n")
    let genet7_ok_boot = kernel_genet7_selftest()
    uartPuts("genet7 ok=")
    uartPutDec(UInt64(genet7_ok_boot))
    uartPuts(" version=74 ring=")
    uartPutDec(UInt64(kernel_genet7_ring()))
    uartPuts(" tx=")
    uartPutDec(UInt64(kernel_genet7_tx()))
    uartPuts(" cons=")
    uartPutDec(UInt64(kernel_genet7_cons()))
    uartPuts(" prod=")
    uartPutDec(UInt64(kernel_genet7_prod()))
    uartPuts(" frames=")
    uartPutDec(UInt64(kernel_genet7_frames()))
    uartPuts("\n")

    // Runtime V75: v4 TDMA PROD doorbell. UART token only.
    uartPuts("runtime v75: GENET v4 TDMA PROD doorbell\n")
    let genet8_ok_boot = kernel_genet8_selftest()
    uartPuts("genet8 ok=")
    uartPutDec(UInt64(genet8_ok_boot))
    uartPuts(" version=75 prod=")
    uartPutDec(UInt64(kernel_genet8_prod()))
    uartPuts(" cons=")
    uartPutDec(UInt64(kernel_genet8_cons()))
    uartPuts(" tx=")
    uartPutDec(UInt64(kernel_genet8_tx()))
    uartPuts(" frames=")
    uartPutDec(UInt64(kernel_genet8_frames()))
    uartPuts("\n")

    // Runtime V76: parse one RX ARP/ICMP request and reply. UART token only.
    uartPuts("runtime v76: GENET ARP or ICMP reply\n")
    let genet9_ok_boot = kernel_genet9_selftest()
    uartPuts("genet9 ok=")
    uartPutDec(UInt64(genet9_ok_boot))
    uartPuts(" version=76 rx=")
    uartPutDec(UInt64(kernel_genet9_rx()))
    uartPuts(" tx=")
    uartPutDec(UInt64(kernel_genet9_tx()))
    uartPuts(" kind=")
    let genet9_kind_boot = kernel_genet9_kind()
    if genet9_kind_boot == 1 {
        uartPuts("arp")
    } else if genet9_kind_boot == 2 {
        uartPuts("icmp")
    } else {
        uartPuts("none")
    }
    uartPuts("\n")

    // Runtime V77: bounded multi-reply poll. UART token only. No long boot wait.
    uartPuts("runtime v77: GENET bounded multi-reply poll\n")
    let genet10_ok_boot = kernel_genet10_selftest()
    uartPuts("genet10 ok=")
    uartPutDec(UInt64(genet10_ok_boot))
    uartPuts(" version=77 rx=")
    uartPutDec(UInt64(kernel_genet10_rx()))
    uartPuts(" tx=")
    uartPutDec(UInt64(kernel_genet10_tx()))
    uartPuts(" replies=")
    uartPutDec(UInt64(kernel_genet10_replies()))
    uartPuts(" kind=")
    let genet10_kind_boot = kernel_genet10_kind()
    if genet10_kind_boot == 1 {
        uartPuts("arp")
    } else if genet10_kind_boot == 2 {
        uartPuts("icmp")
    } else {
        uartPuts("none")
    }
    uartPuts("\n")

    // Runtime V78: bounded UDP echo. UART token only. No long boot wait.
    uartPuts("runtime v78: GENET bounded UDP echo\n")
    let genet11_ok_boot = kernel_genet11_selftest()
    uartPuts("genet11 ok=")
    uartPutDec(UInt64(genet11_ok_boot))
    uartPuts(" version=78 rx=")
    uartPutDec(UInt64(kernel_genet11_rx()))
    uartPuts(" tx=")
    uartPutDec(UInt64(kernel_genet11_tx()))
    uartPuts(" replies=")
    uartPutDec(UInt64(kernel_genet11_replies()))
    uartPuts(" kind=")
    let genet11_kind_boot = kernel_genet11_kind()
    if genet11_kind_boot == 3 {
        uartPuts("udp")
    } else {
        uartPuts("none")
    }
    uartPuts("\n")

    // Runtime V79: bounded TCP echo. UART token only. No long boot wait.
    uartPuts("runtime v79: GENET bounded TCP echo\n")
    let genet12_ok_boot = kernel_genet12_selftest()
    uartPuts("genet12 ok=")
    uartPutDec(UInt64(genet12_ok_boot))
    uartPuts(" version=79 rx=")
    uartPutDec(UInt64(kernel_genet12_rx()))
    uartPuts(" tx=")
    uartPutDec(UInt64(kernel_genet12_tx()))
    uartPuts(" replies=")
    uartPutDec(UInt64(kernel_genet12_replies()))
    uartPuts(" kind=")
    let genet12_kind_boot = kernel_genet12_kind()
    if genet12_kind_boot == 4 {
        uartPuts("tcp")
    } else {
        uartPuts("none")
    }
    uartPuts("\n")

    // Runtime V80: BSC1 + SPI0 probe. UART token only. No boot event emit.
    uartPuts("runtime v80: I2C and SPI register probe\n")
    let i2c_ok_boot = kernel_i2c_selftest()
    uartPuts("i2c ok=")
    uartPutDec(UInt64(i2c_ok_boot))
    uartPuts(" version=80 bsc=")
    uartPutDec(UInt64(kernel_i2c_bsc()))
    uartPuts(" div=")
    uartPutHexCompact(UInt64(kernel_i2c_div()))
    uartPuts(" spi=")
    uartPutDec(UInt64(kernel_i2c_spi()))
    uartPuts("\n")

    // Runtime V81: PWM0+PWM1 probe. UART token only. No boot event emit.
    // No EL0 enter: after GENET DMA that I-aborts (esr=0xbf000002).
    uartPuts("runtime v81: PWM register probe\n")
    let pwm_ok_boot = kernel_pwm_selftest()
    uartPuts("pwm ok=")
    uartPutDec(UInt64(pwm_ok_boot))
    uartPuts(" version=81 ctl=")
    uartPutHexCompact(UInt64(kernel_pwm_ctl()))
    uartPuts(" sta=")
    uartPutHexCompact(UInt64(kernel_pwm_sta()))
    uartPuts(" pwm1=")
    uartPutDec(UInt64(kernel_pwm_pwm1()))
    uartPuts("\n")

    // Runtime V82: bounded BSC1 no-ACK write. UART token only. No boot event emit.
    // No EL0 enter: after GENET DMA that I-aborts (esr=0xbf000002).
    uartPuts("runtime v82: I2C no-ACK transfer\n")
    let i2c2_ok_boot = kernel_i2c2_selftest()
    uartPuts("i2c2 ok=")
    uartPutDec(UInt64(i2c2_ok_boot))
    uartPuts(" version=82 nack=")
    uartPutDec(UInt64(kernel_i2c2_nack()))
    uartPuts(" addr=")
    uartPutHexCompact(UInt64(kernel_i2c2_addr()))
    uartPuts(" sta=")
    uartPutHexCompact(UInt64(kernel_i2c2_sta()))
    uartPuts("\n")

    // Runtime V83: bounded SPI0 byte. UART token only. No boot event emit.
    // No EL0 enter: after GENET DMA that I-aborts (esr=0xbf000002).
    uartPuts("runtime v83: SPI0 bounded transfer\n")
    let spi2_ok_boot = kernel_spi2_selftest()
    uartPuts("spi2 ok=")
    uartPutDec(UInt64(spi2_ok_boot))
    uartPuts(" version=83 done=")
    uartPutDec(UInt64(kernel_spi2_done()))
    uartPuts(" loop=")
    uartPutDec(UInt64(kernel_spi2_loop()))
    uartPuts(" rx=")
    uartPutHexCompact(UInt64(kernel_spi2_rx()))
    uartPuts("\n")

    // Runtime V84: system timer probe. UART token only. No boot event emit.
    // No EL0 enter: after GENET DMA that I-aborts (esr=0xbf000002).
    uartPuts("runtime v84: system timer register probe\n")
    let stimer_ok_boot = kernel_stimer_selftest()
    uartPuts("stimer ok=")
    uartPutDec(UInt64(stimer_ok_boot))
    uartPuts(" version=84 clo=")
    uartPutHexCompact(UInt64(kernel_stimer_clo()))
    uartPuts(" chi=")
    uartPutHexCompact(UInt64(kernel_stimer_chi()))
    uartPuts(" chans=")
    uartPutDec(UInt64(kernel_stimer_chans()))
    uartPuts("\n")

    // Runtime V85: SD reload after GENET. UART token only. No boot event emit.
    // No EL0 enter: after GENET DMA that I-aborts (esr=0xbf000002).
    uartPuts("runtime v85: SD config.txt reload\n")
    let sdload_ok_boot = kernel_sdload_selftest()
    uartPuts("sdload ok=")
    uartPutDec(UInt64(sdload_ok_boot))
    uartPuts(" version=85 file=config.txt bytes=")
    uartPutDec(UInt64(kernel_sdload_bytes()))
    uartPuts(" checksum=")
    uartPutHexCompact(UInt64(kernel_sdload_sum()))
    uartPuts(" match=")
    uartPutDec(UInt64(kernel_sdload_match()))
    uartPuts("\n")

    // Runtime V86: FAT32 root list after GENET. UART token only. No boot event emit.
    // No EL0 enter: after GENET DMA that I-aborts (esr=0xbf000002).
    uartPuts("runtime v86: FAT32 root list\n")
    let sdls_ok_boot = kernel_sdls_selftest()
    uartPuts("sdls ok=")
    uartPutDec(UInt64(sdls_ok_boot))
    uartPuts(" version=86 files=")
    uartPutDec(UInt64(kernel_sdls_files()))
    uartPuts(" config=")
    uartPutDec(UInt64(kernel_sdls_config()))
    uartPuts(" other=")
    uartPutHexCompact(UInt64(kernel_sdls_other()))
    uartPuts("\n")

    // Runtime V87: second FAT32 file after GENET. UART token only. No boot event emit.
    // No EL0 enter: after GENET DMA that I-aborts (esr=0xbf000002).
    uartPuts("runtime v87: FAT32 second file\n")
    let sdfile_ok_boot = kernel_sdfile_selftest()
    uartPuts("sdfile ok=")
    uartPutDec(UInt64(sdfile_ok_boot))
    uartPuts(" version=87 name=")
    uartPutHexCompact(UInt64(kernel_sdfile_name()))
    uartPuts(" bytes=")
    uartPutDec(UInt64(kernel_sdfile_bytes()))
    uartPuts(" checksum=")
    uartPutHexCompact(UInt64(kernel_sdfile_sum()))
    uartPuts("\n")

    // Runtime V88: overlays/ walk after GENET. UART token only. No boot event emit.
    // No EL0 enter: after GENET DMA that I-aborts (esr=0xbf000002).
    uartPuts("runtime v88: FAT32 overlays walk\n")
    let sdovl_ok_boot = kernel_sdovl_selftest()
    uartPuts("sdovl ok=")
    uartPutDec(UInt64(sdovl_ok_boot))
    uartPuts(" version=88 files=")
    uartPutDec(UInt64(kernel_sdovl_files()))
    uartPuts(" name=")
    uartPutHexCompact(UInt64(kernel_sdovl_name()))
    uartPuts("\n")

    // Runtime V89: one overlays/ file after GENET. UART token only. No boot event emit.
    // No EL0 enter: after GENET DMA that I-aborts (esr=0xbf000002).
    uartPuts("runtime v89: FAT32 overlay file\n")
    let sdovf_ok_boot = kernel_sdovf_selftest()
    uartPuts("sdovf ok=")
    uartPutDec(UInt64(sdovf_ok_boot))
    uartPuts(" version=89 name=")
    uartPutHexCompact(UInt64(kernel_sdovf_name()))
    uartPuts(" bytes=")
    uartPutDec(UInt64(kernel_sdovf_bytes()))
    uartPuts(" checksum=")
    uartPutHexCompact(UInt64(kernel_sdovf_sum()))
    uartPuts("\n")

    // Runtime V90: issue.txt by name after GENET. UART token only. No boot event emit.
    // Fail-closed if missing. No EL0 enter: after GENET DMA that I-aborts (esr=0xbf000002).
    uartPuts("runtime v90: FAT32 issue.txt\n")
    let sdiss_ok_boot = kernel_sdiss_selftest()
    uartPuts("sdiss ok=")
    uartPutDec(UInt64(sdiss_ok_boot))
    uartPuts(" version=90")
    uartPuts(" present=")
    uartPutDec(UInt64(kernel_sdiss_present()))
    uartPuts(" name=")
    uartPutHexCompact(UInt64(kernel_sdiss_name()))
    uartPuts(" bytes=")
    uartPutDec(UInt64(kernel_sdiss_bytes()))
    uartPuts(" checksum=")
    uartPutHexCompact(UInt64(kernel_sdiss_sum()))
    uartPuts("\n")

    // Runtime V91: GPIO42 SET/CLR + GPLEV. UART token only. No boot event emit.
    // No EL0 enter: after GENET DMA that I-aborts (esr=0xbf000002).
    uartPuts("runtime v91: GPIO output readback\n")
    let gpio2_ok_boot = kernel_gpio2_selftest()
    uartPuts("gpio2 ok=")
    uartPutDec(UInt64(gpio2_ok_boot))
    uartPuts(" version=91 pin=")
    uartPutDec(UInt64(kernel_gpio2_pin()))
    uartPuts(" set=")
    uartPutDec(UInt64(kernel_gpio2_set()))
    uartPuts(" clr=")
    uartPutDec(UInt64(kernel_gpio2_clr()))
    uartPuts("\n")

    // Runtime V92: system timer C1 match. UART token only. No boot event emit.
    // ARM C1 only. No EL0 enter: after GENET DMA that I-aborts (esr=0xbf000002).
    uartPuts("runtime v92: system timer C1 match\n")
    let stimer2_ok_boot = kernel_stimer2_selftest()
    uartPuts("stimer2 ok=")
    uartPutDec(UInt64(stimer2_ok_boot))
    uartPuts(" version=92 chan=")
    uartPutDec(UInt64(kernel_stimer2_chan()))
    uartPuts(" match=")
    uartPutDec(UInt64(kernel_stimer2_match()))
    uartPuts("\n")

    // Runtime V93: PWM clock enable + CTL poke. UART token only. No boot event emit.
    // No pin-mux. No EL0 enter: after GENET DMA that I-aborts (esr=0xbf000002).
    uartPuts("runtime v93: PWM clock enable\n")
    let pwm2_ok_boot = kernel_pwm2_selftest()
    uartPuts("pwm2 ok=")
    uartPutDec(UInt64(pwm2_ok_boot))
    uartPuts(" version=93 clk=")
    uartPutDec(UInt64(kernel_pwm2_clk()))
    uartPuts(" en=")
    uartPutDec(UInt64(kernel_pwm2_en()))
    uartPuts("\n")

    // Runtime V94: GPIO26 PUP_PDN write+readback. UART token only. No boot event emit.
    // REG1 only. No EL0 enter: after GENET DMA that I-aborts (esr=0xbf000002).
    uartPuts("runtime v94: GPIO PUP readback\n")
    let gpio3_ok_boot = kernel_gpio3_selftest()
    uartPuts("gpio3 ok=")
    uartPutDec(UInt64(gpio3_ok_boot))
    uartPuts(" version=94 pin=")
    uartPutDec(UInt64(kernel_gpio3_pin()))
    uartPuts(" up=")
    uartPutDec(UInt64(kernel_gpio3_up()))
    uartPuts(" dn=")
    uartPutDec(UInt64(kernel_gpio3_dn()))
    uartPuts("\n")

    // Runtime V95: system timer C3 match. UART token only. No boot event emit.
    // ARM C3 only. No EL0 enter: after GENET DMA that I-aborts (esr=0xbf000002).
    uartPuts("runtime v95: system timer C3 match\n")
    let stimer3_ok_boot = kernel_stimer3_selftest()
    uartPuts("stimer3 ok=")
    uartPutDec(UInt64(stimer3_ok_boot))
    uartPuts(" version=95 chan=")
    uartPutDec(UInt64(kernel_stimer3_chan()))
    uartPuts(" match=")
    uartPutDec(UInt64(kernel_stimer3_match()))
    uartPuts("\n")

    // Runtime V96: mailbox GET_TEMPERATURE. UART token only. No boot event emit.
    // Millidegrees. No EL0 enter: after GENET DMA that I-aborts (esr=0xbf000002).
    uartPuts("runtime v96: mailbox temperature\n")
    let mboxt_ok_boot = kernel_mboxt_selftest()
    uartPuts("mboxt ok=")
    uartPutDec(UInt64(mboxt_ok_boot))
    uartPuts(" version=96 temp=")
    uartPutDec(UInt64(kernel_mboxt_temp()))
    uartPuts("\n")

    // Runtime V97: mailbox GET_CLOCK_RATE (ARM). UART token only. No boot event emit.
    // Hz. No EL0 enter: after GENET DMA that I-aborts (esr=0xbf000002).
    uartPuts("runtime v97: mailbox clock rate\n")
    let mboxc_ok_boot = kernel_mboxc_selftest()
    uartPuts("mboxc ok=")
    uartPutDec(UInt64(mboxc_ok_boot))
    uartPuts(" version=97 clk=")
    uartPutDec(UInt64(kernel_mboxc_clk()))
    uartPuts(" hz=")
    uartPutDec(UInt64(kernel_mboxc_hz()))
    uartPuts("\n")

    // Runtime V98: PM watchdog remaining-tick readback. UART token only.
    // Arm, read, disable. Never reset_now. No boot event emit.
    // No EL0 enter: after GENET DMA that I-aborts (esr=0xbf000002).
    uartPuts("runtime v98: watchdog remaining\n")
    let wdog2_ok_boot = kernel_wdog2_selftest()
    uartPuts("wdog2 ok=")
    uartPutDec(UInt64(wdog2_ok_boot))
    uartPuts(" version=98 armed=")
    uartPutDec(UInt64(kernel_wdog2_armed()))
    uartPuts(" off=")
    uartPutDec(UInt64(kernel_wdog2_off()))
    uartPuts(" remain=")
    uartPutDec(UInt64(kernel_wdog2_remain()))
    uartPuts("\n")

    // Runtime V99: mailbox GET_VOLTAGE (core). UART token only. No boot event emit.
    // Microvolts. No EL0 enter: after GENET DMA that I-aborts (esr=0xbf000002).
    uartPuts("runtime v99: mailbox voltage\n")
    let mboxv_ok_boot = kernel_mboxv_selftest()
    uartPuts("mboxv ok=")
    uartPutDec(UInt64(mboxv_ok_boot))
    uartPuts(" version=99 id=")
    uartPutDec(UInt64(kernel_mboxv_id()))
    uartPuts(" uv=")
    uartPutDec(UInt64(kernel_mboxv_uv()))
    uartPuts("\n")

    // Runtime V100: BCM2711 RNG200 word. UART token only. No boot event emit.
    // FIFO ready + one word. No EL0 enter: after GENET DMA that I-aborts.
    uartPuts("runtime v100: RNG200 word\n")
    let rng_ok_boot = kernel_rng_selftest()
    uartPuts("rng ok=")
    uartPutDec(UInt64(rng_ok_boot))
    uartPuts(" version=100 ready=")
    uartPutDec(UInt64(kernel_rng_ready()))
    uartPuts(" data=")
    uartPutDec(UInt64(kernel_rng_data()))
    uartPuts("\n")

    // Runtime V101: BCM2711 DMA engine memcpy. UART token only. No boot event emit.
    // Channel 4, 32-byte NC copy, src==dst. No EL0 enter: after GENET DMA that I-aborts.
    uartPuts("runtime v101: DMA memcpy\n")
    let dma2_ok_boot = kernel_dma2_selftest()
    uartPuts("dma2 ok=")
    uartPutDec(UInt64(dma2_ok_boot))
    uartPuts(" version=101 chan=")
    uartPutDec(UInt64(kernel_dma2_chan()))
    uartPuts(" match=")
    uartPutDec(UInt64(kernel_dma2_match()))
    uartPuts(" bytes=")
    uartPutDec(UInt64(kernel_dma2_bytes()))
    uartPuts("\n")

    // Runtime V102: SDHCI CMD24 free-cluster write. UART token only. No boot event emit.
    // FAT/dir unchanged. No EL0 enter: after GENET DMA that I-aborts.
    uartPuts("runtime v102: SD free-cluster write\n")
    let sdwr_ok_boot = kernel_sdwr_selftest()
    uartPuts("sdwr ok=")
    uartPutDec(UInt64(sdwr_ok_boot))
    uartPuts(" version=102 match=")
    uartPutDec(UInt64(kernel_sdwr_match()))
    uartPuts(" bytes=")
    uartPutDec(UInt64(kernel_sdwr_bytes()))
    uartPuts(" clus=")
    uartPutDec(UInt64(kernel_sdwr_clus()))
    uartPuts("\n")

    // Runtime V103: FAT32 scratch create. UART token only. No boot event emit.
    // AETHER.TMP only. No EL0 enter: after GENET DMA that I-aborts.
    uartPuts("runtime v103: FAT32 scratch create\n")
    let sdmk_ok_boot = kernel_sdmk_selftest()
    uartPuts("sdmk ok=")
    uartPutDec(UInt64(sdmk_ok_boot))
    uartPuts(" version=103 match=")
    uartPutDec(UInt64(kernel_sdmk_match()))
    uartPuts(" created=")
    uartPutDec(UInt64(kernel_sdmk_created()))
    uartPuts(" name=")
    uartPutDec(UInt64(kernel_sdmk_name()))
    uartPuts("\n")

    // Runtime V104: FAT32 scratch re-read by name. UART token only. No boot event emit.
    // AETHER.TMP only. No EL0 enter: after GENET DMA that I-aborts.
    uartPuts("runtime v104: FAT32 scratch reread\n")
    let sdrd_ok_boot = kernel_sdrd_selftest()
    uartPuts("sdrd ok=")
    uartPutDec(UInt64(sdrd_ok_boot))
    uartPuts(" version=104 match=")
    uartPutDec(UInt64(kernel_sdrd_match()))
    uartPuts(" present=")
    uartPutDec(UInt64(kernel_sdrd_present()))
    uartPuts(" name=")
    uartPutDec(UInt64(kernel_sdrd_name()))
    uartPuts("\n")

    // Runtime V105: SDHCI CMD13 card status. UART token only. No boot event emit.
    // No EL0 enter: after GENET DMA that I-aborts.
    uartPuts("runtime v105: SDHCI card status\n")
    let sdst_ok_boot = kernel_sdst_selftest()
    uartPuts("sdst ok=")
    uartPutDec(UInt64(sdst_ok_boot))
    uartPuts(" version=105 state=")
    uartPutDec(UInt64(kernel_sdst_state()))
    uartPuts(" ready=")
    uartPutDec(UInt64(kernel_sdst_ready()))
    uartPuts(" rca=")
    uartPutDec(UInt64(kernel_sdst_rca()))
    uartPuts("\n")

    // Runtime V106: SDHCI ACMD51 SEND_SCR. UART token only. No boot event emit.
    // No EL0 enter: after GENET DMA that I-aborts.
    uartPuts("runtime v106: SDHCI send SCR\n")
    let sdscr_ok_boot = kernel_sdscr_selftest()
    uartPuts("sdscr ok=")
    uartPutDec(UInt64(sdscr_ok_boot))
    uartPuts(" version=106 spec=")
    uartPutDec(UInt64(kernel_sdscr_spec()))
    uartPuts(" bus=")
    uartPutDec(UInt64(kernel_sdscr_bus()))
    uartPuts("\n")

    // Runtime V107: SDHCI ACMD13 SD_STATUS. UART token only. No boot event emit.
    // No EL0 enter: after GENET DMA that I-aborts.
    uartPuts("runtime v107: SDHCI SD_STATUS\n")
    let sdss_ok_boot = kernel_sdss_selftest()
    uartPuts("sdss ok=")
    uartPutDec(UInt64(sdss_ok_boot))
    uartPuts(" version=107 type=")
    uartPutDec(UInt64(kernel_sdss_type()))
    uartPuts(" class=")
    uartPutDec(UInt64(kernel_sdss_class()))
    uartPuts("\n")

    // Runtime V108: SDHCI ACMD6 SET_BUS_WIDTH. UART token only. No boot event emit.
    // No EL0 enter: after GENET DMA that I-aborts.
    uartPuts("runtime v108: SDHCI bus width\n")
    let sdbus_ok_boot = kernel_sdbus_selftest()
    uartPuts("sdbus ok=")
    uartPutDec(UInt64(sdbus_ok_boot))
    uartPuts(" version=108 bits=")
    uartPutDec(UInt64(kernel_sdbus_bits()))
    uartPuts(" host=")
    uartPutDec(UInt64(kernel_sdbus_host()))
    uartPuts(" mbr=")
    uartPutDec(UInt64(kernel_sdbus_mbr()))
    uartPuts("\n")

    // Runtime V109: SDHCI CMD18 multi-block. UART token only. No boot event emit.
    // No EL0 enter: after GENET DMA that I-aborts.
    uartPuts("runtime v109: SDHCI multi-block\n")
    let sdmb_ok_boot = kernel_sdmb_selftest()
    uartPuts("sdmb ok=")
    uartPutDec(UInt64(sdmb_ok_boot))
    uartPuts(" version=109 blocks=")
    uartPutDec(UInt64(kernel_sdmb_blocks()))
    uartPuts(" mbr=")
    uartPutDec(UInt64(kernel_sdmb_mbr()))
    uartPuts("\n")

    // Runtime V110: SDHCI CMD6 SWITCH_FUNC check. UART token only. No boot event emit.
    // No EL0 enter: after GENET DMA that I-aborts.
    uartPuts("runtime v110: SDHCI switch check\n")
    let sdsw_ok_boot = kernel_sdsw_selftest()
    uartPuts("sdsw ok=")
    uartPutDec(UInt64(sdsw_ok_boot))
    uartPuts(" version=110 mode=")
    uartPutDec(UInt64(kernel_sdsw_mode()))
    uartPuts(" grp1=")
    uartPutDec(UInt64(kernel_sdsw_grp1()))
    uartPuts("\n")

    // Runtime V111: SDHCI CMD25 multi-block write. UART token only. No boot event emit.
    // No EL0 enter: after GENET DMA that I-aborts.
    uartPuts("runtime v111: SDHCI multi-block write\n")
    let sdmw_ok_boot = kernel_sdmw_selftest()
    uartPuts("sdmw ok=")
    uartPutDec(UInt64(sdmw_ok_boot))
    uartPuts(" version=111 match=")
    uartPutDec(UInt64(kernel_sdmw_match()))
    uartPuts(" blocks=")
    uartPutDec(UInt64(kernel_sdmw_blocks()))
    uartPuts(" clus=")
    uartPutDec(UInt64(kernel_sdmw_clus()))
    uartPuts("\n")

    kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 22, UInt(kernel_pool_selftest()), UInt(kernel_pool_count()))
    kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 23, UInt(heap_fragmentation_selftest()), UInt(kernel_pool_pressure_selftest()))
    kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 24, UInt(kernel_driver_registry_selftest()), UInt(kernel_driver_count()))
    kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 25, 2, 1)
    kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 27, UInt(kernel_retained_valid()), UInt(kernel_retained_reason_id()))
    kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 28, UInt(kernel_runtime_audit_selftest()), UInt(kernel_runtime_required_symbol_count()))
    kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 29, 1, UInt(kernel_event_lost_count()))
    kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 30, 1, UInt(kernel_event_lost_count()))
    let runtimeV31 = kernel_scheduler_selftest()
    kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 31, UInt(runtimeV31), UInt(kernel_scheduler_core_count()))
    if runtimeV31 != 0 {
        uartPuts("schedselftest ok=1 version=31\n")
    }
    let runtimeV32 = kernel_smp_selftest()
    kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 32, UInt(runtimeV32), UInt(kernel_smp_online_count()))
    if runtimeV32 != 0 {
        uartPuts("schedselftest ok=1 version=32\n")
    }
    let runtimeV33 = kernel_atomic_selftest() != 0 && kernel_spinlock_selftest() != 0 && kernel_scheduler_runqueue_selftest() != 0 ? 1 : 0
    kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 33, UInt(runtimeV33), UInt(kernel_scheduler_core_count()))
    if runtimeV33 != 0 {
        uartPuts("schedselftest ok=1 version=33\n")
    }
    let runtimeV34 = kernel_scheduler_smp_selftest()
    kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 34, UInt(runtimeV34), UInt(kernel_scheduler_total_dispatch_count()))
    if runtimeV34 != 0 {
        uartPuts("schedselftest ok=1 version=34\n")
    }
    let runtimeV35 = kernel_scheduler_secondary_worker_selftest()
    kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 35, UInt(runtimeV35), UInt(kernel_scheduler_secondary_worker_total()))
    if runtimeV35 != 0 {
        uartPuts("schedselftest ok=1 version=35\n")
    }
    let runtimeV36 = kernel_scheduler_timer_worker_feed_selftest()
    kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 36, UInt(runtimeV36), UInt(kernel_scheduler_secondary_worker_feed_total()))
    if runtimeV36 != 0 {
        uartPuts("schedselftest ok=1 version=36\n")
    }
    let runtimeV37 = kernel_scheduler_secondary_job_selftest()
    kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 37, UInt(runtimeV37), UInt(kernel_scheduler_secondary_job_total()))
    if runtimeV37 != 0 {
        uartPuts("schedselftest ok=1 version=37\n")
    }
    let runtimeV38 = kernel_scheduler_secondary_wake_selftest()
    kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 38, UInt(runtimeV38), UInt(kernel_scheduler_secondary_wake_signal_total()))
    if runtimeV38 != 0 {
        uartPuts("schedselftest ok=1 version=38\n")
    }
    let runtimeV39 = kernel_scheduler_secondary_handoff_selftest()
    kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 39, UInt(runtimeV39), UInt(kernel_scheduler_secondary_handoff_issue_total()))
    if runtimeV39 != 0 {
        uartPuts("schedselftest ok=1 version=39\n")
    }
    let runtimeV40 = kernel_scheduler_backpressure_selftest()
    kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 40, UInt(runtimeV40), UInt(kernel_scheduler_runqueue_overflow_total()))
    if runtimeV40 != 0 {
        uartPuts("schedselftest ok=1 version=40\n")
    }
    let runtimeV41 = kernel_scheduler_work_steal_selftest()
    kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 41, UInt(runtimeV41), UInt(kernel_scheduler_steal_total()))
    if runtimeV41 != 0 {
        uartPuts("schedselftest ok=1 version=41\n")
    }
    let runtimeV42 = kernel_scheduler_fairness_selftest()
    kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 42, UInt(runtimeV42), UInt(kernel_scheduler_balance_total()))
    if runtimeV42 != 0 {
        uartPuts("schedselftest ok=1 version=42\n")
    }
    let runtimeV43 = kernel_scheduler_priority_selftest()
    kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 43, UInt(runtimeV43), UInt(kernel_scheduler_priority_preempt_total()))
    if runtimeV43 != 0 {
        uartPuts("schedselftest ok=1 version=43\n")
    }
    let runtimeV44 = kernel_scheduler_concurrency_soak_selftest()
    kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 44, UInt(runtimeV44), UInt(kernel_scheduler_concurrency_soak_round_total()))
    if runtimeV44 != 0 {
        // Feature token: V44 concurrency soak. Package KERNEL_SCHEDULER_VERSION is 46.
        uartPuts("schedselftest ok=1 version=44\n")
    }
    let handleSelftest = kernel_object_handle_selftest()
    let capSelftest = kernel_object_capcheck_selftest()
    kernel_event_emit(KERNEL_EVENT_KIND_HANDLE, UInt(handleSelftest), UInt(capSelftest), 0)
    uartPuts("handlecheck ok=")
    uartPutDec(UInt64(handleSelftest != 0 && capSelftest != 0 ? 1 : 0))
    uartPuts(" handle_selftest=")
    uartPutDec(UInt64(handleSelftest))
    uartPuts(" cap_selftest=")
    uartPutDec(UInt64(capSelftest))
    uartPuts("\n")
    printBootcheck()
    installKernelExecutor()
    spawnAetherTask(TASK_FAST_ID, KERNEL_TASK_ROOT_PARENT) { await fastHeartbeat() }
    spawnAetherTask(TASK_SLOW_ID, KERNEL_TASK_ROOT_PARENT) { await slowHeartbeat() }
    spawnAetherTask(TASK_LONG_ID, KERNEL_TASK_ROOT_PARENT) { await longHeartbeat() }
    spawnAetherTask(TASK_MAIL_TX_ID, KERNEL_TASK_ROOT_PARENT) { await mailboxProducer() }
    spawnAetherTask(TASK_MAIL_RX_ID, KERNEL_TASK_ROOT_PARENT) { await mailboxConsumer() }
    uart_shell_buffer_clear()
    spawnAetherTask(TASK_SHELL_ID, KERNEL_TASK_ROOT_PARENT) { await uartShellMain() }
    irq_enable()
    swift_task_asyncMainDrainQueue()
  }
}

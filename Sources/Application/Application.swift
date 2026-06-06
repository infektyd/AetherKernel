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
    kernel_memory_init()
    kernel_pool_init()
    kernel_cancel_init()
    kernel_event_log_init()
    kernel_scheduler_init()
    kernel_event_emit(KERNEL_EVENT_KIND_BOOT, 33, 0, 0)
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
    kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 18, UInt(kernel_cancel_selftest()), 0)
    kernel_event_emit(KERNEL_EVENT_KIND_TASK, 19, UInt(aetherTaskSpawnSelftest()), UInt(kernel_task_count()))
    kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 20, UInt(aetherChannelSelftest()), UInt(kernel_mailbox_count()))
    kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 21, UInt(kernel_mmu_selftest()), 0)
    kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 22, UInt(kernel_pool_selftest()), UInt(kernel_pool_count()))
    kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 23, UInt(heap_fragmentation_selftest()), UInt(kernel_pool_pressure_selftest()))
    kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 24, UInt(kernel_driver_registry_selftest()), UInt(kernel_driver_count()))
    kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 25, 2, 1)
    kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 27, UInt(kernel_retained_valid()), UInt(kernel_retained_reason_id()))
    kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 28, UInt(kernel_runtime_audit_selftest()), UInt(kernel_runtime_required_symbol_count()))
    kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 29, 1, UInt(kernel_event_lost_count()))
    kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 30, 1, UInt(kernel_event_lost_count()))
    kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 31, UInt(kernel_scheduler_selftest()), UInt(kernel_scheduler_core_count()))
    kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 32, UInt(kernel_smp_selftest()), UInt(kernel_smp_online_count()))
    let runtimeV33 = kernel_atomic_selftest() != 0 && kernel_spinlock_selftest() != 0 && kernel_scheduler_runqueue_selftest() != 0 ? 1 : 0
    kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 33, UInt(runtimeV33), UInt(kernel_scheduler_core_count()))
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

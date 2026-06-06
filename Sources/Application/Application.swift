//===----------------------------------------------------------------------===//
// AetherKernel entry point.
//
// boot.S parks the secondary cores, drops EL2->EL1, enables FP/SIMD (CPACR) and
// the MMU (Normal cacheable RAM, required for the concurrency runtime's atomics),
// sets the stack + EL1 vectors, and `bl _main` into this @main. From here we run
// real Swift async/await tasks on a custom cooperative executor.
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

let MAILBOX_DEMO_ID: UInt32 = 0
let MAILBOX_SELFTEST_ID: UInt32 = 1

func registerKernelTask(_ taskID: UInt32, _ name: StaticString, _ periodMS: UInt32) {
  _ = kernel_task_register(taskID, name.utf8Start, UInt32(name.utf8CodeUnitCount), periodMS)
}

func registerKernelMailbox(_ mailboxID: UInt32, _ name: StaticString) {
  _ = kernel_mailbox_register(mailboxID, name.utf8Start, UInt32(name.utf8CodeUnitCount))
}

func mailboxReceiveU64(_ mailboxID: UInt32) async -> UInt64 {
  var value: UInt = 0
  while kernel_mailbox_recv_u64(mailboxID, &value) == 0 {
    await timerSleepMillis(25)
  }
  return UInt64(value)
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
    registerKernelTask(TASK_FAST_ID, "fast", 250)
    registerKernelTask(TASK_SLOW_ID, "slow", 1000)
    registerKernelTask(TASK_LONG_ID, "long", 2000)
    registerKernelTask(TASK_SHELL_ID, "shell", 0)
    registerKernelTask(TASK_MAIL_TX_ID, "mail-tx", 750)
    registerKernelTask(TASK_MAIL_RX_ID, "mail-rx", 0)
  }

  static func registerRuntimeMailboxes() {
    registerKernelMailbox(MAILBOX_DEMO_ID, "demo")
    registerKernelMailbox(MAILBOX_SELFTEST_ID, "selftest")
  }

  static func registerRuntimeSupervisor() {
    _ = kernel_supervisor_register_task(TASK_FAST_ID, 1000, KERNEL_SUPERVISOR_POLICY_OBSERVE)
    _ = kernel_supervisor_register_task(TASK_SLOW_ID, 3000, KERNEL_SUPERVISOR_POLICY_OBSERVE)
    _ = kernel_supervisor_register_task(TASK_LONG_ID, 5000, KERNEL_SUPERVISOR_POLICY_OBSERVE)
    _ = kernel_supervisor_register_task(TASK_MAIL_TX_ID, 3000, KERNEL_SUPERVISOR_POLICY_OBSERVE)
    _ = kernel_supervisor_register_task(TASK_MAIL_RX_ID, 3000, KERNEL_SUPERVISOR_POLICY_OBSERVE)
    _ = kernel_supervisor_register_task(TASK_SHELL_ID, 0, KERNEL_SUPERVISOR_POLICY_OBSERVE)
    kernel_event_emit(KERNEL_EVENT_KIND_SUPERVISOR, UInt(kernel_supervisor_count()), 0, 0)
  }

  static func mailboxProducer() async {
    var n: UInt64 = 0
    while true {
      kernel_task_mark_state(TASK_MAIL_TX_ID, KERNEL_TASK_STATE_RUNNING)
      if kernel_mailbox_send_u64(MAILBOX_DEMO_ID, UInt(n)) != 0 {
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
    while true {
      kernel_task_mark_state(TASK_MAIL_RX_ID, KERNEL_TASK_STATE_WAITING)
      let value = await mailboxReceiveU64(MAILBOX_DEMO_ID)
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
    // log for kernel/agent observability.
    kernel_memory_init()
    kernel_event_log_init()
    kernel_event_emit(KERNEL_EVENT_KIND_BOOT, 16, 0, 0)
    kernel_object_registry_init()
    kernel_task_registry_init()
    kernel_supervisor_init()
    kernel_mailbox_registry_init()
    registerRuntimeMailboxes()
    registerRuntimeTasks()
    registerRuntimeSupervisor()
    uart_rx_irq_init()
    gicInitRuntimeIRQs()
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
    Task { await fastHeartbeat() }
    Task { await slowHeartbeat() }
    Task { await longHeartbeat() }
    Task { await mailboxProducer() }
    Task { await mailboxConsumer() }
    startUartShellTask()
    irq_enable()
    swift_task_asyncMainDrainQueue()
  }
}

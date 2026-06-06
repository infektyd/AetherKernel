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

let TASK_FAST_ID: UInt32 = 0
let TASK_SLOW_ID: UInt32 = 1
let TASK_LONG_ID: UInt32 = 2
let TASK_SHELL_ID: UInt32 = 3

func registerKernelTask(_ taskID: UInt32, _ name: StaticString, _ periodMS: UInt32) {
  _ = kernel_task_register(taskID, name.utf8Start, UInt32(name.utf8CodeUnitCount), periodMS)
}

@main
struct Application {
  static func fastHeartbeat() async {
    var n: UInt64 = 0
    while true {
      kernel_task_mark_state(TASK_FAST_ID, KERNEL_TASK_STATE_RUNNING)
      runtimeFastCount = n
      kernel_task_record_tick(TASK_FAST_ID)
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
    kernel_memory_init()
    kernel_object_registry_init()
    kernel_task_registry_init()
    registerRuntimeTasks()
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
    printBootcheck()
    Task { await fastHeartbeat() }
    Task { await slowHeartbeat() }
    Task { await longHeartbeat() }
    startUartShellTask()
    irq_enable()
    swift_task_asyncMainDrainQueue()
  }
}

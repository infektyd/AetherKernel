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

@main
struct Application {
  static func fastHeartbeat() async {
    var n: UInt64 = 0
    while true {
      runtimeFastCount = n
      uartPuts("rtv2 fast ")
      uartPutHex(n)
      uartPuts("\n")
      await timerSleepMillis(250)
      n &+= 1
    }
  }

  static func slowHeartbeat() async {
    var n: UInt64 = 0
    while true {
      runtimeSlowCount = n
      uartPuts("rtv2 slow ")
      uartPutHex(n)
      uartPuts("\n")
      await timerSleepSeconds(1)
      n &+= 1
    }
  }

  static func longHeartbeat() async {
    var n: UInt64 = 0
    while true {
      runtimeLongCount = n
      uartPuts("rtv2 long ")
      uartPutHex(n)
      uartPuts("\n")
      await timerSleepSeconds(2)
      n &+= 1
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
    // exposes diagnostics over that same command surface.
    uart_rx_irq_init()
    gicInitRuntimeIRQs()
    uart_rx_irq_enable()
    uartPuts("runtime v2: shared CNTP timer arbiter, multi-task async sleep\n")
    uartPuts("runtime v4: irq-backed uart shell\n")
    uartPuts("runtime v5: diagnostics shell\n")
    Task { await fastHeartbeat() }
    Task { await slowHeartbeat() }
    Task { await longHeartbeat() }
    startUartShellTask()
    irq_enable()
    swift_task_asyncMainDrainQueue()
  }
}

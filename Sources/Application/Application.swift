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

@main
struct Application {
  static func serviceSerialCommands() {
    while let b = uartTryReadByte() {
      if b == 0x72 || b == 0x52 {  // r/R
        uartPuts("serial reset command: watchdog reboot\n")
        watchdog_reset_now()
        while true { wait_for_interrupt() }
      }
    }
  }

  static func fastHeartbeat() async {
    var n: UInt64 = 0
    while true {
      uartPuts("rtv2 fast ")
      uartPutHex(n)
      uartPuts("\n")
      serviceSerialCommands()
      await timerSleepMillis(250)
      n &+= 1
    }
  }

  static func slowHeartbeat() async {
    var n: UInt64 = 0
    while true {
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
    // arbiter. The custom continuation sleep queue and the executor delay queue
    // share one hardware timer while the CPU idles in wfi between jobs.
    gicInitTimerIRQ()
    uartPuts("runtime v2: shared CNTP timer arbiter, multi-task async sleep\n")
    Task { await fastHeartbeat() }
    Task { await slowHeartbeat() }
    Task { await longHeartbeat() }
    irq_enable()
    swift_task_asyncMainDrainQueue()
  }
}

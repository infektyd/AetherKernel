//===----------------------------------------------------------------------===//
// AetherKernel entry point.
//
// boot.S parks the secondary cores, drops EL2->EL1, enables FP/SIMD (CPACR) and
// the MMU (Normal cacheable RAM, required for the concurrency runtime's atomics),
// sets the stack + EL1 vectors, and `bl _main` into this @main. From here we run
// a real Swift async/await heartbeat on a custom cooperative executor.
//===----------------------------------------------------------------------===//
import Support
import _Concurrency

@main
struct Application {
  static func main() {
    uartInit()
    uartPuts("\n=== AetherKernel ===\n")
    uartPuts("Embedded Swift 6.3.2 - bare-metal Raspberry Pi 4B (BCM2711)\n")
    uartPuts("PL011 UART0 @ 0xFE201000 online. Hello from the metal!\n")

    // Prove which exception level the firmware dropped us into.
    // CurrentEL holds the level in bits [3:2]; EL1 reads back as 0x4.
    uartPuts("CurrentEL = "); uartPutHex(UInt64(read_currentel())); uartPuts("\n")
    uartPuts("CNTFRQ = "); uartPutHex(UInt64(timerFrequency())); uartPuts(" Hz (generic timer)\n")

    // A real Swift async/await heartbeat on our cooperative executor: the task
    // prints once a second and suspends on a timer-backed sleep; the CNTP timer
    // IRQ resumes the continuation, and the CPU idles in wfi in between.
    gicInitTimerIRQ()
    uartPuts("async heartbeat: timer-backed sleep 1s, wfi idle\n")
    Task {
      var n: UInt64 = 0
      while true {
        uartPuts("async tick ")
        uartPutHex(n)
        uartPuts("\n")
        await timerSleepSeconds(1)
        n &+= 1
      }
    }
    irq_enable()
    swift_task_asyncMainDrainQueue()
  }
}

//===----------------------------------------------------------------------===//
// AetherKernel entry point.
//
// boot.S parks the secondary cores, zeroes BSS, sets the stack, and `bl main`
// into this @main. We currently run in whatever EL the firmware hands off
// (EL2 on the stock Pi 4 armstub). The EL2->EL1 drop is the next milestone —
// added once UART gives us an output channel to prove it.
//===----------------------------------------------------------------------===//
import Support
import _Concurrency

@main
struct Application {
  static func main() {
    uartInit()
    uartPuts("\n=== AetherKernel ===\n")
    uartPuts("Embedded Swift 6.0 - bare-metal Raspberry Pi 4B (BCM2711)\n")
    uartPuts("PL011 UART0 @ 0xFE201000 online. Hello from the metal!\n")

    // Prove which exception level the firmware dropped us into.
    // CurrentEL holds the level in bits [3:2], so EL2 reads back as 0x8.
    uartPuts("CurrentEL = ")
    uartPutHex(UInt64(read_currentel()))
    uartPuts("\n")

    // Report the generic timer frequency.
    uartPuts("CNTFRQ = ")
    uartPutHex(UInt64(timerFrequency()))
    uartPuts(" Hz (generic timer)\n")

    // PROBE (temporary): prove the allocator before the executor depends on it.
    let a = malloc(64); let b = malloc(64)
    uartPuts("malloc a="); uartPutHex(UInt64(UInt(bitPattern: a)))   // expect ~0x400000+
    uartPuts(" b=");        uartPutHex(UInt64(UInt(bitPattern: b)))   // expect != a, in-window
    free(a)
    let c = malloc(64)                                               // expect to reuse a's slot
    uartPuts(" c(after free a)="); uartPutHex(UInt64(UInt(bitPattern: c))); uartPuts("\n")
    // posix_memalign 4096-aligned:
    var p: UnsafeMutableRawPointer? = nil
    let r = posix_memalign(&p, 4096, 256)
    uartPuts("memalign r="); uartPutHex(UInt64(UInt(r))); uartPuts(" p=")
    uartPutHex(UInt64(UInt(bitPattern: p))); uartPuts(" (low12 must be 0)\n")

    // MS4 Stage 3: timer-backed async heartbeat. Route the CNTP timer IRQ through
    // the GIC, then run an async task that prints once per second and sleeps in
    // between — the CPU idles in wfi until the timer IRQ resumes the continuation.
    gicInitTimerIRQ()
    uartPuts("MS4 Stage 3: async heartbeat (timer-backed sleep 1s, wfi idle)\n")
    Task {
      var n: UInt64 = 0
      while true {
        uartPuts("async tick ")
        uartPutHex(n)
        uartPuts("\n")
        n &+= 1
        if n == 5 {
          // Watchdog self-test: reboot after 5 ticks. The banner reappearing
          // proves the BCM2711 watchdog reset. Spin ~20 ms first so the UART
          // FIFO drains before the SoC resets (else the marker gets cut off).
          uartPuts("watchdog: reboot now\n")
          let t0 = read_cntpct()
          while (read_cntpct() &- t0) < (read_cntfrq() / 50) { nop() }
          watchdog_reset_now()
        }
        await timerSleepSeconds(1)
      }
    }
    irq_enable()
    swift_task_asyncMainDrainQueue()
  }
}


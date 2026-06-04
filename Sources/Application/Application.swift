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

    // MS4 Stage 2: bootstrap the cooperative executor (executor.c). Create one
    // async Task, then hand the CPU to the drain pump (never returns). No timer
    // is armed yet — the task runs on the first drain iteration and the pump then
    // idles in wfi; timer-backed Task.sleep arrives in Stage 3.
    ledInit()
    uartPuts("MS4 Stage 2: creating Task...\n")
    Task {
      uartPuts("[task] hello from async/await on the metal\n")
    }
    uartPuts("MS4 Stage 2: entering drain pump (wfi when idle)...\n")
    swift_task_asyncMainDrainQueue()
  }
}


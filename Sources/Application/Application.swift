//===----------------------------------------------------------------------===//
// AetherKernel entry point.
//
// boot.S parks the secondary cores, zeroes BSS, sets the stack, and `bl main`
// into this @main. We currently run in whatever EL the firmware hands off
// (EL2 on the stock Pi 4 armstub). The EL2->EL1 drop is the next milestone —
// added once UART gives us an output channel to prove it.
//===----------------------------------------------------------------------===//
import Support

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

    // Heartbeat: one tick per real second, paced by the hardware timer
    // (polled CNTP), toggling the ACT LED each second. The ~ms of UART print
    // time per tick is the only drift; this is a real clock, not a spin count.
    ledInit()
    gicInitTimerIRQ()
    timerArmIRQ(1)
    uartPuts("IRQ mode: GIC-400 routing CNTP (INTID 30). Idling in wfi.\n")
    irq_enable()
    while true { wait_for_interrupt() }
  }
}


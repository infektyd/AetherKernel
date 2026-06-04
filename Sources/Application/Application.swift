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

    // Heartbeat: one tick per real second, paced by the hardware timer
    // (polled CNTP), toggling the ACT LED each second. The ~ms of UART print
    // time per tick is the only drift; this is a real clock, not a spin count.
    ledInit()
    var ticks: UInt64 = 0
    var ledOnState = false
    while true {
      timerWaitSeconds(1)
      ledOnState.toggle()
      if ledOnState { ledOn() } else { ledOff() }
      uartPuts("tick ")
      uartPutHex(ticks)
      uartPuts("\n")
      ticks &+= 1
    }
  }
}

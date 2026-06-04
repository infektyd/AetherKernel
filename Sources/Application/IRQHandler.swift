//===----------------------------------------------------------------------===//
// IRQ exception handler.
//===----------------------------------------------------------------------===//
import Support

nonisolated(unsafe) var irqTicks: UInt64 = 0   // only touched in IRQ context (single core)

@_cdecl("irq_handler")
func irqHandler() {
  let iar = gicAck()
  let intid = iar & 0x3FF                  // low 10 bits = INTID
  if intid == 1022 || intid == 1023 { return }  // spurious (1023) / secure-we-can't-ack (1022): NO EOI
  if intid == 30 {                         // CNTP timer
    timerArmIRQ(1)                          // re-arm FIRST — de-asserts the level IRQ before EOI
    if (irqTicks & 1) == 0 { ledOn() } else { ledOff() }
    uartPuts("irq ")
    uartPutHex(irqTicks)
    uartPuts("\n")
    irqTicks &+= 1
  }
  gicEoi(iar)                              // EOI with the exact IAR value (AFTER clearing the timer)
}

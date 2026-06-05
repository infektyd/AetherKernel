//===----------------------------------------------------------------------===//
// IRQ exception handler.
//===----------------------------------------------------------------------===//
import Support

@_cdecl("irq_handler")
func irqHandler() {
  let iar = gicAck()
  let intid = iar & 0x3FF                  // low 10 bits = INTID
  if intid == 1022 || intid == 1023 { return }  // spurious (1023) / secure-we-can't-ack (1022): NO EOI
  if intid == 30 {                         // CNTP timer
    serviceTimerSleepers()                 // resume due sleep continuations; re-arms shared CNTP
    executor_on_timer_irq()                // promote due executor-delayed jobs on the same timer
  }
  gicEoi(iar)                              // EOI with the exact IAR value (AFTER clearing the timer)
}

//===----------------------------------------------------------------------===//
// IRQ exception handler.
//===----------------------------------------------------------------------===//
import Support

@_cdecl("irq_handler")
func irqHandler() {
  let iar = gicAck()
  let intid = iar & 0x3FF                  // low 10 bits = INTID
  kernel_irq_record(intid)
  if intid == 1022 || intid == 1023 { return }  // spurious (1023) / secure-we-can't-ack (1022): NO EOI
  if intid == CNTP_GIC_INTID {             // CNTP timer
    serviceTimerSleepers()                 // resume due sleep continuations; re-arms shared CNTP
    executor_on_timer_irq()                // promote due executor-delayed jobs on the same timer
  } else if intid == UART0_GIC_INTID {     // PL011 UART0 RX
    serviceUartRxIrq()
  }
  gicEoi(iar)                              // EOI with the exact IAR value (AFTER clearing the timer)
}

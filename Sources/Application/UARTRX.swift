//===----------------------------------------------------------------------===//
// Runtime V4 UART RX async bridge.
//
// PL011 IRQ handling and byte storage live in C. Swift owns the single shell
// waiter and resumes it after the IRQ service drains RX bytes into the ring.
//===----------------------------------------------------------------------===//
import Support
import _Concurrency

nonisolated(unsafe) var uartRxWaiter: UnsafeContinuation<UInt8, Never>? = nil

func uartReadByteAsync() async -> UInt8 {
  var raw: UInt32 = 0
  if uart_rx_ring_read_byte(&raw) != 0 {
    return UInt8(raw & 0xFF)
  }

  return await withUnsafeContinuation { (c: UnsafeContinuation<UInt8, Never>) in
    let flags = irq_save()
    if uart_rx_ring_read_byte(&raw) != 0 {
      irq_restore(flags)
      c.resume(returning: UInt8(raw & 0xFF))
    } else if uartRxWaiter != nil {
      irq_restore(flags)
      uartPuts("UART RX PANIC: waiter already active\n")
      while true { wait_for_interrupt() }
    } else {
      uartRxWaiter = c
      irq_restore(flags)
    }
  }
}

func serviceUartRxIrq() {
  uart_rx_irq_service()

  var raw: UInt32 = 0
  var ready: UnsafeContinuation<UInt8, Never>? = nil
  var byte: UInt8 = 0

  let flags = irq_save()
  if let c = uartRxWaiter {
    if uart_rx_ring_read_byte(&raw) != 0 {
      uartRxWaiter = nil
      ready = c
      byte = UInt8(raw & 0xFF)
    }
  }
  irq_restore(flags)

  if let c = ready {
    c.resume(returning: byte)
  }
}

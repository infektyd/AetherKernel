//===----------------------------------------------------------------------===//
// Runtime V6 UART shell.
//
// Line-oriented ASCII command surface over the IRQ-backed PL011 RX path. The
// shell awaits bytes from UARTRX.swift instead of polling the UART FIFO. V5 adds
// diagnostics commands that expose kernel pressure and fault signals; V6 adds
// retained panic/fault records across watchdog reset.
//===----------------------------------------------------------------------===//
import Support
import _Concurrency

let UART_SHELL_BUFFER_CAPACITY: Int = 80
nonisolated(unsafe) var resetAliasCheckScheduled: Bool = false

func isResetAlias(_ b: UInt8) -> Bool {
  b == 0x72 || b == 0x52
}

func shellBufferEquals(_ s: StaticString) -> Bool {
  let n = uart_shell_buffer_count()
  if n != UInt32(s.utf8CodeUnitCount) {
    return false
  }

  let p = s.utf8Start
  var i: UInt32 = 0
  while i < n {
    if UInt8(uart_shell_buffer_get(i) & 0xFF) != p[Int(i)] {
      return false
    }
    i += 1
  }
  return true
}

func printShellReady() {
  uartPuts("shell ready commands=help,status,heap,queues,tasks,diag,irqs,timers,memcheck,faults,retained,retained-clear,panic-test,fault-test,reboot\n")
}

func printShellHelp() {
  uartPuts("shell help commands=help,status,heap,queues,tasks,diag,irqs,timers,memcheck,faults,retained,retained-clear,panic-test,fault-test,reboot\n")
}

func printStatus() {
  let uptime = (UInt64(kernel_timer_now()) &* 1000) / UInt64(timerFrequency())

  uartPuts("status uptime_ms=")
  uartPutDec(uptime)
  uartPuts(" fast=")
  uartPutDec(runtimeFastCount)
  uartPuts(" slow=")
  uartPutDec(runtimeSlowCount)
  uartPuts(" long=")
  uartPutDec(runtimeLongCount)
  uartPuts(" heap_free=")
  uartPutDec(UInt64(heap_free_bytes()))
  uartPuts(" ready=")
  uartPutDec(UInt64(executor_ready_count()))
  uartPuts(" delayed=")
  uartPutDec(UInt64(executor_delayed_count()))
  uartPuts(" sleepers=")
  uartPutDec(UInt64(timerSleepPendingCount()))
  uartPuts(" timer_mask=")
  uartPutHexCompact(UInt64(kernel_timer_active_mask()))
  uartPuts("\n")
}

func printHeap() {
  uartPuts("heap total=")
  uartPutDec(UInt64(heap_total_bytes()))
  uartPuts(" free=")
  uartPutDec(UInt64(heap_free_bytes()))
  uartPuts(" largest=")
  uartPutDec(UInt64(heap_largest_free_bytes()))
  uartPuts(" allocated=")
  uartPutDec(UInt64(heap_allocated_bytes()))
  uartPuts(" high_water=")
  uartPutDec(UInt64(heap_high_water_bytes()))
  uartPuts(" failed_allocs=")
  uartPutDec(UInt64(heap_failed_alloc_count()))
  uartPuts(" mallocs=")
  uartPutDec(UInt64(heap_malloc_count()))
  uartPuts(" frees=")
  uartPutDec(UInt64(heap_free_count()))
  uartPuts(" reallocs=")
  uartPutDec(UInt64(heap_realloc_count()))
  uartPuts(" callocs=")
  uartPutDec(UInt64(heap_calloc_count()))
  uartPuts("\n")
}

func printQueues() {
  uartPuts("queues ready=")
  uartPutDec(UInt64(executor_ready_count()))
  uartPuts("/")
  uartPutDec(UInt64(executor_ready_capacity()))
  uartPuts(" delayed=")
  uartPutDec(UInt64(executor_delayed_count()))
  uartPuts("/")
  uartPutDec(UInt64(executor_delayed_capacity()))
  uartPuts(" sleepers=")
  uartPutDec(UInt64(timerSleepPendingCount()))
  uartPuts("/")
  uartPutDec(UInt64(timerSleepCapacity()))
  uartPuts(" timer_mask=")
  uartPutHexCompact(UInt64(kernel_timer_active_mask()))
  uartPuts("\n")
}

func printTasks() {
  uartPuts("task fast count=")
  uartPutDec(runtimeFastCount)
  uartPuts(" period_ms=250\n")
  uartPuts("task slow count=")
  uartPutDec(runtimeSlowCount)
  uartPuts(" period_ms=1000\n")
  uartPuts("task long count=")
  uartPutDec(runtimeLongCount)
  uartPuts(" period_ms=2000\n")
}

func printDiag() {
  let uptime = (UInt64(kernel_timer_now()) &* 1000) / UInt64(timerFrequency())
  let heapOK = heap_integrity_check() != 0

  uartPuts("diag version=v5 uptime_ms=")
  uartPutDec(uptime)
  uartPuts(" heap_ok=")
  uartPutDec(UInt64(heapOK ? 1 : 0))
  uartPuts(" heap_free=")
  uartPutDec(UInt64(heap_free_bytes()))
  uartPuts(" heap_high_water=")
  uartPutDec(UInt64(heap_high_water_bytes()))
  uartPuts(" heap_failed_allocs=")
  uartPutDec(UInt64(heap_failed_alloc_count()))
  uartPuts(" irq_total=")
  uartPutDec(UInt64(kernel_irq_total_count()))
  uartPuts(" irq_unknown=")
  uartPutDec(UInt64(kernel_irq_unknown_count()))
  uartPuts(" uart_rx_overflows=")
  uartPutDec(UInt64(uart_rx_overflow_count()))
  uartPuts("\n")
}

func printIrqs() {
  uartPuts("irqs total=")
  uartPutDec(UInt64(kernel_irq_total_count()))
  uartPuts(" cntp=")
  uartPutDec(UInt64(kernel_irq_cntp_count()))
  uartPuts(" uart0=")
  uartPutDec(UInt64(kernel_irq_uart0_count()))
  uartPuts(" spurious=")
  uartPutDec(UInt64(kernel_irq_spurious_count()))
  uartPuts(" unknown=")
  uartPutDec(UInt64(kernel_irq_unknown_count()))
  uartPuts("\n")
}

func printTimers() {
  uartPuts("timers now=")
  uartPutDec(UInt64(kernel_timer_now()))
  uartPuts(" freq=")
  uartPutDec(UInt64(timerFrequency()))
  uartPuts(" active_count=")
  uartPutDec(UInt64(kernel_timer_active_count()))
  uartPuts(" active_mask=")
  uartPutHexCompact(UInt64(kernel_timer_active_mask()))
  uartPuts(" sleep_deadline=")
  uartPutDec(UInt64(kernel_timer_deadline_ticks(KERNEL_TIMER_CLIENT_SLEEP)))
  uartPuts(" executor_deadline=")
  uartPutDec(UInt64(kernel_timer_deadline_ticks(KERNEL_TIMER_CLIENT_EXECUTOR)))
  uartPuts("\n")
}

func printMemcheck() {
  let ok = heap_integrity_check() != 0

  uartPuts("memcheck ok=")
  uartPutDec(UInt64(ok ? 1 : 0))
  uartPuts(" total=")
  uartPutDec(UInt64(heap_total_bytes()))
  uartPuts(" free=")
  uartPutDec(UInt64(heap_free_bytes()))
  uartPuts(" largest=")
  uartPutDec(UInt64(heap_largest_free_bytes()))
  uartPuts(" allocated=")
  uartPutDec(UInt64(heap_allocated_bytes()))
  uartPuts(" high_water=")
  uartPutDec(UInt64(heap_high_water_bytes()))
  uartPuts(" failed_allocs=")
  uartPutDec(UInt64(heap_failed_alloc_count()))
  uartPuts("\n")
}

func printFaults() {
  uartPuts("faults seen=")
  uartPutDec(UInt64(kernel_fault_seen()))
  uartPuts(" panic_seen=")
  uartPutDec(UInt64(kernel_panic_seen()))
  uartPuts(" esr=")
  uartPutHexCompact(UInt64(kernel_fault_esr()))
  uartPuts(" elr=")
  uartPutHexCompact(UInt64(kernel_fault_elr()))
  uartPuts(" far=")
  uartPutHexCompact(UInt64(kernel_fault_far()))
  uartPuts("\n")
}

func printRetainedKind(_ kind: UInt32) {
  if kind == KERNEL_RETAINED_KIND_PANIC {
    uartPuts("panic")
  } else if kind == KERNEL_RETAINED_KIND_FAULT {
    uartPuts("fault")
  } else {
    uartPuts("none")
  }
}

func printRetainedReason() {
  var i: UInt32 = 0
  let n = kernel_retained_reason_len()
  while i < n {
    let b = UInt8(kernel_retained_reason_byte(i) & 0xFF)
    if b >= 0x20 && b < 0x7F {
      uartPutc(b)
    } else {
      uartPutc(0x2E)
    }
    i += 1
  }
}

func printRetained() {
  let valid = kernel_retained_valid()
  let kind = kernel_retained_kind()

  uartPuts("retained valid=")
  uartPutDec(UInt64(valid))
  uartPuts(" kind=")
  printRetainedKind(kind)
  uartPuts(" seq=")
  uartPutDec(UInt64(kernel_retained_sequence()))
  uartPuts(" esr=")
  uartPutHexCompact(UInt64(kernel_retained_esr()))
  uartPuts(" elr=")
  uartPutHexCompact(UInt64(kernel_retained_elr()))
  uartPuts(" far=")
  uartPutHexCompact(UInt64(kernel_retained_far()))
  uartPuts(" reason=")
  printRetainedReason()
  uartPuts("\n")
}

func clearRetained() {
  kernel_retained_clear()
  uartPuts("retained clear ok=1\n")
}

func shellPanicTest() {
  uartPuts("shell panic-test reason=command\n")
  uartDrainTx()
  kernel_panic_test()
  while true { wait_for_interrupt() }
}

func shellFaultTest() {
  uartPuts("shell fault-test reason=command\n")
  uartDrainTx()
  kernel_trigger_sync_fault()
  while true { wait_for_interrupt() }
}

func shellReboot(_ reason: StaticString) {
  uartPuts("shell reboot reason=")
  uartPuts(reason)
  uartPuts("\n")
  uartDrainTx()
  watchdog_reset_now()
  while true { wait_for_interrupt() }
}

func shellRebootCommand() {
  uartPuts("shell reboot reason=command\n")
  uartDrainTx()
  watchdog_reset_now()
  while true { wait_for_interrupt() }
}

func resetAliasQuietWindow() async {
  await timerSleepMillis(20)

  var shouldReset = false
  let flags = irq_save()
  if uart_shell_buffer_count() == 1 {
    let b = UInt8(uart_shell_buffer_get(0) & 0xFF)
    shouldReset = isResetAlias(b)
  }
  resetAliasCheckScheduled = false
  irq_restore(flags)

  if shouldReset {
    shellReboot("alias")
  }
}

func scheduleResetAliasCheckIfNeeded() {
  if resetAliasCheckScheduled {
    return
  }
  resetAliasCheckScheduled = true
  Task { await resetAliasQuietWindow() }
}

func processUartShellLine() {
  let n = uart_shell_buffer_count()
  if n == 0 {
    return
  }

  if n == 1 && isResetAlias(UInt8(uart_shell_buffer_get(0) & 0xFF)) {
    shellReboot("alias")
  } else if shellBufferEquals("help") {
    printShellHelp()
  } else if shellBufferEquals("status") {
    printStatus()
  } else if shellBufferEquals("heap") {
    printHeap()
  } else if shellBufferEquals("queues") {
    printQueues()
  } else if shellBufferEquals("tasks") {
    printTasks()
  } else if shellBufferEquals("diag") {
    printDiag()
  } else if shellBufferEquals("irqs") {
    printIrqs()
  } else if shellBufferEquals("timers") {
    printTimers()
  } else if shellBufferEquals("memcheck") {
    printMemcheck()
  } else if shellBufferEquals("faults") {
    printFaults()
  } else if shellBufferEquals("retained") {
    printRetained()
  } else if shellBufferEquals("retained-clear") {
    clearRetained()
  } else if shellBufferEquals("panic-test") {
    shellPanicTest()
  } else if shellBufferEquals("fault-test") {
    shellFaultTest()
  } else if shellBufferEquals("reboot") {
    shellRebootCommand()
  } else {
    uartPuts("shell error reason=unknown command=")
    uartPutByteStringFromShellBuffer()
    uartPuts("\n")
  }
}

func processUartShellByte(_ b: UInt8) {
  if b == 0x0A || b == 0x0D {
    processUartShellLine()
    uart_shell_buffer_clear()
  } else if uart_shell_buffer_append(UInt32(b)) == 0 {
    uartPuts("shell error reason=line_too_long\n")
    uart_shell_buffer_clear()
  } else if uart_shell_buffer_count() == 1 && isResetAlias(b) {
    scheduleResetAliasCheckIfNeeded()
  }
}

func uartShellMain() async {
  printShellReady()
  while true {
    let b = await uartReadByteAsync()
    processUartShellByte(b)
  }
}

func startUartShellTask() {
  uart_shell_buffer_clear()
  Task { await uartShellMain() }
}

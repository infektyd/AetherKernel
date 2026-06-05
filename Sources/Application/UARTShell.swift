//===----------------------------------------------------------------------===//
// Runtime V3 UART shell.
//
// Line-oriented ASCII command surface over the existing PL011 RX path. This is
// intentionally polled from an async task for V3; UART RX interrupts become the
// next driver-layer milestone.
//===----------------------------------------------------------------------===//
import Support
import _Concurrency

let UART_SHELL_BUFFER_CAPACITY: Int = 80

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
  uartPuts("shell ready commands=help,status,heap,queues,tasks,reboot\n")
}

func printShellHelp() {
  uartPuts("shell help commands=help,status,heap,queues,tasks,reboot\n")
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

func shellReboot(_ reason: StaticString) {
  uartPuts("shell reboot reason=")
  uartPuts(reason)
  uartPuts("\n")
  watchdog_reset_now()
  while true { wait_for_interrupt() }
}

func shellRebootCommand() {
  uartPuts("shell reboot reason=command\n")
  watchdog_reset_now()
  while true { wait_for_interrupt() }
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
  } else if shellBufferEquals("reboot") {
    shellRebootCommand()
  } else {
    uartPuts("shell error reason=unknown command=")
    uartPutByteStringFromShellBuffer()
    uartPuts("\n")
  }
}

func pollUartShell() {
  var readAny = false

  while let b = uartTryReadByte() {
    readAny = true

    if b == 0x0A || b == 0x0D {
      processUartShellLine()
      uart_shell_buffer_clear()
    } else if uart_shell_buffer_append(UInt32(b)) == 0 {
      uartPuts("shell error reason=line_too_long\n")
      uart_shell_buffer_clear()
    }
  }

  if !readAny && uart_shell_buffer_count() == 1 {
    let b = UInt8(uart_shell_buffer_get(0) & 0xFF)
    if isResetAlias(b) {
      shellReboot("alias")
    }
  }
}

func uartShellMain() async {
  printShellReady()
  while true {
    pollUartShell()
    await timerSleepMillis(25)
  }
}

func startUartShellTask() {
  uart_shell_buffer_clear()
  Task { await uartShellMain() }
}

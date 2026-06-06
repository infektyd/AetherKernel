//===----------------------------------------------------------------------===//
// Runtime V11 UART shell.
//
// Line-oriented ASCII command surface over the IRQ-backed PL011 RX path. The
// shell awaits bytes from UARTRX.swift instead of polling the UART FIFO. V5 adds
// diagnostics commands that expose kernel pressure and fault signals; V6 adds
// retained panic/fault records across watchdog reset. V7 adds memory ownership
// and frame allocator inspection. V8 adds allocator guard/status self-checks.
// V9 adds bounded heap/frame pressure tests. V10 adds explicit guard probes.
// V11 adds boot and soak invariant checks for host-side proof loops. V12 adds a
// fixed kernel object table and cooperative task registry. V13 adds bounded
// mailbox message queues. V14 adds a deterministic task supervisor. V15 adds
// capability-tagged kernel object handles. V16 adds a fixed event log ring.
//===----------------------------------------------------------------------===//
import Support
import _Concurrency

let UART_SHELL_BUFFER_CAPACITY: Int = 80
let SOAK_ROUNDS: UInt32 = 3
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
  uartPuts("shell ready commands=help,status,heap,queues,tasks,tasks2,kobjects,mailboxes,sendtest,supervisor,health,capcheck,events,diag,irqs,timers,memcheck,faults,retained,retained-clear,memmap,frames,heapcheck,framecheck,stress,frameprobe,bootcheck,soak,heap-invalid-free-test,heap-double-free-test,panic-test,fault-test,reboot\n")
}

func printShellHelp() {
  uartPuts("shell help commands=help,status,heap,queues,tasks,tasks2,kobjects,mailboxes,sendtest,supervisor,health,capcheck,events,diag,irqs,timers,memcheck,faults,retained,retained-clear,memmap,frames,heapcheck,framecheck,stress,frameprobe,bootcheck,soak,heap-invalid-free-test,heap-double-free-test,panic-test,fault-test,reboot\n")
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

func printKernelObjectKind(_ kind: UInt32) {
  if kind == KERNEL_OBJECT_KIND_TASK {
    uartPuts("task")
  } else if kind == KERNEL_OBJECT_KIND_DRIVER {
    uartPuts("driver")
  } else if kind == KERNEL_OBJECT_KIND_RUNTIME {
    uartPuts("runtime")
  } else if kind == KERNEL_OBJECT_KIND_MAILBOX {
    uartPuts("mailbox")
  } else {
    uartPuts("unknown")
  }
}

func printKernelObjectName(_ index: UInt32) {
  var i: UInt32 = 0
  let n = kernel_object_name_len(index)
  while i < n {
    let b = UInt8(kernel_object_name_byte(index, i) & 0xFF)
    if b >= 0x20 && b < 0x7F {
      uartPutc(b)
    } else {
      uartPutc(0x2E)
    }
    i += 1
  }
}

func printTaskState(_ state: UInt32) {
  if state == KERNEL_TASK_STATE_RUNNING {
    uartPuts("running")
  } else if state == KERNEL_TASK_STATE_WAITING {
    uartPuts("waiting")
  } else {
    uartPuts("idle")
  }
}

func printTaskName(_ task: UInt32) {
  var i: UInt32 = 0
  let n = kernel_task_name_len(task)
  while i < n {
    let b = UInt8(kernel_task_name_byte(task, i) & 0xFF)
    if b >= 0x20 && b < 0x7F {
      uartPutc(b)
    } else {
      uartPutc(0x2E)
    }
    i += 1
  }
}

func printKobjects() {
  let count = kernel_object_count()
  let handleSelftest = kernel_object_handle_selftest()
  let capSelftest = kernel_object_capcheck_selftest()

  uartPuts("kobjects count=")
  uartPutDec(UInt64(count))
  uartPuts(" capacity=")
  uartPutDec(UInt64(kernel_object_capacity()))
  uartPuts(" active=")
  uartPutDec(UInt64(kernel_object_active_count()))
  uartPuts(" selftest=")
  uartPutDec(UInt64(kernel_object_registry_selftest()))
  uartPuts(" handle_selftest=")
  uartPutDec(UInt64(handleSelftest))
  uartPuts(" cap_selftest=")
  uartPutDec(UInt64(capSelftest))
  uartPuts("\n")

  var i: UInt32 = 0
  while i < kernel_object_capacity() {
    let id = kernel_object_id(i)
    if id != 0 {
      let caps = kernel_object_caps(i)
      let handle = kernel_object_make_handle(i, caps)
      uartPuts(" object index=")
      uartPutDec(UInt64(i))
      uartPuts(" id=")
      uartPutDec(UInt64(id))
      uartPuts(" handle=")
      uartPutHex(UInt64(handle))
      uartPuts(" generation=")
      uartPutDec(UInt64(kernel_object_generation(i)))
      uartPuts(" kind=")
      printKernelObjectKind(kernel_object_kind(i))
      uartPuts(" flags=")
      uartPutHexCompact(UInt64(kernel_object_flags(i)))
      uartPuts(" caps=")
      uartPutHexCompact(UInt64(caps))
      uartPuts(" name=")
      printKernelObjectName(i)
      uartPuts("\n")
    }
    i += 1
  }
}

func printCapcheck() {
  let inspectHandle = kernel_object_make_handle(0, KERNEL_OBJECT_CAP_INSPECT)
  let inspect = inspectHandle != KERNEL_OBJECT_HANDLE_INVALID &&
    kernel_object_lookup_id(inspectHandle, KERNEL_OBJECT_CAP_INSPECT) != 0 &&
    kernel_object_handle_last_error() == KERNEL_OBJECT_LOOKUP_OK
  let denied = kernel_object_capcheck_selftest() != 0
  let stale = kernel_object_handle_selftest() != 0
  let ok = inspect && denied && stale

  uartPuts("capcheck ok=")
  uartPutDec(UInt64(ok ? 1 : 0))
  uartPuts(" inspect=")
  uartPutDec(UInt64(inspect ? 1 : 0))
  uartPuts(" denied=")
  uartPutDec(UInt64(denied ? 1 : 0))
  uartPuts(" stale=")
  uartPutDec(UInt64(stale ? 1 : 0))
  uartPuts(" last_error=")
  uartPutDec(UInt64(kernel_object_handle_last_error()))
  uartPuts("\n")
}

func printEventKind(_ kind: UInt32) {
  if kind == KERNEL_EVENT_KIND_BOOT {
    uartPuts("boot")
  } else if kind == KERNEL_EVENT_KIND_TASK {
    uartPuts("task")
  } else if kind == KERNEL_EVENT_KIND_TIMER {
    uartPuts("timer")
  } else if kind == KERNEL_EVENT_KIND_MAILBOX {
    uartPuts("mailbox")
  } else if kind == KERNEL_EVENT_KIND_SUPERVISOR {
    uartPuts("supervisor")
  } else if kind == KERNEL_EVENT_KIND_SHELL {
    uartPuts("shell")
  } else if kind == KERNEL_EVENT_KIND_HANDLE {
    uartPuts("handle")
  } else if kind == KERNEL_EVENT_KIND_SELFTEST {
    uartPuts("selftest")
  } else {
    uartPuts("unknown")
  }
}

func printEvents() {
  kernel_event_emit(KERNEL_EVENT_KIND_SHELL, 16, UInt(kernel_event_count()), 0)
  let selftest = kernel_event_log_selftest()
  let count = kernel_event_count()

  uartPuts("events count=")
  uartPutDec(UInt64(count))
  uartPuts(" capacity=")
  uartPutDec(UInt64(kernel_event_capacity()))
  uartPuts(" lost=")
  uartPutDec(UInt64(kernel_event_lost_count()))
  uartPuts(" sequence=")
  uartPutDec(UInt64(kernel_event_sequence()))
  uartPuts(" selftest=")
  uartPutDec(UInt64(selftest))
  uartPuts("\n")

  var i: UInt32 = 0
  while i < count {
    uartPuts(" event index=")
    uartPutDec(UInt64(i))
    uartPuts(" seq=")
    uartPutDec(UInt64(kernel_event_seq(i)))
    uartPuts(" kind=")
    printEventKind(kernel_event_kind(i))
    uartPuts(" ticks=")
    uartPutDec(UInt64(kernel_event_ticks(i)))
    uartPuts(" a0=")
    uartPutHexCompact(UInt64(kernel_event_arg0(i)))
    uartPuts(" a1=")
    uartPutHexCompact(UInt64(kernel_event_arg1(i)))
    uartPuts(" a2=")
    uartPutHexCompact(UInt64(kernel_event_arg2(i)))
    uartPuts("\n")
    i += 1
  }
}

func printTasks2() {
  let count = kernel_task_count()

  uartPuts("tasks2 count=")
  uartPutDec(UInt64(count))
  uartPuts(" capacity=")
  uartPutDec(UInt64(kernel_task_capacity()))
  uartPuts(" selftest=")
  uartPutDec(UInt64(kernel_task_registry_selftest()))
  if kernel_task_object_id(TASK_FAST_ID) != 0 {
    uartPuts(" task index=")
    uartPutDec(UInt64(TASK_FAST_ID))
    uartPuts(" name=")
    printTaskName(TASK_FAST_ID)
  }
  uartPuts("\n")

  var i: UInt32 = 0
  while i < kernel_task_capacity() {
    if kernel_task_object_id(i) != 0 {
      uartPuts(" task index=")
      uartPutDec(UInt64(i))
      uartPuts(" object=")
      uartPutDec(UInt64(kernel_task_object_id(i)))
      uartPuts(" name=")
      printTaskName(i)
      uartPuts(" state=")
      printTaskState(kernel_task_state(i))
      uartPuts(" ticks=")
      uartPutDec(UInt64(kernel_task_tick_count(i)))
      uartPuts(" period_ms=")
      uartPutDec(UInt64(kernel_task_period_ms(i)))
      uartPuts("\n")
    }
    i += 1
  }
}

func printMailboxName(_ mailbox: UInt32) {
  var i: UInt32 = 0
  let n = kernel_mailbox_name_len(mailbox)
  while i < n {
    let b = UInt8(kernel_mailbox_name_byte(mailbox, i) & 0xFF)
    if b >= 0x20 && b < 0x7F {
      uartPutc(b)
    } else {
      uartPutc(0x2E)
    }
    i += 1
  }
}

func printMailboxes() {
  uartPuts("mailboxes count=")
  uartPutDec(UInt64(kernel_mailbox_count()))
  uartPuts(" capacity=")
  uartPutDec(UInt64(kernel_mailbox_capacity()))
  uartPuts(" queue_capacity=")
  uartPutDec(UInt64(kernel_mailbox_queue_capacity()))
  uartPuts(" selftest=")
  uartPutDec(UInt64(kernel_mailbox_selftest()))
  uartPuts("\n")

  var i: UInt32 = 0
  while i < kernel_mailbox_capacity() {
    if kernel_mailbox_object_id(i) != 0 {
      uartPuts(" mailbox index=")
      uartPutDec(UInt64(i))
      uartPuts(" object=")
      uartPutDec(UInt64(kernel_mailbox_object_id(i)))
      uartPuts(" name=")
      printMailboxName(i)
      uartPuts(" depth=")
      uartPutDec(UInt64(kernel_mailbox_depth(i)))
      uartPuts(" sent=")
      uartPutDec(UInt64(kernel_mailbox_sent_count(i)))
      uartPuts(" received=")
      uartPutDec(UInt64(kernel_mailbox_received_count(i)))
      uartPuts(" drops=")
      uartPutDec(UInt64(kernel_mailbox_drop_count(i)))
      uartPuts(" last_error=")
      uartPutDec(UInt64(kernel_mailbox_last_error(i)))
      uartPuts("\n")
    }
    i += 1
  }
}

func printSendtest() {
  let testValue: UInt = 0x132d
  var receivedValue: UInt = 0

  kernel_mailbox_clear(MAILBOX_SELFTEST_ID)
  let sent = kernel_mailbox_send_u64(MAILBOX_SELFTEST_ID, testValue)
  let received = kernel_mailbox_recv_u64(MAILBOX_SELFTEST_ID, &receivedValue)
  let selftest = kernel_mailbox_selftest()
  let ok = sent != 0 && received != 0 && selftest != 0 && receivedValue == testValue

  uartPuts("sendtest ok=")
  uartPutDec(UInt64(ok ? 1 : 0))
  uartPuts(" mailbox=")
  uartPutDec(UInt64(MAILBOX_SELFTEST_ID))
  uartPuts(" sent=")
  uartPutDec(UInt64(sent))
  uartPuts(" received=")
  uartPutDec(UInt64(received))
  uartPuts(" value=")
  uartPutHex(UInt64(receivedValue))
  uartPuts(" selftest=")
  uartPutDec(UInt64(selftest))
  uartPuts("\n")
}

func printSupervisorPolicy(_ policy: UInt32) {
  if policy == KERNEL_SUPERVISOR_POLICY_PANIC {
    uartPuts("panic")
  } else {
    uartPuts("observe")
  }
}

func printSupervisorState(_ state: UInt32) {
  if state == KERNEL_SUPERVISOR_STATE_MISSED {
    uartPuts("missed")
  } else {
    uartPuts("healthy")
  }
}

func printSupervisor() {
  kernel_supervisor_check()

  uartPuts("supervisor count=")
  uartPutDec(UInt64(kernel_supervisor_count()))
  uartPuts(" capacity=")
  uartPutDec(UInt64(kernel_supervisor_capacity()))
  uartPuts(" unhealthy=")
  uartPutDec(UInt64(kernel_supervisor_unhealthy_count()))
  uartPuts(" total_missed=")
  uartPutDec(UInt64(kernel_supervisor_total_missed_count()))
  uartPuts(" selftest=")
  uartPutDec(UInt64(kernel_supervisor_selftest()))
  uartPuts("\n")

  var i: UInt32 = 0
  while i < kernel_supervisor_capacity() {
    let task = kernel_supervisor_task_id(i)
    if task != 0 || i == TASK_FAST_ID {
      uartPuts(" supervise index=")
      uartPutDec(UInt64(i))
      uartPuts(" task=")
      uartPutDec(UInt64(task))
      uartPuts(" name=")
      printTaskName(task)
      uartPuts(" policy=")
      printSupervisorPolicy(kernel_supervisor_policy(i))
      uartPuts(" deadline_ms=")
      uartPutDec(UInt64(kernel_supervisor_deadline_ms(i)))
      uartPuts(" last_ms=")
      uartPutDec(UInt64(kernel_supervisor_last_heartbeat_ms(i)))
      uartPuts(" missed=")
      uartPutDec(UInt64(kernel_supervisor_missed_count(i)))
      uartPuts(" state=")
      printSupervisorState(kernel_supervisor_state(i))
      uartPuts("\n")
    }
    i += 1
  }
}

func printHealth() {
  kernel_supervisor_check()
  let unhealthy = kernel_supervisor_unhealthy_count()
  let missed = kernel_supervisor_total_missed_count()
  let ok = unhealthy == 0 && kernel_supervisor_selftest() != 0

  uartPuts("health ok=")
  uartPutDec(UInt64(ok ? 1 : 0))
  uartPuts(" supervised=")
  uartPutDec(UInt64(kernel_supervisor_count()))
  uartPuts(" unhealthy=")
  uartPutDec(UInt64(unhealthy))
  uartPuts(" total_missed=")
  uartPutDec(UInt64(missed))
  uartPuts(" uptime_ms=")
  uartPutDec(UInt64(kernel_supervisor_now_ms()))
  uartPuts("\n")
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

func printMemoryRegionName(_ index: UInt32) {
  var i: UInt32 = 0
  let n = kernel_memory_region_name_len(index)
  while i < n {
    let b = UInt8(kernel_memory_region_name_byte(index, i) & 0xFF)
    if b >= 0x20 && b < 0x7F {
      uartPutc(b)
    } else {
      uartPutc(0x2E)
    }
    i += 1
  }
}

func printMemoryRegionKind(_ kind: UInt32) {
  if kind == KERNEL_MEMORY_REGION_KIND_RESERVED {
    uartPuts("reserved")
  } else if kind == KERNEL_MEMORY_REGION_KIND_HEAP {
    uartPuts("heap")
  } else if kind == KERNEL_MEMORY_REGION_KIND_FRAMES {
    uartPuts("frames")
  } else {
    uartPuts("unknown")
  }
}

func printMemmap() {
  let count = kernel_memory_region_count()
  uartPuts("memmap valid=")
  uartPutDec(UInt64(kernel_memory_map_valid()))
  uartPuts(" regions=")
  uartPutDec(UInt64(count))
  uartPuts(" page_size=")
  uartPutDec(UInt64(KERNEL_PAGE_SIZE))
  uartPuts(" reserved=")
  uartPutDec(UInt64(kernel_memory_reserved_bytes()))
  uartPuts(" error=")
  uartPutDec(UInt64(kernel_memory_last_error()))
  uartPuts("\n")

  var i: UInt32 = 0
  while i < count {
    let start = kernel_memory_region_start(i)
    let end = kernel_memory_region_end(i)
    uartPuts(" region index=")
    uartPutDec(UInt64(i))
    uartPuts(" name=")
    printMemoryRegionName(i)
    uartPuts(" kind=")
    printMemoryRegionKind(kernel_memory_region_kind(i))
    uartPuts(" start=")
    uartPutHexCompact(UInt64(start))
    uartPuts(" end=")
    uartPutHexCompact(UInt64(end))
    uartPuts(" bytes=")
    uartPutDec(UInt64(end - start))
    uartPuts("\n")
    i += 1
  }
}

func printFrames() {
  let selftest = kernel_frame_allocator_selftest()

  uartPuts("frames total=")
  uartPutDec(UInt64(kernel_frame_total_count()))
  uartPuts(" free=")
  uartPutDec(UInt64(kernel_frame_free_count()))
  uartPuts(" used=")
  uartPutDec(UInt64(kernel_frame_used_count()))
  uartPuts(" reserved=")
  uartPutDec(UInt64(kernel_frame_reserved_count()))
  uartPuts(" base=")
  uartPutHexCompact(UInt64(kernel_frame_base()))
  uartPuts(" limit=")
  uartPutHexCompact(UInt64(kernel_frame_limit()))
  uartPuts(" selftest=")
  uartPutDec(UInt64(selftest))
  uartPuts("\n")
}

func printHeapcheck() {
  let ok = heap_guard_selftest() != 0

  uartPuts("heapcheck ok=")
  uartPutDec(UInt64(ok ? 1 : 0))
  uartPuts(" error=")
  uartPutDec(UInt64(heap_guard_last_error()))
  uartPuts(" invalid_frees=")
  uartPutDec(UInt64(heap_invalid_free_count()))
  uartPuts(" double_frees=")
  uartPutDec(UInt64(heap_double_free_count()))
  uartPuts(" corruptions=")
  uartPutDec(UInt64(heap_corruption_count()))
  uartPuts(" free=")
  uartPutDec(UInt64(heap_free_bytes()))
  uartPuts(" largest=")
  uartPutDec(UInt64(heap_largest_free_bytes()))
  uartPuts("\n")
}

func printFramecheck() {
  let stress = kernel_frame_allocator_stress_selftest()
  let ok = stress != 0 && kernel_frame_free_count() == kernel_frame_total_count()

  uartPuts("framecheck ok=")
  uartPutDec(UInt64(ok ? 1 : 0))
  uartPuts(" total=")
  uartPutDec(UInt64(kernel_frame_total_count()))
  uartPuts(" free=")
  uartPutDec(UInt64(kernel_frame_free_count()))
  uartPuts(" used=")
  uartPutDec(UInt64(kernel_frame_used_count()))
  uartPuts(" bad_frees=")
  uartPutDec(UInt64(kernel_frame_bad_free_count()))
  uartPuts(" double_frees=")
  uartPutDec(UInt64(kernel_frame_double_free_count()))
  uartPuts(" error=")
  uartPutDec(UInt64(kernel_frame_last_error()))
  uartPuts(" stress=")
  uartPutDec(UInt64(stress))
  uartPuts("\n")
}

func printStress() {
  let heap = heap_pressure_selftest()
  let frames = kernel_frame_pressure_selftest()
  let ok = heap != 0 && frames != 0

  uartPuts("stress ok=")
  uartPutDec(UInt64(ok ? 1 : 0))
  uartPuts(" heap=")
  uartPutDec(UInt64(heap))
  uartPuts(" frames=")
  uartPutDec(UInt64(frames))
  uartPuts(" heap_peak=")
  uartPutDec(UInt64(heap_pressure_last_peak_bytes()))
  uartPuts(" frame_peak=")
  uartPutDec(UInt64(kernel_frame_pressure_last_peak_count()))
  uartPuts(" heap_leak=")
  uartPutDec(UInt64(heap_pressure_last_leak_bytes()))
  uartPuts(" frame_leak=")
  uartPutDec(UInt64(kernel_frame_pressure_last_leak_count()))
  uartPuts("\n")
}

func printFrameprobe() {
  let ok = kernel_frame_guard_probe_selftest()

  uartPuts("frameprobe ok=")
  uartPutDec(UInt64(ok))
  uartPuts(" last_ok=")
  uartPutDec(UInt64(kernel_frame_guard_probe_last_ok()))
  uartPuts(" bad_frees=")
  uartPutDec(UInt64(kernel_frame_bad_free_count()))
  uartPuts(" double_frees=")
  uartPutDec(UInt64(kernel_frame_double_free_count()))
  uartPuts(" error=")
  uartPutDec(UInt64(kernel_frame_last_error()))
  uartPuts(" free=")
  uartPutDec(UInt64(kernel_frame_free_count()))
  uartPuts(" used=")
  uartPutDec(UInt64(kernel_frame_used_count()))
  uartPuts("\n")
}

func printBootcheck() {
  let memmap = kernel_memory_map_valid()
  let heap = heap_guard_selftest()
  let frames = kernel_frame_allocator_selftest()
  let ok = memmap != 0 && heap != 0 && frames != 0

  uartPuts("bootcheck ok=")
  uartPutDec(UInt64(ok ? 1 : 0))
  uartPuts(" memmap=")
  uartPutDec(UInt64(memmap))
  uartPuts(" heap=")
  uartPutDec(UInt64(heap))
  uartPuts(" frames=")
  uartPutDec(UInt64(frames))
  uartPuts(" retained_valid=")
  uartPutDec(UInt64(kernel_retained_valid()))
  uartPuts(" heap_free=")
  uartPutDec(UInt64(heap_free_bytes()))
  uartPuts(" frame_free=")
  uartPutDec(UInt64(kernel_frame_free_count()))
  uartPuts("\n")
}

func printSoak() {
  var round: UInt32 = 0
  var failures: UInt64 = 0
  var maxHeapPeak: UInt64 = 0
  var maxFramePeak: UInt64 = 0
  var heapLeak: UInt64 = 0
  var frameLeak: UInt64 = 0

  while round < SOAK_ROUNDS {
    let heap = heap_pressure_selftest()
    let frames = kernel_frame_pressure_selftest()
    if heap == 0 || frames == 0 {
      failures += 1
    }

    let currentHeapPeak = UInt64(heap_pressure_last_peak_bytes())
    let currentFramePeak = UInt64(kernel_frame_pressure_last_peak_count())
    if currentHeapPeak > maxHeapPeak {
      maxHeapPeak = currentHeapPeak
    }
    if currentFramePeak > maxFramePeak {
      maxFramePeak = currentFramePeak
    }

    heapLeak += UInt64(heap_pressure_last_leak_bytes())
    frameLeak += UInt64(kernel_frame_pressure_last_leak_count())
    round += 1
  }

  let ok = failures == 0 && heapLeak == 0 && frameLeak == 0
  uartPuts("soak ok=")
  uartPutDec(UInt64(ok ? 1 : 0))
  uartPuts(" rounds=")
  uartPutDec(UInt64(SOAK_ROUNDS))
  uartPuts(" failures=")
  uartPutDec(failures)
  uartPuts(" heap_peak=")
  uartPutDec(maxHeapPeak)
  uartPuts(" frame_peak=")
  uartPutDec(maxFramePeak)
  uartPuts(" heap_leak=")
  uartPutDec(heapLeak)
  uartPuts(" frame_leak=")
  uartPutDec(frameLeak)
  uartPuts("\n")
}

func shellHeapInvalidFreeTest() {
  uartPuts("shell heap-invalid-free-test reason=command\n")
  uartDrainTx()
  heap_guard_invalid_free_test()
  while true { wait_for_interrupt() }
}

func shellHeapDoubleFreeTest() {
  uartPuts("shell heap-double-free-test reason=command\n")
  uartDrainTx()
  heap_guard_double_free_test()
  while true { wait_for_interrupt() }
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
  } else if shellBufferEquals("tasks2") {
    printTasks2()
  } else if shellBufferEquals("kobjects") {
    printKobjects()
  } else if shellBufferEquals("mailboxes") {
    printMailboxes()
  } else if shellBufferEquals("sendtest") {
    printSendtest()
  } else if shellBufferEquals("supervisor") {
    printSupervisor()
  } else if shellBufferEquals("health") {
    printHealth()
  } else if shellBufferEquals("capcheck") {
    printCapcheck()
  } else if shellBufferEquals("events") {
    printEvents()
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
  } else if shellBufferEquals("memmap") {
    printMemmap()
  } else if shellBufferEquals("frames") {
    printFrames()
  } else if shellBufferEquals("heapcheck") {
    printHeapcheck()
  } else if shellBufferEquals("framecheck") {
    printFramecheck()
  } else if shellBufferEquals("stress") {
    printStress()
  } else if shellBufferEquals("frameprobe") {
    printFrameprobe()
  } else if shellBufferEquals("bootcheck") {
    printBootcheck()
  } else if shellBufferEquals("soak") {
    printSoak()
  } else if shellBufferEquals("heap-invalid-free-test") {
    shellHeapInvalidFreeTest()
  } else if shellBufferEquals("heap-double-free-test") {
    shellHeapDoubleFreeTest()
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
    kernel_task_mark_state(TASK_SHELL_ID, KERNEL_TASK_STATE_WAITING)
    let b = await uartReadByteAsync()
    kernel_task_mark_state(TASK_SHELL_ID, KERNEL_TASK_STATE_RUNNING)
    kernel_task_record_tick(TASK_SHELL_ID)
    kernel_supervisor_heartbeat(TASK_SHELL_ID)
    processUartShellByte(b)
  }
}

func startUartShellTask() {
  uart_shell_buffer_clear()
  Task { await uartShellMain() }
}

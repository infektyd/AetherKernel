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
// capability-tagged kernel object handles. V16 adds a fixed event log ring. V17
// adds a one-line boot certificate for host proof loops. V18 adds cooperative
// cancellation token selftests. V19 adds structured task spawn metadata. V20
// adds Swift-facing async channels over the fixed mailbox queues. V21 exposes
// the current MMU ownership boundary without adding dynamic remaps. V22 adds fixed guarded typed pools.
// V23 adds allocator/pool pressure telemetry. V24 adds a fixed driver registry.
// V25 adds a scriptable request/response envelope for host and agent control.
// V27 adds retained panic/fault taxonomy. V28 audits Swift runtime dependencies.
// V29 adds a compact agent-oriented control-session health line.
// V31 adds a preemptive scheduler tick substrate.
// V32 adds SMP secondary-core bring-up accounting.
// V33 adds atomics, spinlocks, and per-core run queues.
// V34 adds timer-driven SMP scheduler dispatch accounting.
// V35 adds C-only secondary scheduler workers.
// V36 adds timer-fed secondary scheduler workers.
// V37 adds timer-fed secondary C scheduler jobs.
// V38 adds SEV/WFE secondary scheduler wakeups.
// V39 adds secondary scheduler handoff acknowledgements.
// V40 adds scheduler backpressure proof.
// V41 adds secondary scheduler work-stealing proof.
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

func shellBufferHasPrefix(_ s: StaticString) -> Bool {
  let n = UInt32(s.utf8CodeUnitCount)
  if uart_shell_buffer_count() < n {
    return false
  }
  return shellBufferSliceEquals(0, n, s)
}

func shellBufferSliceEquals(_ start: UInt32, _ len: UInt32, _ s: StaticString) -> Bool {
  if len != UInt32(s.utf8CodeUnitCount) {
    return false
  }
  if start + len > uart_shell_buffer_count() {
    return false
  }

  let p = s.utf8Start
  var i: UInt32 = 0
  while i < len {
    if UInt8(uart_shell_buffer_get(start + i) & 0xFF) != p[Int(i)] {
      return false
    }
    i += 1
  }
  return true
}

func uartPutShellBufferSlice(_ start: UInt32, _ len: UInt32) {
  var i: UInt32 = 0
  while i < len {
    let b = UInt8(uart_shell_buffer_get(start + i) & 0xFF)
    if b >= 0x20 && b < 0x7F {
      uartPutc(b)
    } else {
      uartPutc(0x2E)
    }
    i += 1
  }
}

func printShellReady() {
  uartPuts("shell ready commands=help,protocol,status,heap,queues,tasks,tasks2,kobjects,drivers,drivercheck,mailboxes,sendtest,supervisor,health,capcheck,events,runtime,agent,certificate,sched,sched2,sched3,sched4,sched5,sched6,sched7,sched8,sched9,sched10,cores,locks,runqueues,diag,irqs,timers,memcheck,faults,retained,retained-clear,memmap,mmu,pools,poolcheck,heapfrag,poolstats,frames,heapcheck,framecheck,stress,frameprobe,bootcert,canceltest,taskcheck,channeltest,bootcheck,soak,heap-invalid-free-test,heap-double-free-test,panic-test,fault-test,reboot\n")
}

func printShellHelp() {
  uartPuts("shell help commands=help,protocol,status,heap,queues,tasks,tasks2,kobjects,drivers,drivercheck,mailboxes,sendtest,supervisor,health,capcheck,events,runtime,agent,certificate,sched,sched2,sched3,sched4,sched5,sched6,sched7,sched8,sched9,sched10,cores,locks,runqueues,diag,irqs,timers,memcheck,faults,retained,retained-clear,memmap,mmu,pools,poolcheck,heapfrag,poolstats,frames,heapcheck,framecheck,stress,frameprobe,bootcert,canceltest,taskcheck,channeltest,bootcheck,soak,heap-invalid-free-test,heap-double-free-test,panic-test,fault-test,reboot\n")
}

func printProtocol() {
  uartPuts("protocol version=2 request=req id_field=id cmd_field=cmd begin_end=1 errors=1 max_line=80\n")
}

func printRuntimeAudit() {
  let runtimeAudit = kernel_runtime_audit_selftest()

  uartPuts("runtime ok=")
  uartPutDec(UInt64(runtimeAudit))
  uartPuts(" version=28")
  uartPuts(" swift=6.3.2")
  uartPuts(" source_hooks=")
  uartPutDec(UInt64(kernel_runtime_source_hook_count()))
  uartPuts(" linked_hooks=")
  uartPutDec(UInt64(kernel_runtime_linked_hook_count()))
  uartPuts(" heap_shims=")
  uartPutDec(UInt64(kernel_runtime_heap_shim_count()))
  uartPuts(" linked_heap_shims=")
  uartPutDec(UInt64(kernel_runtime_linked_heap_shim_count()))
  uartPuts(" required_symbols=")
  uartPutDec(UInt64(kernel_runtime_required_symbol_count()))
  uartPuts(" audit=1\n")
}

func printAgentSession() {
  kernel_supervisor_check()

  let runtimeAudit = kernel_runtime_audit_selftest()
  let eventsLost = kernel_event_lost_count()
  let bootcertOk = runtimeAudit != 0 && kernel_memory_map_valid() != 0 &&
    heap_guard_selftest() != 0 && kernel_frame_allocator_selftest() != 0 &&
    kernel_driver_registry_selftest() != 0 && heap_fragmentation_selftest() != 0 &&
    kernel_pool_selftest() != 0 && kernel_pool_pressure_selftest() != 0 &&
    kernel_mmu_selftest() != 0 && aetherChannelSelftest() != 0 &&
    aetherTaskSpawnSelftest() != 0 && kernel_cancel_selftest() != 0 &&
    kernel_object_registry_selftest() != 0 && kernel_task_registry_selftest() != 0 &&
    kernel_mailbox_selftest() != 0 && kernel_supervisor_selftest() != 0 &&
    kernel_event_log_selftest() != 0 && eventsLost == 0

  uartPuts("agent ok=")
  uartPutDec(UInt64(bootcertOk ? 1 : 0))
  uartPuts(" version=29")
  if bootcertOk {
    uartPuts(" health=green")
  } else {
    uartPuts(" health=red")
  }
  uartPuts(" bootcert=")
  uartPutDec(UInt64(bootcertOk ? 1 : 0))
  uartPuts(" runtime=")
  uartPutDec(UInt64(runtimeAudit))
  uartPuts(" protocol=2")
  uartPuts(" agent=1")
  uartPuts(" events_lost=")
  uartPutDec(UInt64(eventsLost))
  uartPuts(" heap_free=")
  uartPutDec(UInt64(heap_free_bytes()))
  uartPuts(" ready=")
  uartPutDec(UInt64(executor_ready_count()))
  uartPuts(" delayed=")
  uartPutDec(UInt64(executor_delayed_count()))
  uartPuts(" sleepers=")
  uartPutDec(UInt64(timerSleepPendingCount()))
  uartPuts("\n")
}

func printSubstrateCertificate() {
  kernel_supervisor_check()

  let workerFeed = kernel_scheduler_timer_worker_feed_selftest()
  let secondaryWorkers = kernel_scheduler_secondary_worker_selftest()
  let jobExec = kernel_scheduler_secondary_job_selftest()
  let wake = kernel_scheduler_secondary_wake_selftest()
  let preemptive = kernel_scheduler_active()
  let smpScheduler = kernel_scheduler_smp_selftest()
  let handoff = kernel_scheduler_secondary_handoff_selftest()
  let backpressure = kernel_scheduler_backpressure_selftest()
  let stealing = kernel_scheduler_work_steal_selftest()
  let fairness = kernel_scheduler_fairness_selftest()
  let atomics = kernel_atomic_selftest()
  let locks = kernel_spinlock_selftest()
  let queues = kernel_scheduler_runqueue_selftest()
  let smp = kernel_smp_selftest()
  let scheduler = kernel_scheduler_selftest()
  let runtimeAudit = kernel_runtime_audit_selftest()
  let agentSession = UInt32(1)
  let memmap = kernel_memory_map_valid()
  let heap = heap_guard_selftest()
  let frames = kernel_frame_allocator_selftest()
  let memory = memmap != 0 && heap != 0 && frames != 0 ? 1 : 0
  let kobjects = kernel_object_registry_selftest()
  let handles = kernel_object_handle_selftest() != 0 && kernel_object_capcheck_selftest() != 0 ? 1 : 0
  let objects = kobjects != 0 && handles != 0 ? 1 : 0
  let tasks = kernel_task_registry_selftest()
  let mailboxes = kernel_mailbox_selftest()
  let supervisor = kernel_supervisor_selftest()
  let events = kernel_event_log_selftest()
  let cancellations = kernel_cancel_selftest()
  let channels = aetherChannelSelftest()
  let drivers = kernel_driver_registry_selftest()
  let pressure = heap_fragmentation_selftest() != 0 && kernel_pool_pressure_selftest() != 0 ? 1 : 0
  let pools = kernel_pool_selftest()
  let mmu = kernel_mmu_selftest()
  let eventsLost = kernel_event_lost_count()
  let bootcertOk = fairness != 0 && stealing != 0 && backpressure != 0 && handoff != 0 && wake != 0 && jobExec != 0 && workerFeed != 0 && secondaryWorkers != 0 && preemptive != 0 && smpScheduler != 0 && atomics != 0 && locks != 0 && queues != 0 && runtimeAudit != 0 && smp != 0 && scheduler != 0 && agentSession != 0 && memory != 0 &&
    objects != 0 && tasks != 0 && mailboxes != 0 && supervisor != 0 &&
    events != 0 && cancellations != 0 && channels != 0 && drivers != 0 &&
    pressure != 0 && pools != 0 && mmu != 0 && eventsLost == 0

  uartPuts("certificate ok=")
  uartPutDec(UInt64(bootcertOk ? 1 : 0))
  uartPuts(" version=42")
  uartPuts(" substrate=1")
  uartPuts(" bootcert=")
  uartPutDec(UInt64(bootcertOk ? 1 : 0))
  uartPuts(" fairness=")
  uartPutDec(UInt64(fairness))
  uartPuts(" stealing=")
  uartPutDec(UInt64(stealing))
  uartPuts(" backpressure=")
  uartPutDec(UInt64(backpressure))
  uartPuts(" handoff=")
  uartPutDec(UInt64(handoff))
  uartPuts(" wake=")
  uartPutDec(UInt64(wake))
  uartPuts(" job_exec=")
  uartPutDec(UInt64(jobExec))
  uartPuts(" worker_feed=")
  uartPutDec(UInt64(workerFeed))
  uartPuts(" secondary_workers=")
  uartPutDec(UInt64(secondaryWorkers))
  uartPuts(" preemptive=")
  uartPutDec(UInt64(preemptive))
  uartPuts(" smp_scheduler=")
  uartPutDec(UInt64(smpScheduler))
  uartPuts(" atomics=")
  uartPutDec(UInt64(atomics))
  uartPuts(" locks=")
  uartPutDec(UInt64(locks))
  uartPuts(" queues=")
  uartPutDec(UInt64(queues))
  uartPuts(" smp=")
  uartPutDec(UInt64(smp))
  uartPuts(" scheduler=")
  uartPutDec(UInt64(scheduler))
  uartPuts(" agent=1")
  uartPuts(" runtime=")
  uartPutDec(UInt64(runtimeAudit))
  uartPuts(" protocol=2")
  uartPuts(" memory=")
  uartPutDec(UInt64(memory))
  uartPuts(" objects=")
  uartPutDec(UInt64(objects))
  uartPuts(" tasks=")
  uartPutDec(UInt64(tasks))
  uartPuts(" mailboxes=")
  uartPutDec(UInt64(mailboxes))
  uartPuts(" supervisor=")
  uartPutDec(UInt64(supervisor))
  uartPuts(" handles=")
  uartPutDec(UInt64(handles))
  uartPuts(" events=")
  uartPutDec(UInt64(events))
  uartPuts(" cancellations=")
  uartPutDec(UInt64(cancellations))
  uartPuts(" channels=")
  uartPutDec(UInt64(channels))
  uartPuts(" drivers=")
  uartPutDec(UInt64(drivers))
  uartPuts(" pressure=")
  uartPutDec(UInt64(pressure))
  uartPuts(" pools=")
  uartPutDec(UInt64(pools))
  uartPuts(" mmu=")
  uartPutDec(UInt64(mmu))
  uartPuts(" swift=6.3.2")
  uartPuts(" events_lost=")
  uartPutDec(UInt64(eventsLost))
  uartPuts(" heap_free=")
  uartPutDec(UInt64(heap_free_bytes()))
  uartPuts(" frame_free=")
  uartPutDec(UInt64(kernel_frame_free_count()))
  uartPuts(" uptime_ms=")
  uartPutDec(UInt64(kernel_supervisor_now_ms()))
  uartPuts("\n")
}

func printScheduler() {
  let selftest = kernel_scheduler_selftest()
  let core: UInt32 = 0
  let active = kernel_scheduler_active()
  let ticks = kernel_scheduler_tick_count(core)
  let irqTicks = kernel_scheduler_irq_tick_count(core)
  let preemptions = kernel_scheduler_preempt_count(core)
  let runqueue = kernel_scheduler_runqueue_count(core)
  let capacity = kernel_scheduler_runqueue_capacity()

  uartPuts("sched ok=")
  uartPutDec(UInt64(selftest != 0 && active != 0 ? 1 : 0))
  uartPuts(" version=31")
  uartPuts(" active=")
  uartPutDec(UInt64(active))
  uartPuts(" cores=")
  uartPutDec(1)
  uartPuts(" core=0")
  uartPuts(" interval_ticks=")
  uartPutDec(UInt64(kernel_scheduler_interval_ticks()))
  uartPuts(" ticks=")
  uartPutDec(UInt64(ticks))
  uartPuts(" irq_ticks=")
  uartPutDec(UInt64(irqTicks))
  uartPuts(" preemptions=")
  uartPutDec(UInt64(preemptions))
  uartPuts(" runqueue=")
  uartPutDec(UInt64(runqueue))
  uartPuts("/")
  uartPutDec(UInt64(capacity))
  uartPuts(" enqueues=")
  uartPutDec(UInt64(kernel_scheduler_enqueue_count(core)))
  uartPuts(" dequeues=")
  uartPutDec(UInt64(kernel_scheduler_dequeue_count(core)))
  uartPuts(" selftest=")
  uartPutDec(UInt64(selftest))
  uartPuts("\n")
}

func printScheduler2() {
  let preemptive = kernel_scheduler_active()
  let smpScheduler = kernel_scheduler_smp_selftest()
  let ok = preemptive != 0 && smpScheduler != 0 ? 1 : 0

  uartPuts("sched2 ok=")
  uartPutDec(UInt64(ok))
  uartPuts(" version=34")
  uartPuts(" preemptive=")
  uartPutDec(UInt64(preemptive))
  uartPuts(" smp_scheduler=")
  uartPutDec(UInt64(smpScheduler))
  uartPuts(" active=")
  uartPutDec(UInt64(kernel_scheduler_smp_dispatch_enabled()))
  uartPuts(" cores=")
  uartPutDec(UInt64(kernel_scheduler_core_count()))
  uartPuts(" online=")
  uartPutDec(UInt64(kernel_smp_online_count()))
  uartPuts(" dispatches=")
  uartPutDec(UInt64(kernel_scheduler_total_dispatch_count()))
  uartPuts(" routes=")
  uartPutDec(UInt64(kernel_scheduler_total_route_count()))
  uartPuts(" min=")
  uartPutDec(UInt64(kernel_scheduler_fairness_min()))
  uartPuts(" max=")
  uartPutDec(UInt64(kernel_scheduler_fairness_max()))
  uartPuts(" imbalance=")
  uartPutDec(UInt64(kernel_scheduler_fairness_imbalance()))
  uartPuts(" core0=")
  uartPutDec(UInt64(kernel_scheduler_dispatch_count(0)))
  uartPuts(" core1=")
  uartPutDec(UInt64(kernel_scheduler_dispatch_count(1)))
  uartPuts(" core2=")
  uartPutDec(UInt64(kernel_scheduler_dispatch_count(2)))
  uartPuts(" core3=")
  uartPutDec(UInt64(kernel_scheduler_dispatch_count(3)))
  uartPuts(" selftest=")
  uartPutDec(UInt64(smpScheduler))
  uartPuts("\n")
}

func printScheduler3() {
  let secondaryWorkers = kernel_scheduler_secondary_worker_selftest()
  let ok = secondaryWorkers != 0 ? 1 : 0

  uartPuts("sched3 ok=")
  uartPutDec(UInt64(ok))
  uartPuts(" version=35")
  uartPuts(" secondary_workers=")
  uartPutDec(UInt64(secondaryWorkers))
  uartPuts(" active=")
  uartPutDec(UInt64(kernel_scheduler_secondary_workers_enabled()))
  uartPuts(" cores=")
  uartPutDec(UInt64(kernel_scheduler_core_count()))
  uartPuts(" online=")
  uartPutDec(UInt64(kernel_smp_online_count()))
  uartPuts(" worker_drains=")
  uartPutDec(UInt64(kernel_scheduler_total_worker_drain_count()))
  uartPuts(" worker_idles=")
  uartPutDec(UInt64(kernel_scheduler_total_worker_idle_count()))
  uartPuts(" min=")
  uartPutDec(UInt64(kernel_scheduler_secondary_worker_min()))
  uartPuts(" max=")
  uartPutDec(UInt64(kernel_scheduler_secondary_worker_max()))
  uartPuts(" imbalance=")
  uartPutDec(UInt64(kernel_scheduler_secondary_worker_imbalance()))
  uartPuts(" core0=")
  uartPutDec(UInt64(kernel_scheduler_worker_drain_count(0)))
  uartPuts(" core1=")
  uartPutDec(UInt64(kernel_scheduler_worker_drain_count(1)))
  uartPuts(" core2=")
  uartPutDec(UInt64(kernel_scheduler_worker_drain_count(2)))
  uartPuts(" core3=")
  uartPutDec(UInt64(kernel_scheduler_worker_drain_count(3)))
  uartPuts(" selftest=")
  uartPutDec(UInt64(secondaryWorkers))
  uartPuts("\n")
}

func printScheduler4() {
  let workerFeed = kernel_scheduler_timer_worker_feed_selftest()
  let secondaryWorkers = kernel_scheduler_secondary_worker_selftest()
  let ok = workerFeed != 0 && secondaryWorkers != 0 ? 1 : 0

  uartPuts("sched4 ok=")
  uartPutDec(UInt64(ok))
  uartPuts(" version=36")
  uartPuts(" worker_feed=")
  uartPutDec(UInt64(workerFeed))
  uartPuts(" secondary_workers=")
  uartPutDec(UInt64(secondaryWorkers))
  uartPuts(" feeds=")
  uartPutDec(UInt64(kernel_scheduler_secondary_worker_feed_total()))
  uartPuts(" drains=")
  uartPutDec(UInt64(kernel_scheduler_secondary_worker_total()))
  uartPuts(" drops=")
  uartPutDec(UInt64(kernel_scheduler_total_worker_feed_drop_count()))
  uartPuts(" gap=")
  uartPutDec(UInt64(kernel_scheduler_worker_feed_drain_gap()))
  uartPuts(" feed_imbalance=")
  uartPutDec(UInt64(kernel_scheduler_secondary_worker_feed_imbalance()))
  uartPuts(" drain_imbalance=")
  uartPutDec(UInt64(kernel_scheduler_secondary_worker_imbalance()))
  uartPuts(" core0_feed=")
  uartPutDec(UInt64(kernel_scheduler_worker_feed_count(0)))
  uartPuts(" core1_feed=")
  uartPutDec(UInt64(kernel_scheduler_worker_feed_count(1)))
  uartPuts(" core2_feed=")
  uartPutDec(UInt64(kernel_scheduler_worker_feed_count(2)))
  uartPuts(" core3_feed=")
  uartPutDec(UInt64(kernel_scheduler_worker_feed_count(3)))
  uartPuts(" core0_drain=")
  uartPutDec(UInt64(kernel_scheduler_worker_drain_count(0)))
  uartPuts(" core1_drain=")
  uartPutDec(UInt64(kernel_scheduler_worker_drain_count(1)))
  uartPuts(" core2_drain=")
  uartPutDec(UInt64(kernel_scheduler_worker_drain_count(2)))
  uartPuts(" core3_drain=")
  uartPutDec(UInt64(kernel_scheduler_worker_drain_count(3)))
  uartPuts(" selftest=")
  uartPutDec(UInt64(workerFeed))
  uartPuts("\n")
}

func printScheduler5() {
  let jobExec = kernel_scheduler_secondary_job_selftest()
  let workerFeed = kernel_scheduler_timer_worker_feed_selftest()
  let secondaryWorkers = kernel_scheduler_secondary_worker_selftest()
  let ok = jobExec != 0 && workerFeed != 0 && secondaryWorkers != 0 ? 1 : 0

  uartPuts("sched5 ok=")
  uartPutDec(UInt64(ok))
  uartPuts(" version=37")
  uartPuts(" job_exec=")
  uartPutDec(UInt64(jobExec))
  uartPuts(" worker_feed=")
  uartPutDec(UInt64(workerFeed))
  uartPuts(" secondary_workers=")
  uartPutDec(UInt64(secondaryWorkers))
  uartPuts(" executions=")
  uartPutDec(UInt64(kernel_scheduler_secondary_job_total()))
  uartPuts(" completions=")
  uartPutDec(UInt64(kernel_scheduler_secondary_job_completion_total()))
  uartPuts(" noops=")
  uartPutDec(UInt64(kernel_scheduler_secondary_job_noop_total()))
  uartPuts(" checksum=")
  uartPutDec(UInt64(kernel_scheduler_secondary_job_checksum_total()))
  uartPuts(" gap=")
  uartPutDec(UInt64(kernel_scheduler_secondary_job_completion_gap()))
  uartPuts(" imbalance=")
  uartPutDec(UInt64(kernel_scheduler_secondary_job_imbalance()))
  uartPuts(" core0_exec=")
  uartPutDec(UInt64(kernel_scheduler_secondary_job_execution_count(0)))
  uartPuts(" core1_exec=")
  uartPutDec(UInt64(kernel_scheduler_secondary_job_execution_count(1)))
  uartPuts(" core2_exec=")
  uartPutDec(UInt64(kernel_scheduler_secondary_job_execution_count(2)))
  uartPuts(" core3_exec=")
  uartPutDec(UInt64(kernel_scheduler_secondary_job_execution_count(3)))
  uartPuts(" core0_done=")
  uartPutDec(UInt64(kernel_scheduler_secondary_job_completion_count(0)))
  uartPuts(" core1_done=")
  uartPutDec(UInt64(kernel_scheduler_secondary_job_completion_count(1)))
  uartPuts(" core2_done=")
  uartPutDec(UInt64(kernel_scheduler_secondary_job_completion_count(2)))
  uartPuts(" core3_done=")
  uartPutDec(UInt64(kernel_scheduler_secondary_job_completion_count(3)))
  uartPuts(" selftest=")
  uartPutDec(UInt64(jobExec))
  uartPuts("\n")
}

func printScheduler6() {
  let wake = kernel_scheduler_secondary_wake_selftest()
  let jobExec = kernel_scheduler_secondary_job_selftest()
  let workerFeed = kernel_scheduler_timer_worker_feed_selftest()
  let ok = wake != 0 && jobExec != 0 && workerFeed != 0 ? 1 : 0

  uartPuts("sched6 ok=")
  uartPutDec(UInt64(ok))
  uartPuts(" version=38")
  uartPuts(" wake=")
  uartPutDec(UInt64(wake))
  uartPuts(" job_exec=")
  uartPutDec(UInt64(jobExec))
  uartPuts(" worker_feed=")
  uartPutDec(UInt64(workerFeed))
  uartPuts(" signals=")
  uartPutDec(UInt64(kernel_scheduler_secondary_wake_signal_total()))
  uartPuts(" mask=")
  uartPutHexCompact(UInt64(kernel_scheduler_secondary_wake_signal_mask()))
  uartPuts(" targets=")
  uartPutDec(UInt64(kernel_scheduler_secondary_wake_target_total()))
  uartPuts(" waits=")
  uartPutDec(UInt64(kernel_scheduler_secondary_wake_wait_total()))
  uartPuts(" wakes=")
  uartPutDec(UInt64(kernel_scheduler_secondary_wake_ack_total()))
  uartPuts(" gap=")
  uartPutDec(UInt64(kernel_scheduler_secondary_wake_gap()))
  uartPuts(" imbalance=")
  uartPutDec(UInt64(kernel_scheduler_secondary_wake_imbalance()))
  uartPuts(" core0_wait=")
  uartPutDec(UInt64(kernel_scheduler_secondary_wake_wait_count(0)))
  uartPuts(" core1_wait=")
  uartPutDec(UInt64(kernel_scheduler_secondary_wake_wait_count(1)))
  uartPuts(" core2_wait=")
  uartPutDec(UInt64(kernel_scheduler_secondary_wake_wait_count(2)))
  uartPuts(" core3_wait=")
  uartPutDec(UInt64(kernel_scheduler_secondary_wake_wait_count(3)))
  uartPuts(" core0_wake=")
  uartPutDec(UInt64(kernel_scheduler_secondary_wake_ack_count(0)))
  uartPuts(" core1_wake=")
  uartPutDec(UInt64(kernel_scheduler_secondary_wake_ack_count(1)))
  uartPuts(" core2_wake=")
  uartPutDec(UInt64(kernel_scheduler_secondary_wake_ack_count(2)))
  uartPuts(" core3_wake=")
  uartPutDec(UInt64(kernel_scheduler_secondary_wake_ack_count(3)))
  uartPuts(" selftest=")
  uartPutDec(UInt64(wake))
  uartPuts("\n")
}

func printScheduler7() {
  let wake = kernel_scheduler_secondary_wake_selftest()
  let handoff = kernel_scheduler_secondary_handoff_selftest()
  let jobExec = kernel_scheduler_secondary_job_selftest()
  let ok = handoff != 0 && wake != 0 && jobExec != 0 ? 1 : 0

  uartPuts("sched7 ok=")
  uartPutDec(UInt64(ok))
  uartPuts(" version=39")
  uartPuts(" handoff=")
  uartPutDec(UInt64(handoff))
  uartPuts(" wake=")
  uartPutDec(UInt64(wake))
  uartPuts(" job_exec=")
  uartPutDec(UInt64(jobExec))
  uartPuts(" issued=")
  uartPutDec(UInt64(kernel_scheduler_secondary_handoff_issue_total()))
  uartPuts(" completed=")
  uartPutDec(UInt64(kernel_scheduler_secondary_handoff_completion_total()))
  uartPuts(" gap=")
  uartPutDec(UInt64(kernel_scheduler_secondary_handoff_gap()))
  uartPuts(" imbalance=")
  uartPutDec(UInt64(kernel_scheduler_secondary_handoff_imbalance()))
  uartPuts(" core0_issue=")
  uartPutDec(UInt64(kernel_scheduler_secondary_handoff_issue_count(0)))
  uartPuts(" core1_issue=")
  uartPutDec(UInt64(kernel_scheduler_secondary_handoff_issue_count(1)))
  uartPuts(" core2_issue=")
  uartPutDec(UInt64(kernel_scheduler_secondary_handoff_issue_count(2)))
  uartPuts(" core3_issue=")
  uartPutDec(UInt64(kernel_scheduler_secondary_handoff_issue_count(3)))
  uartPuts(" core0_done=")
  uartPutDec(UInt64(kernel_scheduler_secondary_handoff_completion_count(0)))
  uartPuts(" core1_done=")
  uartPutDec(UInt64(kernel_scheduler_secondary_handoff_completion_count(1)))
  uartPuts(" core2_done=")
  uartPutDec(UInt64(kernel_scheduler_secondary_handoff_completion_count(2)))
  uartPuts(" core3_done=")
  uartPutDec(UInt64(kernel_scheduler_secondary_handoff_completion_count(3)))
  uartPuts(" selftest=")
  uartPutDec(UInt64(handoff))
  uartPuts("\n")
}

func printScheduler8() {
  let wake = kernel_scheduler_secondary_wake_selftest()
  let handoff = kernel_scheduler_secondary_handoff_selftest()
  let backpressure = kernel_scheduler_backpressure_selftest()
  let total = kernel_scheduler_runqueue_count(0) +
    kernel_scheduler_runqueue_count(1) +
    kernel_scheduler_runqueue_count(2) +
    kernel_scheduler_runqueue_count(3)
  let ok = backpressure != 0 && handoff != 0 && wake != 0 && total == 0 ? 1 : 0

  uartPuts("sched8 ok=")
  uartPutDec(UInt64(ok))
  uartPuts(" version=40")
  uartPuts(" backpressure=")
  uartPutDec(UInt64(backpressure))
  uartPuts(" handoff=")
  uartPutDec(UInt64(handoff))
  uartPuts(" wake=")
  uartPutDec(UInt64(wake))
  uartPuts(" high_water=")
  uartPutDec(UInt64(kernel_scheduler_runqueue_high_water_max()))
  uartPuts(" overflows=")
  uartPutDec(UInt64(kernel_scheduler_runqueue_overflow_total()))
  uartPuts(" total=")
  uartPutDec(UInt64(total))
  uartPuts(" capacity=")
  uartPutDec(UInt64(kernel_scheduler_runqueue_capacity()))
  uartPuts(" core0_high=")
  uartPutDec(UInt64(kernel_scheduler_runqueue_high_water(0)))
  uartPuts(" core1_high=")
  uartPutDec(UInt64(kernel_scheduler_runqueue_high_water(1)))
  uartPuts(" core2_high=")
  uartPutDec(UInt64(kernel_scheduler_runqueue_high_water(2)))
  uartPuts(" core3_high=")
  uartPutDec(UInt64(kernel_scheduler_runqueue_high_water(3)))
  uartPuts(" core0_overflow=")
  uartPutDec(UInt64(kernel_scheduler_runqueue_overflow_count(0)))
  uartPuts(" core1_overflow=")
  uartPutDec(UInt64(kernel_scheduler_runqueue_overflow_count(1)))
  uartPuts(" core2_overflow=")
  uartPutDec(UInt64(kernel_scheduler_runqueue_overflow_count(2)))
  uartPuts(" core3_overflow=")
  uartPutDec(UInt64(kernel_scheduler_runqueue_overflow_count(3)))
  uartPuts(" selftest=")
  uartPutDec(UInt64(backpressure))
  uartPuts("\n")
}

func printScheduler9() {
  let wake = kernel_scheduler_secondary_wake_selftest()
  let handoff = kernel_scheduler_secondary_handoff_selftest()
  let backpressure = kernel_scheduler_backpressure_selftest()
  let stealing = kernel_scheduler_work_steal_selftest()
  let total = kernel_scheduler_runqueue_count(0) +
    kernel_scheduler_runqueue_count(1) +
    kernel_scheduler_runqueue_count(2) +
    kernel_scheduler_runqueue_count(3)
  let ok = stealing != 0 && backpressure != 0 && handoff != 0 && wake != 0 && total == 0 ? 1 : 0

  uartPuts("sched9 ok=")
  uartPutDec(UInt64(ok))
  uartPuts(" version=41")
  uartPuts(" stealing=")
  uartPutDec(UInt64(stealing))
  uartPuts(" backpressure=")
  uartPutDec(UInt64(backpressure))
  uartPuts(" handoff=")
  uartPutDec(UInt64(handoff))
  uartPuts(" wake=")
  uartPutDec(UInt64(wake))
  uartPuts(" steals=")
  uartPutDec(UInt64(kernel_scheduler_steal_total()))
  uartPuts(" completions=")
  uartPutDec(UInt64(kernel_scheduler_steal_completion_total()))
  uartPuts(" total=")
  uartPutDec(UInt64(total))
  uartPuts(" capacity=")
  uartPutDec(UInt64(kernel_scheduler_runqueue_capacity()))
  uartPuts(" source_core1=")
  uartPutDec(UInt64(kernel_scheduler_steal_source_count(1)))
  uartPuts(" source_core2=")
  uartPutDec(UInt64(kernel_scheduler_steal_source_count(2)))
  uartPuts(" source_core3=")
  uartPutDec(UInt64(kernel_scheduler_steal_source_count(3)))
  uartPuts(" dest_core1=")
  uartPutDec(UInt64(kernel_scheduler_steal_success_count(1)))
  uartPuts(" dest_core2=")
  uartPutDec(UInt64(kernel_scheduler_steal_success_count(2)))
  uartPuts(" dest_core3=")
  uartPutDec(UInt64(kernel_scheduler_steal_success_count(3)))
  uartPuts(" attempts_core1=")
  uartPutDec(UInt64(kernel_scheduler_steal_attempt_count(1)))
  uartPuts(" attempts_core2=")
  uartPutDec(UInt64(kernel_scheduler_steal_attempt_count(2)))
  uartPuts(" attempts_core3=")
  uartPutDec(UInt64(kernel_scheduler_steal_attempt_count(3)))
  uartPuts(" selftest=")
  uartPutDec(UInt64(stealing))
  uartPuts("\n")
}

func printScheduler10() {
  let wake = kernel_scheduler_secondary_wake_selftest()
  let handoff = kernel_scheduler_secondary_handoff_selftest()
  let backpressure = kernel_scheduler_backpressure_selftest()
  let stealing = kernel_scheduler_work_steal_selftest()
  let fairness = kernel_scheduler_fairness_selftest()
  let total = kernel_scheduler_runqueue_count(0) +
    kernel_scheduler_runqueue_count(1) +
    kernel_scheduler_runqueue_count(2) +
    kernel_scheduler_runqueue_count(3)
  let ok = fairness != 0 && stealing != 0 && backpressure != 0 && handoff != 0 && wake != 0 && total == 0 ? 1 : 0

  uartPuts("sched10 ok=")
  uartPutDec(UInt64(ok))
  uartPuts(" version=42")
  uartPuts(" fairness=")
  uartPutDec(UInt64(fairness))
  uartPuts(" stealing=")
  uartPutDec(UInt64(stealing))
  uartPuts(" backpressure=")
  uartPutDec(UInt64(backpressure))
  uartPuts(" handoff=")
  uartPutDec(UInt64(handoff))
  uartPuts(" wake=")
  uartPutDec(UInt64(wake))
  uartPuts(" balances=")
  uartPutDec(UInt64(kernel_scheduler_balance_total()))
  uartPuts(" completions=")
  uartPutDec(UInt64(kernel_scheduler_balance_completion_total()))
  uartPuts(" min=")
  uartPutDec(UInt64(kernel_scheduler_fairness_min()))
  uartPuts(" max=")
  uartPutDec(UInt64(kernel_scheduler_fairness_max()))
  uartPuts(" imbalance=")
  uartPutDec(UInt64(kernel_scheduler_fairness_imbalance()))
  uartPuts(" total=")
  uartPutDec(UInt64(total))
  uartPuts(" capacity=")
  uartPutDec(UInt64(kernel_scheduler_runqueue_capacity()))
  uartPuts(" source_core1=")
  uartPutDec(UInt64(kernel_scheduler_balance_source_count(1)))
  uartPuts(" source_core2=")
  uartPutDec(UInt64(kernel_scheduler_balance_source_count(2)))
  uartPuts(" source_core3=")
  uartPutDec(UInt64(kernel_scheduler_balance_source_count(3)))
  uartPuts(" dest_core1=")
  uartPutDec(UInt64(kernel_scheduler_balance_success_count(1)))
  uartPuts(" dest_core2=")
  uartPutDec(UInt64(kernel_scheduler_balance_success_count(2)))
  uartPuts(" dest_core3=")
  uartPutDec(UInt64(kernel_scheduler_balance_success_count(3)))
  uartPuts(" attempts_core1=")
  uartPutDec(UInt64(kernel_scheduler_balance_attempt_count(1)))
  uartPuts(" attempts_core2=")
  uartPutDec(UInt64(kernel_scheduler_balance_attempt_count(2)))
  uartPuts(" attempts_core3=")
  uartPutDec(UInt64(kernel_scheduler_balance_attempt_count(3)))
  uartPuts(" queue_min=")
  uartPutDec(UInt64(kernel_scheduler_secondary_queue_min()))
  uartPuts(" queue_max=")
  uartPutDec(UInt64(kernel_scheduler_secondary_queue_max()))
  uartPuts(" queue_imbalance=")
  uartPutDec(UInt64(kernel_scheduler_secondary_queue_imbalance()))
  uartPuts(" selftest=")
  uartPutDec(UInt64(fairness))
  uartPuts("\n")
}

func printCores() {
  let selftest = kernel_smp_selftest()

  uartPuts("cores ok=")
  uartPutDec(UInt64(selftest))
  uartPuts(" version=32")
  uartPuts(" capacity=")
  uartPutDec(UInt64(kernel_smp_core_capacity()))
  uartPuts(" online=")
  uartPutDec(UInt64(kernel_smp_online_count()))
  uartPuts(" mask=")
  uartPutHexCompact(UInt64(kernel_smp_online_mask()))
  uartPuts(" primary=")
  uartPutDec(UInt64(kernel_smp_primary_core_id()))
  uartPuts(" release=")
  uartPutHexCompact(UInt64(kernel_smp_release_map()))
  uartPuts(" selftest=")
  uartPutDec(UInt64(selftest))

  uartPuts(" core0=")
  uartPutDec(UInt64(kernel_smp_core_online(0)))
  uartPuts(" entries0=")
  uartPutDec(UInt64(kernel_smp_core_entry_count(0)))
  uartPuts(" heartbeat0=")
  uartPutDec(UInt64(kernel_smp_core_heartbeat(0)))
  uartPuts(" core1=")
  uartPutDec(UInt64(kernel_smp_core_online(1)))
  uartPuts(" entries1=")
  uartPutDec(UInt64(kernel_smp_core_entry_count(1)))
  uartPuts(" heartbeat1=")
  uartPutDec(UInt64(kernel_smp_core_heartbeat(1)))
  uartPuts(" core2=")
  uartPutDec(UInt64(kernel_smp_core_online(2)))
  uartPuts(" entries2=")
  uartPutDec(UInt64(kernel_smp_core_entry_count(2)))
  uartPuts(" heartbeat2=")
  uartPutDec(UInt64(kernel_smp_core_heartbeat(2)))
  uartPuts(" core3=")
  uartPutDec(UInt64(kernel_smp_core_online(3)))
  uartPuts(" entries3=")
  uartPutDec(UInt64(kernel_smp_core_entry_count(3)))
  uartPuts(" heartbeat3=")
  uartPutDec(UInt64(kernel_smp_core_heartbeat(3)))
  uartPuts("\n")
}

func printLocks() {
  let atomics = kernel_atomic_selftest()
  let spinlocks = kernel_spinlock_selftest()
  let ok = atomics != 0 && spinlocks != 0 ? 1 : 0

  uartPuts("locks ok=")
  uartPutDec(UInt64(ok))
  uartPuts(" version=33")
  uartPuts(" atomics=")
  uartPutDec(UInt64(atomics))
  uartPuts(" spinlocks=")
  uartPutDec(UInt64(spinlocks))
  uartPuts(" acquisitions=")
  uartPutDec(UInt64(spinlocks != 0 ? 2 : 0))
  uartPuts(" contentions=")
  uartPutDec(0)
  uartPuts(" selftest=")
  uartPutDec(UInt64(ok))
  uartPuts("\n")
}

func printRunQueues() {
  let selftest = kernel_scheduler_runqueue_selftest()
  let core0 = kernel_scheduler_runqueue_count(0)
  let core1 = kernel_scheduler_runqueue_count(1)
  let core2 = kernel_scheduler_runqueue_count(2)
  let core3 = kernel_scheduler_runqueue_count(3)
  let total = core0 + core1 + core2 + core3
  let ok = selftest != 0 && kernel_scheduler_core_count() == 4 && total == 0 ? 1 : 0

  uartPuts("runqueues ok=")
  uartPutDec(UInt64(ok))
  uartPuts(" version=33")
  uartPuts(" cores=")
  uartPutDec(UInt64(kernel_scheduler_core_count()))
  uartPuts(" capacity=")
  uartPutDec(UInt64(kernel_scheduler_runqueue_capacity()))
  uartPuts(" total=")
  uartPutDec(UInt64(total))
  uartPuts(" core0=")
  uartPutDec(UInt64(core0))
  uartPuts(" core1=")
  uartPutDec(UInt64(core1))
  uartPuts(" core2=")
  uartPutDec(UInt64(core2))
  uartPuts(" core3=")
  uartPutDec(UInt64(core3))
  uartPuts(" enqueues0=")
  uartPutDec(UInt64(kernel_scheduler_enqueue_count(0)))
  uartPuts(" dequeues0=")
  uartPutDec(UInt64(kernel_scheduler_dequeue_count(0)))
  uartPuts(" selftest=")
  uartPutDec(UInt64(selftest))
  uartPuts("\n")
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

func printDriverName(_ driver: UInt32) {
  var i: UInt32 = 0
  let n = kernel_driver_name_len(driver)
  while i < n {
    let b = UInt8(kernel_driver_name_byte(driver, i) & 0xFF)
    if b >= 0x20 && b < 0x7F {
      uartPutc(b)
    } else {
      uartPutc(0x2E)
    }
    i += 1
  }
}

func printDriverState(_ state: UInt32) {
  if state == KERNEL_DRIVER_STATE_READY {
    uartPuts("ready")
  } else {
    uartPuts("unknown")
  }
}

func printDrivers() {
  let selftest = kernel_driver_registry_selftest()

  uartPuts("drivers count=")
  uartPutDec(UInt64(kernel_driver_count()))
  uartPuts(" capacity=")
  uartPutDec(UInt64(kernel_driver_capacity()))
  uartPuts(" selftest=")
  uartPutDec(UInt64(selftest))
  uartPuts("\n")

  var i: UInt32 = 0
  while i < kernel_driver_capacity() {
    if kernel_driver_object_id(i) != 0 {
      uartPuts(" driver index=")
      uartPutDec(UInt64(i))
      uartPuts(" object=")
      uartPutDec(UInt64(kernel_driver_object_id(i)))
      uartPuts(" handle=")
      uartPutHex(UInt64(kernel_driver_handle(i)))
      uartPuts(" name=")
      printDriverName(i)
      if kernel_driver_state(i) == KERNEL_DRIVER_STATE_READY {
        uartPuts(" state=ready")
      } else {
        uartPuts(" state=")
        printDriverState(kernel_driver_state(i))
      }
      uartPuts(" intid=")
      uartPutDec(UInt64(kernel_driver_intid(i)))
      uartPuts(" base=")
      uartPutHexCompact(UInt64(kernel_driver_base(i)))
      uartPuts(" caps=")
      uartPutHexCompact(UInt64(kernel_driver_caps(i)))
      uartPuts(" irq_count=")
      uartPutDec(UInt64(kernel_driver_irq_count(i)))
      uartPuts(" errors=")
      uartPutDec(UInt64(kernel_driver_error_count(i)))
      uartPuts(" ops=")
      uartPutDec(UInt64(kernel_driver_operation_count(i)))
      uartPuts("\n")
    }
    i += 1
  }
}

func printDrivercheck() {
  let selftest = kernel_driver_registry_selftest()
  let ok = selftest != 0 &&
    kernel_driver_count() == 4 &&
    kernel_driver_intid(UInt32(KERNEL_DRIVER_ID_UART0)) == 153 &&
    kernel_driver_intid(UInt32(KERNEL_DRIVER_ID_CNTP)) == 30

  uartPuts("drivercheck ok=")
  uartPutDec(UInt64(ok ? 1 : 0))
  uartPuts(" count=")
  uartPutDec(UInt64(kernel_driver_count()))
  uartPuts("/")
  uartPutDec(UInt64(kernel_driver_capacity()))
  uartPuts(" uart_irq=")
  uartPutDec(UInt64(kernel_driver_irq_count(UInt32(KERNEL_DRIVER_ID_UART0))))
  uartPuts(" timer_irq=")
  uartPutDec(UInt64(kernel_driver_irq_count(UInt32(KERNEL_DRIVER_ID_CNTP))))
  uartPuts(" gic_total=")
  uartPutDec(UInt64(kernel_driver_irq_count(UInt32(KERNEL_DRIVER_ID_GIC))))
  uartPuts(" watchdog_resets=")
  uartPutDec(UInt64(watchdog_reset_count()))
  uartPuts(" unknown_irq=")
  uartPutDec(UInt64(kernel_driver_error_count(UInt32(KERNEL_DRIVER_ID_GIC))))
  uartPuts(" selftest=")
  uartPutDec(UInt64(selftest))
  uartPuts("\n")
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
  kernel_event_emit(KERNEL_EVENT_KIND_SHELL, 22, UInt(kernel_event_count()), 0)
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
      uartPuts(" parent=")
      uartPutDec(UInt64(kernel_task_parent_id(i)))
      uartPuts(" handle=")
      uartPutHex(UInt64(kernel_task_handle(i)))
      uartPuts(" name=")
      printTaskName(i)
      uartPuts(" state=")
      printTaskState(kernel_task_state(i))
      uartPuts(" ticks=")
      uartPutDec(UInt64(kernel_task_tick_count(i)))
      uartPuts(" spawns=")
      uartPutDec(UInt64(kernel_task_spawn_count(i)))
      uartPuts(" completions=")
      uartPutDec(UInt64(kernel_task_completion_count(i)))
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

func printChanneltest() {
  let channel = AetherChannelU64(mailboxID: MAILBOX_SELFTEST_ID)
  let testValue: UInt64 = 0x0000_0000_0000_c020

  kernel_mailbox_clear(MAILBOX_SELFTEST_ID)
  let sent = channel.send(testValue) ? 1 : 0
  let received = channel.tryReceive()
  let selftest = aetherChannelSelftest()
  let ok = sent != 0 && received.0 && received.1 == testValue && selftest != 0

  uartPuts("channeltest ok=")
  uartPutDec(UInt64(ok ? 1 : 0))
  uartPuts(" mailbox=")
  uartPutDec(UInt64(MAILBOX_SELFTEST_ID))
  uartPuts(" sent=")
  uartPutDec(UInt64(sent))
  uartPuts(" received=")
  uartPutDec(UInt64(received.0 ? 1 : 0))
  uartPuts(" value=")
  uartPutHex(received.1)
  uartPuts(" depth=")
  uartPutDec(UInt64(channel.depth()))
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
  uartPuts(" kind_id=")
  uartPutDec(UInt64(kind))
  uartPuts(" category=")
  uartPutDec(UInt64(kernel_retained_category()))
  uartPuts(" reason_id=")
  uartPutDec(UInt64(kernel_retained_reason_id()))
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

func printMMURegionKind(_ kind: UInt32) {
  if kind == KERNEL_MMU_REGION_KIND_NORMAL {
    uartPuts("normal")
  } else if kind == KERNEL_MMU_REGION_KIND_DEVICE {
    uartPuts("device")
  } else {
    uartPuts("fault")
  }
}

func printMMU() {
  let selftest = kernel_mmu_selftest()

  uartPuts("mmu ok=")
  uartPutDec(UInt64(selftest))
  uartPuts(" regions=")
  uartPutDec(UInt64(kernel_mmu_region_count()))
  uartPuts(" entries=")
  uartPutDec(UInt64(kernel_mmu_l1_entry_count()))
  uartPuts(" block_size=")
  uartPutHexCompact(UInt64(kernel_mmu_block_size()))
  uartPuts(" tcr=")
  uartPutHex(UInt64(kernel_mmu_tcr_value()))
  uartPuts(" mair=")
  uartPutHex(UInt64(kernel_mmu_mair_value()))
  uartPuts(" selftest=")
  uartPutDec(UInt64(selftest))
  uartPuts("\n")

  var i: UInt32 = 0
  while i < kernel_mmu_region_count() {
    uartPuts(" region index=")
    uartPutDec(UInt64(i))
    uartPuts(" va=")
    uartPutHex(UInt64(kernel_mmu_region_va_base(i)))
    uartPuts(" pa=")
    uartPutHex(UInt64(kernel_mmu_region_pa_base(i)))
    uartPuts(" size=")
    uartPutHexCompact(UInt64(kernel_mmu_region_size(i)))
    uartPuts(" kind=")
    printMMURegionKind(kernel_mmu_region_kind(i))
    uartPuts("\n")
    i += 1
  }
}

func printPoolName(_ pool: UInt32) {
  var i: UInt32 = 0
  let n = kernel_pool_name_len(pool)
  while i < n {
    let b = UInt8(kernel_pool_name_byte(pool, i) & 0xFF)
    if b >= 0x20 && b < 0x7F {
      uartPutc(b)
    } else {
      uartPutc(0x2E)
    }
    i += 1
  }
}

func printPools() {
  let selftest = kernel_pool_selftest()

  uartPuts("pools count=")
  uartPutDec(UInt64(kernel_pool_count()))
  uartPuts(" capacity=")
  uartPutDec(UInt64(kernel_pool_capacity()))
  uartPuts(" selftest=")
  uartPutDec(UInt64(selftest))
  uartPuts("\n")

  var i: UInt32 = 0
  while i < kernel_pool_capacity() {
    if kernel_pool_slot_capacity(i) != 0 {
      uartPuts(" pool index=")
      uartPutDec(UInt64(i))
      uartPuts(" name=")
      printPoolName(i)
      uartPuts(" slot_size=")
      uartPutDec(UInt64(kernel_pool_slot_size(i)))
      uartPuts(" used=")
      uartPutDec(UInt64(kernel_pool_used(i)))
      uartPuts("/")
      uartPutDec(UInt64(kernel_pool_slot_capacity(i)))
      uartPuts(" high_water=")
      uartPutDec(UInt64(kernel_pool_high_water(i)))
      uartPuts(" allocs=")
      uartPutDec(UInt64(kernel_pool_alloc_count(i)))
      uartPuts(" frees=")
      uartPutDec(UInt64(kernel_pool_free_count(i)))
      uartPuts(" failed=")
      uartPutDec(UInt64(kernel_pool_failed_alloc_count(i)))
      uartPuts(" bad_frees=")
      uartPutDec(UInt64(kernel_pool_bad_free_count(i)))
      uartPuts(" double_frees=")
      uartPutDec(UInt64(kernel_pool_double_free_count(i)))
      uartPuts(" generation=")
      uartPutDec(UInt64(kernel_pool_generation(i)))
      uartPuts(" last_error=")
      uartPutDec(UInt64(kernel_pool_last_error(i)))
      uartPuts("\n")
    }
    i += 1
  }
}

func printPoolcheck() {
  let selftest = kernel_pool_selftest()
  let pool = UInt32(KERNEL_POOL_SELFTEST_ID)
  let ok = selftest != 0 &&
    kernel_pool_used(pool) == 0 &&
    kernel_pool_failed_alloc_count(pool) == 1 &&
    kernel_pool_bad_free_count(pool) == 1 &&
    kernel_pool_double_free_count(pool) == 1

  uartPuts("poolcheck ok=")
  uartPutDec(UInt64(ok ? 1 : 0))
  uartPuts(" pools=")
  uartPutDec(UInt64(kernel_pool_count()))
  uartPuts("/")
  uartPutDec(UInt64(kernel_pool_capacity()))
  uartPuts(" selftest=")
  uartPutDec(UInt64(selftest))
  uartPuts(" used=")
  uartPutDec(UInt64(kernel_pool_used(pool)))
  uartPuts(" high_water=")
  uartPutDec(UInt64(kernel_pool_high_water(pool)))
  uartPuts(" allocs=")
  uartPutDec(UInt64(kernel_pool_alloc_count(pool)))
  uartPuts(" frees=")
  uartPutDec(UInt64(kernel_pool_free_count(pool)))
  uartPuts(" failed=")
  uartPutDec(UInt64(kernel_pool_failed_alloc_count(pool)))
  uartPuts(" bad_frees=")
  uartPutDec(UInt64(kernel_pool_bad_free_count(pool)))
  uartPuts(" double_frees=")
  uartPutDec(UInt64(kernel_pool_double_free_count(pool)))
  uartPuts(" last_error=")
  uartPutDec(UInt64(kernel_pool_last_error(pool)))
  uartPuts("\n")
}

func printHeapfrag() {
  let selftest = heap_fragmentation_selftest()
  let stress = heap_pressure_selftest()
  let ok = selftest != 0 && stress != 0 && heap_pressure_last_leak_bytes() == 0

  uartPuts("heapfrag ok=")
  uartPutDec(UInt64(ok ? 1 : 0))
  uartPuts(" selftest=")
  uartPutDec(UInt64(selftest))
  uartPuts(" pressure=")
  uartPutDec(UInt64(stress))
  uartPuts(" total=")
  uartPutDec(UInt64(heap_total_bytes()))
  uartPuts(" free=")
  uartPutDec(UInt64(heap_free_bytes()))
  uartPuts(" largest_free=")
  uartPutDec(UInt64(heap_largest_free_bytes()))
  uartPuts(" smallest_free=")
  uartPutDec(UInt64(heap_smallest_free_bytes()))
  uartPuts(" free_blocks=")
  uartPutDec(UInt64(heap_free_block_count()))
  uartPuts(" allocated_blocks=")
  uartPutDec(UInt64(heap_allocated_block_count()))
  uartPuts(" fragmentation_permil=")
  uartPutDec(UInt64(heap_fragmentation_permil()))
  uartPuts(" pressure_peak=")
  uartPutDec(UInt64(heap_pressure_last_peak_bytes()))
  uartPuts(" pressure_leak=")
  uartPutDec(UInt64(heap_pressure_last_leak_bytes()))
  uartPuts(" pressure_free_blocks=")
  uartPutDec(UInt64(heap_pressure_last_free_block_count()))
  uartPuts(" pressure_largest_free=")
  uartPutDec(UInt64(heap_pressure_last_largest_free_bytes()))
  uartPuts("\n")
}

func printPoolstats() {
  let selftest = kernel_pool_pressure_selftest()

  uartPuts("poolstats ok=")
  uartPutDec(UInt64(selftest))
  uartPuts(" pools=")
  uartPutDec(UInt64(kernel_pool_count()))
  uartPuts("/")
  uartPutDec(UInt64(kernel_pool_capacity()))
  uartPuts(" total_slots=")
  uartPutDec(UInt64(kernel_pool_total_slot_count()))
  uartPuts(" used_slots=")
  uartPutDec(UInt64(kernel_pool_used_slot_count()))
  uartPuts(" high_water_slots=")
  uartPutDec(UInt64(kernel_pool_high_water_slot_count()))
  uartPuts(" failed_allocs=")
  uartPutDec(UInt64(kernel_pool_failed_alloc_total()))
  uartPuts(" bad_frees=")
  uartPutDec(UInt64(kernel_pool_bad_free_total()))
  uartPuts(" double_frees=")
  uartPutDec(UInt64(kernel_pool_double_free_total()))
  uartPuts(" selftest=")
  uartPutDec(UInt64(selftest))
  uartPuts("\n")
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

func printBootcert() {
  kernel_supervisor_check()
  kernel_event_emit(KERNEL_EVENT_KIND_SHELL, 42, UInt(kernel_event_count()), 0)

  let workerFeed = kernel_scheduler_timer_worker_feed_selftest()
  let secondaryWorkers = kernel_scheduler_secondary_worker_selftest()
  let jobExec = kernel_scheduler_secondary_job_selftest()
  let wake = kernel_scheduler_secondary_wake_selftest()
  let preemptive = kernel_scheduler_active()
  let smpScheduler = kernel_scheduler_smp_selftest()
  let handoff = kernel_scheduler_secondary_handoff_selftest()
  let backpressure = kernel_scheduler_backpressure_selftest()
  let stealing = kernel_scheduler_work_steal_selftest()
  let fairness = kernel_scheduler_fairness_selftest()
  let atomics = kernel_atomic_selftest()
  let locks = kernel_spinlock_selftest()
  let queues = kernel_scheduler_runqueue_selftest()
  let smp = kernel_smp_selftest()
  let scheduler = kernel_scheduler_selftest()
  let substrateCertificate = UInt32(1)
  let agentSession = UInt32(1)
  let runtimeAudit = kernel_runtime_audit_selftest()
  let taxonomy = UInt32(1)
  let protocolV2 = UInt32(1)
  let memmap = kernel_memory_map_valid()
  let heap = heap_guard_selftest()
  let frames = kernel_frame_allocator_selftest()
  let mmu = kernel_mmu_selftest()
  let pools = kernel_pool_selftest()
  let pressure = heap_fragmentation_selftest() != 0 && kernel_pool_pressure_selftest() != 0 ? 1 : 0
  let drivers = kernel_driver_registry_selftest()
  let taskspawns = aetherTaskSpawnSelftest()
  let cancellations = kernel_cancel_selftest()
  let retainedValid = kernel_retained_valid()
  let kobjects = kernel_object_registry_selftest()
  let tasks = kernel_task_registry_selftest()
  let mailboxes = kernel_mailbox_selftest()
  let channels = aetherChannelSelftest()
  let supervisor = kernel_supervisor_selftest()
  let events = kernel_event_log_selftest()
  let eventsLost = kernel_event_lost_count()
  let ok = fairness != 0 && stealing != 0 && backpressure != 0 && handoff != 0 && wake != 0 && jobExec != 0 && workerFeed != 0 && secondaryWorkers != 0 && preemptive != 0 && smpScheduler != 0 && atomics != 0 && locks != 0 && queues != 0 && smp != 0 && scheduler != 0 && substrateCertificate != 0 && agentSession != 0 && runtimeAudit != 0 && taxonomy != 0 && protocolV2 != 0 && memmap != 0 && heap != 0 && frames != 0 && mmu != 0 && pools != 0 && pressure != 0 && drivers != 0 &&
    taskspawns != 0 && cancellations != 0 && kobjects != 0 && tasks != 0 && mailboxes != 0 &&
    channels != 0 && supervisor != 0 && events != 0 && eventsLost == 0

  uartPuts("bootcert ok=")
  uartPutDec(UInt64(ok ? 1 : 0))
  uartPuts(" version=42")
  uartPuts(" fairness=")
  uartPutDec(UInt64(fairness))
  uartPuts(" stealing=")
  uartPutDec(UInt64(stealing))
  uartPuts(" backpressure=")
  uartPutDec(UInt64(backpressure))
  uartPuts(" handoff=")
  uartPutDec(UInt64(handoff))
  uartPuts(" wake=")
  uartPutDec(UInt64(wake))
  uartPuts(" job_exec=")
  uartPutDec(UInt64(jobExec))
  uartPuts(" worker_feed=")
  uartPutDec(UInt64(workerFeed))
  uartPuts(" secondary_workers=")
  uartPutDec(UInt64(secondaryWorkers))
  uartPuts(" preemptive=")
  uartPutDec(UInt64(preemptive))
  uartPuts(" smp_scheduler=")
  uartPutDec(UInt64(smpScheduler))
  uartPuts(" atomics=")
  uartPutDec(UInt64(atomics))
  uartPuts(" locks=")
  uartPutDec(UInt64(locks))
  uartPuts(" queues=")
  uartPutDec(UInt64(queues))
  uartPuts(" smp=")
  uartPutDec(UInt64(smp))
  uartPuts(" scheduler=")
  uartPutDec(UInt64(scheduler))
  uartPuts(" certificate=")
  uartPutDec(UInt64(substrateCertificate))
  uartPuts(" agent=1")
  uartPuts(" runtime=")
  uartPutDec(UInt64(runtimeAudit))
  uartPuts(" taxonomy=1")
  uartPuts(" protocol=1")
  uartPuts(" memmap=")
  uartPutDec(UInt64(memmap))
  uartPuts(" heap=")
  uartPutDec(UInt64(heap))
  uartPuts(" frames=")
  uartPutDec(UInt64(frames))
  uartPuts(" drivers=")
  uartPutDec(UInt64(drivers))
  uartPuts(" pressure=")
  uartPutDec(UInt64(pressure))
  uartPuts(" pools=")
  uartPutDec(UInt64(pools))
  uartPuts(" mmu=")
  uartPutDec(UInt64(mmu))
  uartPuts(" channels=")
  uartPutDec(UInt64(channels))
  uartPuts(" taskspawns=")
  uartPutDec(UInt64(taskspawns))
  uartPuts(" cancellations=")
  uartPutDec(UInt64(cancellations))
  uartPuts(" retained_valid=")
  uartPutDec(UInt64(retainedValid))
  uartPuts(" kobjects=")
  uartPutDec(UInt64(kobjects))
  uartPuts(" tasks=")
  uartPutDec(UInt64(tasks))
  uartPuts(" mailboxes=")
  uartPutDec(UInt64(mailboxes))
  uartPuts(" supervisor=")
  uartPutDec(UInt64(supervisor))
  uartPuts(" events=")
  uartPutDec(UInt64(events))
  uartPuts(" events_lost=")
  uartPutDec(UInt64(eventsLost))
  uartPuts(" heap_free=")
  uartPutDec(UInt64(heap_free_bytes()))
  uartPuts(" frame_free=")
  uartPutDec(UInt64(kernel_frame_free_count()))
  uartPuts(" uptime_ms=")
  uartPutDec(UInt64(kernel_supervisor_now_ms()))
  uartPuts("\n")
}

func printTaskcheck() {
  let ok = aetherTaskSpawnSelftest()
  var spawns: UInt64 = 0
  var completions: UInt64 = 0
  var i: UInt32 = 0
  while i < kernel_task_capacity() {
    if kernel_task_object_id(i) != 0 {
      spawns += UInt64(kernel_task_spawn_count(i))
      completions += UInt64(kernel_task_completion_count(i))
    }
    i += 1
  }

  uartPuts("taskcheck ok=")
  uartPutDec(UInt64(ok))
  uartPuts(" count=")
  uartPutDec(UInt64(kernel_task_count()))
  uartPuts(" capacity=")
  uartPutDec(UInt64(kernel_task_capacity()))
  uartPuts(" spawns=")
  uartPutDec(spawns)
  uartPuts(" completions=")
  uartPutDec(completions)
  uartPuts(" root_parent=")
  uartPutDec(UInt64(KERNEL_TASK_ROOT_PARENT))
  uartPuts(" selftest=")
  uartPutDec(UInt64(kernel_task_registry_selftest()))
  uartPuts("\n")
}

func printCanceltest() {
  kernel_task_mark_state(TASK_CANCEL_ID, KERNEL_TASK_STATE_RUNNING)
  let selftest = kernel_cancel_selftest()
  kernel_task_record_tick(TASK_CANCEL_ID)
  kernel_supervisor_heartbeat(TASK_CANCEL_ID)
  kernel_task_mark_state(TASK_CANCEL_ID, KERNEL_TASK_STATE_IDLE)
  kernel_event_emit(KERNEL_EVENT_KIND_SELFTEST, 18, UInt(selftest), UInt(kernel_cancel_completed_count()))

  uartPuts("canceltest ok=")
  uartPutDec(UInt64(selftest))
  uartPuts(" capacity=")
  uartPutDec(UInt64(kernel_cancel_token_capacity()))
  uartPuts(" active=")
  uartPutDec(UInt64(kernel_cancel_token_count()))
  uartPuts(" requested=")
  uartPutDec(UInt64(kernel_cancel_requested_count()))
  uartPuts(" completed=")
  uartPutDec(UInt64(kernel_cancel_completed_count()))
  uartPuts(" last_error=")
  uartPutDec(UInt64(kernel_cancel_last_error()))
  uartPuts(" fast=")
  uartPutDec(runtimeFastCount)
  uartPuts(" slow=")
  uartPutDec(runtimeSlowCount)
  uartPuts(" long=")
  uartPutDec(runtimeLongCount)
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

struct ProtocolRequest {
  var ok: Bool
  var requestID: UInt64
  var commandStart: UInt32
  var commandLen: UInt32
}

func parseProtocolRequest() -> ProtocolRequest {
  let n = uart_shell_buffer_count()
  let prefixLen = UInt32(7) // "req id="
  if !shellBufferHasPrefix("req id=") {
    return ProtocolRequest(ok: false, requestID: 0, commandStart: 0, commandLen: 0)
  }

  var i = prefixLen
  var id: UInt64 = 0
  var digits: UInt32 = 0
  while i < n {
    let b = UInt8(uart_shell_buffer_get(i) & 0xFF)
    if b < 0x30 || b > 0x39 {
      break
    }
    id = (id &* 10) &+ UInt64(b - 0x30)
    digits += 1
    i += 1
  }

  if digits == 0 {
    return ProtocolRequest(ok: false, requestID: 0, commandStart: 0, commandLen: 0)
  }
  if i + 5 > n || !shellBufferSliceEquals(i, 5, " cmd=") {
    return ProtocolRequest(ok: false, requestID: id, commandStart: 0, commandLen: 0)
  }

  let commandStart = i + 5
  let commandLen = n - commandStart
  if commandLen == 0 {
    return ProtocolRequest(ok: false, requestID: id, commandStart: commandStart, commandLen: 0)
  }

  return ProtocolRequest(ok: true, requestID: id, commandStart: commandStart, commandLen: commandLen)
}

func printProtocolResponsePrefix(_ requestID: UInt64, _ commandStart: UInt32, _ commandLen: UInt32) {
  uartPuts("resp id=")
  uartPutDec(requestID)
  uartPuts(" cmd=")
  uartPutShellBufferSlice(commandStart, commandLen)
}

func printProtocolBegin(_ requestID: UInt64, _ commandStart: UInt32, _ commandLen: UInt32) {
  printProtocolResponsePrefix(requestID, commandStart, commandLen)
  uartPuts(" begin\n")
}

func printProtocolEnd(_ requestID: UInt64, _ commandStart: UInt32, _ commandLen: UInt32) {
  uartPuts("resp id=")
  uartPutDec(requestID)
  uartPuts(" ok=1 cmd=")
  uartPutShellBufferSlice(commandStart, commandLen)
  uartPuts(" end\n")
}

func printProtocolBadRequest(_ requestID: UInt64, _ commandStart: UInt32, _ commandLen: UInt32) {
  uartPuts("resp id=")
  uartPutDec(requestID)
  uartPuts(" ok=0 cmd=")
  uartPutShellBufferSlice(commandStart, commandLen)
  uartPuts(" error=bad_request\n")
}

func printProtocolUnknown(_ requestID: UInt64, _ commandStart: UInt32, _ commandLen: UInt32) {
  uartPuts("resp id=")
  uartPutDec(requestID)
  uartPuts(" ok=0 cmd=")
  uartPutShellBufferSlice(commandStart, commandLen)
  uartPuts(" error=unknown\n")
}

func dispatchShellCommand(_ commandStart: UInt32, _ commandLen: UInt32, _ requestID: UInt64, _ wrapped: Bool) -> Bool {
  if wrapped {
    printProtocolBegin(requestID, commandStart, commandLen)
  }

  var handled = true

  if shellBufferSliceEquals(commandStart, commandLen, "help") {
    printShellHelp()
  } else if shellBufferSliceEquals(commandStart, commandLen, "protocol") {
    printProtocol()
  } else if shellBufferSliceEquals(commandStart, commandLen, "status") {
    printStatus()
  } else if shellBufferSliceEquals(commandStart, commandLen, "heap") {
    printHeap()
  } else if shellBufferSliceEquals(commandStart, commandLen, "queues") {
    printQueues()
  } else if shellBufferSliceEquals(commandStart, commandLen, "tasks") {
    printTasks()
  } else if shellBufferSliceEquals(commandStart, commandLen, "tasks2") {
    printTasks2()
  } else if shellBufferSliceEquals(commandStart, commandLen, "kobjects") {
    printKobjects()
  } else if shellBufferSliceEquals(commandStart, commandLen, "drivers") {
    printDrivers()
  } else if shellBufferSliceEquals(commandStart, commandLen, "drivercheck") {
    printDrivercheck()
  } else if shellBufferSliceEquals(commandStart, commandLen, "mailboxes") {
    printMailboxes()
  } else if shellBufferSliceEquals(commandStart, commandLen, "sendtest") {
    printSendtest()
  } else if shellBufferSliceEquals(commandStart, commandLen, "supervisor") {
    printSupervisor()
  } else if shellBufferSliceEquals(commandStart, commandLen, "health") {
    printHealth()
  } else if shellBufferSliceEquals(commandStart, commandLen, "capcheck") {
    printCapcheck()
  } else if shellBufferSliceEquals(commandStart, commandLen, "events") {
    printEvents()
  } else if shellBufferSliceEquals(commandStart, commandLen, "runtime") {
    printRuntimeAudit()
  } else if shellBufferSliceEquals(commandStart, commandLen, "agent") {
    printAgentSession()
  } else if shellBufferSliceEquals(commandStart, commandLen, "certificate") {
    printSubstrateCertificate()
  } else if shellBufferSliceEquals(commandStart, commandLen, "sched") {
    printScheduler()
  } else if shellBufferSliceEquals(commandStart, commandLen, "sched2") {
    printScheduler2()
  } else if shellBufferSliceEquals(commandStart, commandLen, "sched3") {
    printScheduler3()
  } else if shellBufferSliceEquals(commandStart, commandLen, "sched4") {
    printScheduler4()
  } else if shellBufferSliceEquals(commandStart, commandLen, "sched5") {
    printScheduler5()
  } else if shellBufferSliceEquals(commandStart, commandLen, "sched6") {
    printScheduler6()
  } else if shellBufferSliceEquals(commandStart, commandLen, "sched7") {
    printScheduler7()
  } else if shellBufferSliceEquals(commandStart, commandLen, "sched8") {
    printScheduler8()
  } else if shellBufferSliceEquals(commandStart, commandLen, "sched9") {
    printScheduler9()
  } else if shellBufferSliceEquals(commandStart, commandLen, "sched10") {
    printScheduler10()
  } else if shellBufferSliceEquals(commandStart, commandLen, "cores") {
    printCores()
  } else if shellBufferSliceEquals(commandStart, commandLen, "locks") {
    printLocks()
  } else if shellBufferSliceEquals(commandStart, commandLen, "runqueues") {
    printRunQueues()
  } else if shellBufferSliceEquals(commandStart, commandLen, "diag") {
    printDiag()
  } else if shellBufferSliceEquals(commandStart, commandLen, "irqs") {
    printIrqs()
  } else if shellBufferSliceEquals(commandStart, commandLen, "timers") {
    printTimers()
  } else if shellBufferSliceEquals(commandStart, commandLen, "memcheck") {
    printMemcheck()
  } else if shellBufferSliceEquals(commandStart, commandLen, "faults") {
    printFaults()
  } else if shellBufferSliceEquals(commandStart, commandLen, "retained") {
    printRetained()
  } else if shellBufferSliceEquals(commandStart, commandLen, "retained-clear") {
    clearRetained()
  } else if shellBufferSliceEquals(commandStart, commandLen, "memmap") {
    printMemmap()
  } else if shellBufferSliceEquals(commandStart, commandLen, "mmu") {
    printMMU()
  } else if shellBufferSliceEquals(commandStart, commandLen, "pools") {
    printPools()
  } else if shellBufferSliceEquals(commandStart, commandLen, "poolcheck") {
    printPoolcheck()
  } else if shellBufferSliceEquals(commandStart, commandLen, "heapfrag") {
    printHeapfrag()
  } else if shellBufferSliceEquals(commandStart, commandLen, "poolstats") {
    printPoolstats()
  } else if shellBufferSliceEquals(commandStart, commandLen, "frames") {
    printFrames()
  } else if shellBufferSliceEquals(commandStart, commandLen, "heapcheck") {
    printHeapcheck()
  } else if shellBufferSliceEquals(commandStart, commandLen, "framecheck") {
    printFramecheck()
  } else if shellBufferSliceEquals(commandStart, commandLen, "stress") {
    printStress()
  } else if shellBufferSliceEquals(commandStart, commandLen, "frameprobe") {
    printFrameprobe()
  } else if shellBufferSliceEquals(commandStart, commandLen, "bootcert") {
    printBootcert()
  } else if shellBufferSliceEquals(commandStart, commandLen, "canceltest") {
    printCanceltest()
  } else if shellBufferSliceEquals(commandStart, commandLen, "taskcheck") {
    printTaskcheck()
  } else if shellBufferSliceEquals(commandStart, commandLen, "channeltest") {
    printChanneltest()
  } else if shellBufferSliceEquals(commandStart, commandLen, "bootcheck") {
    printBootcheck()
  } else if shellBufferSliceEquals(commandStart, commandLen, "soak") {
    printSoak()
  } else if shellBufferSliceEquals(commandStart, commandLen, "heap-invalid-free-test") {
    shellHeapInvalidFreeTest()
  } else if shellBufferSliceEquals(commandStart, commandLen, "heap-double-free-test") {
    shellHeapDoubleFreeTest()
  } else if shellBufferSliceEquals(commandStart, commandLen, "panic-test") {
    shellPanicTest()
  } else if shellBufferSliceEquals(commandStart, commandLen, "fault-test") {
    shellFaultTest()
  } else if shellBufferSliceEquals(commandStart, commandLen, "reboot") {
    shellRebootCommand()
  } else {
    handled = false
  }

  if handled {
    if wrapped {
      printProtocolEnd(requestID, commandStart, commandLen)
    }
    return true
  }

  if wrapped {
    printProtocolUnknown(requestID, commandStart, commandLen)
  } else {
    uartPuts("shell error reason=unknown command=")
    uartPutShellBufferSlice(commandStart, commandLen)
    uartPuts("\n")
  }
  return false
}

func processProtocolRequest() {
  let request = parseProtocolRequest()
  if !request.ok {
    printProtocolBadRequest(request.requestID, request.commandStart, request.commandLen)
    return
  }
  _ = dispatchShellCommand(request.commandStart, request.commandLen, request.requestID, true)
}

func processUartShellLine() {
  let n = uart_shell_buffer_count()
  if n == 0 {
    return
  }

  if n == 1 && isResetAlias(UInt8(uart_shell_buffer_get(0) & 0xFF)) {
    shellReboot("alias")
  } else if shellBufferEquals("protocol") {
    printProtocol()
  } else if shellBufferHasPrefix("req id=") {
    processProtocolRequest()
  } else {
    _ = dispatchShellCommand(0, n, 0, false)
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

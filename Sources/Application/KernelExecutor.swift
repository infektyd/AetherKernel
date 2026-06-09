//===----------------------------------------------------------------------===//
// AetherKernel — cooperative global executor (Runtime V2, Swift-owned state).
//
// Single-threaded FIFO ready ring plus a fixed delayed-job queue. CNTP ownership
// is shared through the kernel timer arbiter: this executor owns the EXECUTOR
// client deadline, while TimerSleep.swift owns the SLEEP client deadline.
//
// Path B: the embedded runtime dispatches through ExecutorImpl.h swiftcall hooks
// in executor.c; those remain thin C trampolines into the @_cdecl entry points
// below. Job execution uses UnownedJob.runSynchronously on this SerialExecutor.
//===----------------------------------------------------------------------===//
@_spi(ExperimentalCustomExecutors) import _Concurrency
import Support

private let READY_CAPACITY: Int = 64
private let DELAY_CAPACITY: Int = 32
private let TIMER_CLIENT_EXECUTOR: UInt32 = KERNEL_TIMER_CLIENT_EXECUTOR

private struct DelayedSlot {
  var deadlineTicks: UInt64 = 0
  var job: UnsafeMutableRawPointer? = nil
}

nonisolated(unsafe) private var ready: [UnsafeMutableRawPointer?] =
  Array(repeating: nil, count: READY_CAPACITY)
nonisolated(unsafe) private var readyHead: UInt32 = 0
nonisolated(unsafe) private var readyTail: UInt32 = 0

nonisolated(unsafe) private var delayed: [DelayedSlot] =
  Array(repeating: DelayedSlot(), count: DELAY_CAPACITY)
nonisolated(unsafe) private var delayedCount: UInt32 = 0

nonisolated(unsafe) private var swiftEnqueueAnnounced = false

//===----------------------------------------------------------------------===//
// Panic + timer helpers
//===----------------------------------------------------------------------===//

private func executorPanic(_ msg: StaticString) {
  uartPuts("EXECUTOR PANIC: ")
  uartPuts(msg)
  uartPuts("\n")
  while true { wait_for_interrupt() }
}

private func nsToTicks(_ ns: UInt64) -> UInt64 {
  (ns &* UInt64(timerFrequency())) / 1_000_000_000
}

//===----------------------------------------------------------------------===//
// Ready ring (FIFO). Caller must hold an irq_save() critical section.
//===----------------------------------------------------------------------===//

private func readyPushUnsafe(_ job: UnsafeMutableRawPointer) {
  let next = (readyTail &+ 1) % UInt32(READY_CAPACITY)
  if next == readyHead {
    executorPanic("ready queue overflow")
  }
  ready[Int(readyTail)] = job
  readyTail = next
}

private func readyPopUnsafe() -> UnsafeMutableRawPointer? {
  if readyHead == readyTail {
    return nil
  }
  let job = ready[Int(readyHead)]
  ready[Int(readyHead)] = nil
  readyHead = (readyHead &+ 1) % UInt32(READY_CAPACITY)
  return job
}

private func readyCountUnsafe() -> UInt32 {
  if readyTail >= readyHead {
    return readyTail &- readyHead
  }
  return UInt32(READY_CAPACITY) &- readyHead &+ readyTail
}

private func readyPushLocked(_ job: UnsafeMutableRawPointer) {
  let flags = irq_save()
  if !swiftEnqueueAnnounced {
    swiftEnqueueAnnounced = true
    uartPuts("KEXEC swift executor live\n")
  }
  readyPushUnsafe(job)
  irq_restore(flags)
}

//===----------------------------------------------------------------------===//
// Delay queue helpers. Caller must hold an irq_save() critical section.
//===----------------------------------------------------------------------===//

private func delayPushUnsafe(_ deadlineTicks: UInt64, _ job: UnsafeMutableRawPointer) {
  if delayedCount >= UInt32(DELAY_CAPACITY) {
    executorPanic("delay queue overflow")
  }
  delayed[Int(delayedCount)].deadlineTicks = deadlineTicks
  delayed[Int(delayedCount)].job = job
  delayedCount &+= 1
}

private func delayMinDeadlineUnsafe() -> UInt64 {
  if delayedCount == 0 {
    return 0
  }
  var min = delayed[0].deadlineTicks
  var i: UInt32 = 1
  while i < delayedCount {
    if delayed[Int(i)].deadlineTicks < min {
      min = delayed[Int(i)].deadlineTicks
    }
    i &+= 1
  }
  return min
}

private func executorRearmTimerUnsafe() {
  if delayedCount == 0 {
    kernel_timer_clear_deadline(TIMER_CLIENT_EXECUTOR)
    return
  }
  kernel_timer_set_deadline(TIMER_CLIENT_EXECUTOR, UInt(delayMinDeadlineUnsafe()))
}

private func delayScheduleDeadline(_ deadlineTicks: UInt64, _ job: UnsafeMutableRawPointer) {
  let flags = irq_save()
  delayPushUnsafe(deadlineTicks, job)
  executorRearmTimerUnsafe()
  irq_restore(flags)
}

private func delayScheduleNs(_ delayNs: UInt64, _ job: UnsafeMutableRawPointer) {
  if delayNs == 0 {
    readyPushLocked(job)
    return
  }
  var ticks = nsToTicks(delayNs)
  if ticks == 0 {
    ticks = 1
  }
  let deadline = UInt64(kernel_timer_now()) &+ ticks
  delayScheduleDeadline(deadline, job)
}

private func promoteDueJobs() {
  let flags = irq_save()
  let now = UInt64(kernel_timer_now())
  var i: UInt32 = 0
  while i < delayedCount {
    if delayed[Int(i)].deadlineTicks <= now {
      readyPushUnsafe(delayed[Int(i)].job!)
      delayedCount &-= 1
      if i < delayedCount {
        delayed[Int(i)] = delayed[Int(delayedCount)]
      }
    } else {
      i &+= 1
    }
  }
  executorRearmTimerUnsafe()
  irq_restore(flags)
}

private func armNextDeadline() {
  let flags = irq_save()
  executorRearmTimerUnsafe()
  irq_restore(flags)
}

private func runJob(_ ptr: UnsafeMutableRawPointer) {
  let unowned = unsafeBitCast(ptr, to: UnownedJob.self)
  unowned.runSynchronously(on: KernelExecutor.shared.asUnownedSerialExecutor())
}

//===----------------------------------------------------------------------===//
// @_cdecl entry points (executor.c trampolines)
//===----------------------------------------------------------------------===//

@_cdecl("kernel_executor_enqueue")
func kernel_executor_enqueue(_ job: UnsafeMutableRawPointer) {
  readyPushLocked(job)
}

@_cdecl("kernel_executor_enqueue_delay_ns")
func kernel_executor_enqueue_delay_ns(_ ns: UInt64, _ job: UnsafeMutableRawPointer) {
  delayScheduleNs(ns, job)
}

@_cdecl("kernel_executor_enqueue_deadline_ns")
func kernel_executor_enqueue_deadline_ns(
  _ targetNs: UInt64, _ nowNs: UInt64, _ job: UnsafeMutableRawPointer
) {
  if targetNs <= nowNs {
    kernel_executor_enqueue(job)
    return
  }
  kernel_executor_enqueue_delay_ns(targetNs &- nowNs, job)
}

@_cdecl("kernel_executor_on_timer_irq")
func kernel_executor_on_timer_irq() {
  promoteDueJobs()
}

@_cdecl("kernel_executor_donate_until")
func kernel_executor_donate_until(
  _ condition: @convention(c) (UnsafeMutableRawPointer?) -> CInt,
  _ context: UnsafeMutableRawPointer?
) {
  while true {
    promoteDueJobs()
    let flags = irq_save()
    let job = readyPopUnsafe()
    irq_restore(flags)
    if let job = job {
      runJob(job)
      continue
    }
    if condition(context) != 0 {
      return
    }
    armNextDeadline()
    wait_for_interrupt()
  }
}

@_cdecl("kernel_executor_drain_main")
func kernel_executor_drain_main() -> Never {
  while true {
    promoteDueJobs()
    let flags = irq_save()
    let job = readyPopUnsafe()
    irq_restore(flags)
    if let job = job {
      runJob(job)
      continue
    }
    armNextDeadline()
    wait_for_interrupt()
  }
}

@_cdecl("kernel_executor_ready_count")
func kernel_executor_ready_count() -> UInt32 {
  let flags = irq_save()
  let count = readyCountUnsafe()
  irq_restore(flags)
  return count
}

@_cdecl("kernel_executor_delayed_count")
func kernel_executor_delayed_count() -> UInt32 {
  let flags = irq_save()
  let count = delayedCount
  irq_restore(flags)
  return count
}

//===----------------------------------------------------------------------===//
// SerialExecutor + factory install (optional Path A enqueue surface).
//===----------------------------------------------------------------------===//

final class KernelExecutor: SerialExecutor, MainExecutor, TaskExecutor {
  static let shared = KernelExecutor()

  func enqueue(_ job: UnownedJob) {
    readyPushLocked(unsafeBitCast(job, to: UnsafeMutableRawPointer.self))
  }

  func enqueue(_ job: consuming ExecutorJob) {
    readyPushLocked(unsafeBitCast(UnownedJob(job), to: UnsafeMutableRawPointer.self))
  }

  func run() throws {}
  func stop() {}
}

struct KernelExecutorFactory: ExecutorFactory {
  static var mainExecutor: any MainExecutor { KernelExecutor.shared }
  static var defaultExecutor: any TaskExecutor { KernelExecutor.shared }
}

// Must run after kernel_memory_init() (class allocation) and before the first
// spawnAetherTask: the runtime's lazy default-init is a no-op once
// Task._defaultExecutor is set.
func installKernelExecutor() {
  // Touch the queue globals now so their lazy init (swift_once + allocation)
  // happens in main context — never inside the timer IRQ path.
  ready[0] = nil
  delayed[0] = DelayedSlot()
  _createExecutors(factory: KernelExecutorFactory.self)
}
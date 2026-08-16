//===----------------------------------------------------------------------===//
// AetherKernel — cooperative global executor (Runtime V2, Swift-owned state).
//
// Single-threaded FIFO ready ring. The former delayed-job queue was removed:
// Task.sleep is unavailable in embedded and TimerSleep.swift owns timed wakeups
// (CNTP SLEEP client), so the delay hooks were hardware-proven dead 2026-06-09
// (KEXEC-DELAY-PATH hits = 0 over boot + canceltest/taskcheck/channeltest/
// sendtest/soak/stress + soak window). They now panic loudly if ever called.
//
// Path B: the embedded runtime dispatches through ExecutorImpl.h swiftcall hooks
// in executor.c; those remain thin C trampolines into the @_cdecl entry points
// below. Job execution uses UnownedJob.runSynchronously on this SerialExecutor.
//===----------------------------------------------------------------------===//
@_spi(ExperimentalCustomExecutors) import _Concurrency
import Support

private let READY_CAPACITY: Int = 64

nonisolated(unsafe) private var ready: [UnsafeMutableRawPointer?] =
  Array(repeating: nil, count: READY_CAPACITY)
nonisolated(unsafe) private var readyHead: UInt32 = 0
nonisolated(unsafe) private var readyTail: UInt32 = 0

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

// Delay hooks were hardware-proven dead (slice 3, 2026-06-09: zero hits over
// boot + full concurrency exercisers + soak). They panic loudly so any future
// runtime path that routes here fails visibly instead of silently mis-timing.
@_cdecl("kernel_executor_enqueue_delay_ns")
func kernel_executor_enqueue_delay_ns(_ ns: UInt64, _ job: UnsafeMutableRawPointer) {
  executorPanic("delay enqueue is unsupported (proven dead; use TimerSleep)")
}

@_cdecl("kernel_executor_enqueue_deadline_ns")
func kernel_executor_enqueue_deadline_ns(
  _ targetNs: UInt64, _ nowNs: UInt64, _ job: UnsafeMutableRawPointer
) {
  executorPanic("deadline enqueue is unsupported (proven dead; use TimerSleep)")
}

// Kept as a no-op: IRQHandler.swift's ordering contract still calls through
// executor_on_timer_irq, and the drain loop's wfi wakes on the scheduler tick.
@_cdecl("kernel_executor_on_timer_irq")
func kernel_executor_on_timer_irq() {
}

@_cdecl("kernel_executor_donate_until")
func kernel_executor_donate_until(
  _ condition: @convention(c) (UnsafeMutableRawPointer?) -> CInt,
  _ context: UnsafeMutableRawPointer?
) {
  while true {
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
    wait_for_interrupt()
  }
}

@_cdecl("kernel_executor_drain_main")
func kernel_executor_drain_main() -> Never {
  while true {
    let flags = irq_save()
    let job = readyPopUnsafe()
    irq_restore(flags)
    if let job = job {
      runJob(job)
      continue
    }
    // One HDMI glyph, then re-check jobs. Never tight-loop paint.
    _ = kernel_vc_console_paint_if_needed()
    let flags2 = irq_save()
    let job2 = readyPopUnsafe()
    irq_restore(flags2)
    if let job2 = job2 {
      runJob(job2)
      continue
    }
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

// ABI kept for shell diagnostics; the delayed queue no longer exists.
@_cdecl("kernel_executor_delayed_count")
func kernel_executor_delayed_count() -> UInt32 {
  0
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
  // Touch the queue global now so its lazy init (swift_once + allocation)
  // happens in main context — never inside an IRQ path.
  ready[0] = nil
  _createExecutors(factory: KernelExecutorFactory.self)
}
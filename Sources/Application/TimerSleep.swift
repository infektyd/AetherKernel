//===----------------------------------------------------------------------===//
// Timer-backed async sleep. Task.sleep is unavailable in Embedded Swift, so we
// build suspension from withUnsafeContinuation + the CNTP timer IRQ.
//
// Milestone contract: this file owns CNTP. The Swift runtime delay hooks in the
// C executor intentionally panic rather than touching the same timer.
// Single slot: supports one concurrent sleeper (the heartbeat task). A second
// sleeper is a kernel bug, so fail loudly instead of orphaning a continuation.
//
// All CNTP register work lives in C (timersleep_hw.c): the inline-asm helpers get
// miscompiled (the mrs/msr dropped) when inlined into the @_cdecl IRQ-path Swift
// function, so Swift here only stores/resumes the continuation.
//===----------------------------------------------------------------------===//
import Support
import _Concurrency

// Touched in task context and in IRQ context (serviceTimerSleeper).
nonisolated(unsafe) var sleeperCont: UnsafeContinuation<Void, Never>? = nil

// Suspend the current async task for `secs` seconds of CNTP time.
func timerSleepSeconds(_ secs: UInt64) async {
  await withUnsafeContinuation { (c: UnsafeContinuation<Void, Never>) in
    if sleeperCont != nil {
      uartPuts("TIMER SLEEP PANIC: concurrent sleeper unsupported\n")
      while true { wait_for_interrupt() }
    }
    sleeperCont = c
    timer_sleep_arm(UInt(secs))   // C: record deadline + arm CNTP (irq_save-guarded)
  }
}

// Called from the CNTP timer IRQ (INTID 30). timer_sleep_due() (C) reads the
// counter, and either disables CNTP + returns 1 (matured) or re-arms + returns 0.
func serviceTimerSleeper() {
  if timer_sleep_due() != 0 {
    if let c = sleeperCont {
      sleeperCont = nil
      c.resume()                  // enqueues the continuation on our executor
    }
  }
}
